#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <limits>

#include "SVCCache.h"
#include "AICacheBudget.h"
#include "SVCChunking.h"

using namespace lmms::svc;
namespace {
void check(bool value, const char* message)
{
	if (!value)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

template <class Function> void invalid(Function function, const char* message)
{
	bool rejected = false;
	try
	{
		function();
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}
	check(rejected, message);
}

void coverage(const std::vector<Segment>& segments, uint64_t frames)
{
	uint64_t next = 0;
	for (const auto& segment : segments)
	{
		check(segment.start == next && segment.end > segment.start && segment.inputStart <= segment.start
				&& segment.inputEnd >= segment.end && segment.inputEnd <= frames && segment.algorithmVersion == 1,
			"continuous original source / effective-context mapping");
		next = segment.end;
	}
	check(next == frames, "no tail or silence removed");
}

QByteArray wave()
{
	QByteArray result(52, 0);
	result.replace(0, 4, "RIFF");
	qToLittleEndian<uint32_t>(44, result.data() + 4);
	result.replace(8, 8, "WAVEfmt ");
	qToLittleEndian<uint32_t>(16, result.data() + 16);
	qToLittleEndian<uint16_t>(1, result.data() + 20);
	qToLittleEndian<uint16_t>(1, result.data() + 22);
	qToLittleEndian<uint32_t>(16000, result.data() + 24);
	qToLittleEndian<uint32_t>(32000, result.data() + 28);
	qToLittleEndian<uint16_t>(2, result.data() + 32);
	qToLittleEndian<uint16_t>(16, result.data() + 34);
	result.replace(36, 4, "data");
	qToLittleEndian<uint32_t>(8, result.data() + 40);
	return result;
}

svc_event audio(const QByteArray& pcm, int index = 1, int offset = 0)
{
	svc_event event{};
	event.size = sizeof(event);
	event.type = SVC_AUDIO;
	event.sample_rate = 16000;
	event.channels = 1;
	event.chunk_index = index;
	event.sample_offset = offset;
	event.sample_count = pcm.size() / 2;
	event.bytes = reinterpret_cast<const uint8_t*>(pcm.constData());
	event.byte_count = pcm.size();
	return event;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	const ChunkConfig defaults;
	check(defaults.silenceThresholdDbfs == -70 && defaults.lengthThresholdSeconds == 30
			&& defaults.forcedChunkSeconds == 10 && defaults.validate().isEmpty(),
		"default knob settings");
	for (int seconds : {25, 30, 35})
	{
		std::vector<float> source(seconds * 1000, 0.5f);
		const auto segments = segmentAudio(source.data(), source.size(), 1, 1000);
		coverage(segments, source.size());
		check(segments.size() == (seconds > 30 ? 4 : 1), "strict 30-second threshold");
		if (seconds == 35)
		{
			check(segments[0].frames() == 10000 && segments[1].frames() == 10000 && segments[2].frames() == 10000
					&& segments[3].frames() == 5000,
				"35 -> 10+10+10+5");
		}
	}
	std::vector<float> silence(35000, 0);
	const auto silentSegments = segmentAudio(silence.data(), silence.size(), 1, 1000);
	coverage(silentSegments, silence.size());
	check(silentSegments.size() == 4, "all silence follows same length rule");
	std::vector<float> source(55000, 0.5f);
	std::fill(source.begin() + 17900, source.begin() + 18100, 0);
	const auto split = segmentAudio(source.data(), source.size(), 1, 1000);
	coverage(split, source.size());
	check(
		split.size() == 5 && split[0].frames() == 18000 && split[1].frames() == 10000 && split.back().frames() == 7000,
		"silence first: 18+37 -> 18+10+10+10+7");
	std::vector<float> stereo(55000 * 2);
	for (int index = 0; index < 55000; ++index)
	{
		stereo[index * 2] = 0.5f;
		stereo[index * 2 + 1] = -0.5f;
	}
	const auto antiphase = segmentAudio(stereo.data(), 55000, 2, 1000);
	check(antiphase.size() == 6 && antiphase.front().frames() == 10000, "antiphase never cancels channel RMS");
	for (int index = 17900; index < 18100; ++index)
	{
		stereo[index * 2] = 0;
	}
	check(segmentAudio(stereo.data(), 55000, 2, 1000).size() == 6, "any active channel prevents silence");
	std::vector<float> customSource(351, 0.5f);
	ChunkConfig custom{-50, 0.3, 0.1};
	const auto customSegments = segmentAudio(customSource.data(), customSource.size(), 1, 1000, custom);
	coverage(customSegments, customSource.size());
	check(customSegments.size() == 4 && customSegments.back().frames() == 51 && customSegments.back().padding == 39,
		"custom knobs and short tail padding preserve effective samples");
	std::vector<float> tiny(1, 0.5f);
	check(segmentAudio(tiny.data(), 1, 1, 1000).front().padding == 99, "tiny source not dropped");
	for (const auto& bad : {ChunkConfig{-121, 30, 10}, ChunkConfig{1, 30, 10}, ChunkConfig{-70, 0, 10},
			 ChunkConfig{-70, 30, 31}, ChunkConfig{-70, std::numeric_limits<double>::infinity(), 10},
			 ChunkConfig{std::numeric_limits<double>::quiet_NaN(), 30, 10}})
	{
		invalid([&]() { segmentAudio(source.data(), source.size(), 1, 1000, bad); }, "invalid knobs rejected");
	}
	invalid([&]() { segmentAudio(source.data(), source.size(), 1, 1000, {}, AudioLimits{0.1, 5, 1000000}); },
		"backend duration limit blocks request, no silent config adjustment");
	invalid([&]() { segmentAudio(source.data(), source.size(), 1, 1000, {}, AudioLimits{0.1, 600, 100}); },
		"backend byte limit blocks request");
	source.back() = std::numeric_limits<float>::quiet_NaN();
	invalid([&]() { segmentAudio(source.data(), source.size(), 1, 1000); }, "nonfinite audio rejected");

	QTemporaryDir working;
	check(working.isValid(), "isolated cache fixtures");
	const auto inputBytes = wave();
	QBuffer input;
	input.setData(inputBytes);
	input.open(QIODevice::ReadOnly);
	const QJsonObject snapshot{{"model_id", "identity"}, {"parameters", QJsonObject{{"gain", 0}}},
		{"connection_identity", "http://127.0.0.1:8000"}, {"source_start", "0"}, {"source_end", "4"}};
	auto pair = CachePair::create(working.path(), "Reference", input, snapshot);
	check(QFileInfo(pair->inputPath()).fileName() == QFileInfo(pair->outputPath()).fileName()
			&& pair->hash().size() == 64 && pair->inputPath().contains("cache/svc/Reference/input/")
			&& pair->outputPath().contains("cache/svc/Reference/output/"),
		"hash-only A/B identical names / fixed directories");
	check(pair->metadata().value("nonce").toString().size() == 32, "128-bit OS random nonce");
	const QByteArray pcm("\x01\x00\x02\x00\x03\x00\x04\x00", 8);
	check(pair->append(audio(pcm)), "write validated provisional part");
	check(QFile::exists(pair->partialPath()) && !QFile::exists(pair->outputPath())
			&& pair->metadata().value("chunks").toArray().size() == 1,
		"partial and manifest before done");
	check(!pair->complete(SVC_OK) && !pair->complete(SVC_FAILED), "only validated complete terminal may commit");
	check(pair->complete(SVC_COMPLETE) && QFile::exists(pair->outputPath()) && !QFile::exists(pair->partialPath()),
		"atomic final rename");
	QFile output(pair->outputPath());
	check(output.open(QIODevice::ReadOnly), "completed WAV readable");
	const auto outputBytes = output.readAll();
	check(outputBytes.size() == 52 && outputBytes.mid(44) == pcm
			&& qFromLittleEndian<uint32_t>(outputBytes.constData() + 40) == 8,
		"final WAV header and exact PCM");
	QFile original(pair->inputPath());
	check(original.open(QIODevice::ReadOnly) && original.readAll() == inputBytes, "source never modified");
	input.seek(0);
	auto failed = CachePair::create(working.path(), "Reference", input, snapshot);
	check(failed->hash() != pair->hash(), "same content has new identity");
	check(failed->append(audio(pcm)), "failure fixture partial");
	failed->fail();
	check(failed->metadata().value("state").toString() == "failed-partial" && !failed->complete(SVC_COMPLETE)
			&& !QFile::exists(failed->outputPath()),
		"failure never upgrades to full cache");
	input.seek(0);
	auto abandoned = CachePair::create(working.path(), "Reference", input, snapshot);
	const auto abandonedPath = abandoned->outputPath();
	abandoned.reset();
	QFile manifest(abandonedPath + ".json");
	check(manifest.open(QIODevice::ReadOnly)
			&& QJsonDocument::fromJson(manifest.readAll()).object().value("state").toString() == "cancelled-partial",
		"destruction records cancellation");
	for (const auto& engine : {"../escape", "CON", "RVC/name", "LPT1"})
	{
		input.seek(0);
		invalid([&]() { CachePair::create(working.path(), engine, input, snapshot); }, "unsafe engine rejected");
	}
	input.seek(0);
	invalid(
		[&]() {
			CachePair::create(working.path(), "Reference", input, {{"parameters", QJsonObject{{"token", "secret"}}}});
		},
		"credentials excluded from cache identity and metadata");
	std::vector<std::future<QString>> tasks;
	for (int index = 0; index < 16; ++index)
	{
		tasks.push_back(std::async(std::launch::async, [&]() {
			QBuffer concurrent;
			concurrent.setData(inputBytes);
			concurrent.open(QIODevice::ReadOnly);
			auto item = CachePair::create(working.path(), "Reference", concurrent, snapshot);
			return item->hash();
		}));
	}
	QSet<QString> names;
	for (auto& task : tasks)
	{
		names.insert(task.get());
	}
	check(names.size() == 16 && !names.contains(pair->hash()), "parallel unique exclusive allocation");
	QTemporaryDir managed;
	check(cacheLimit() == 2LL * 1024 * 1024 * 1024, "default global SVC cache limit is 2 GiB");
	input.seek(0);
	auto oldest = CachePair::create(managed.path(), "Reference", input, snapshot);
	check(oldest->append(audio(pcm)) && oldest->complete(SVC_COMPLETE), "old completed cache fixture");
	const auto oldestInput = oldest->inputPath();
	const auto oldestOutput = oldest->outputPath();
	oldest.reset();
	const auto oldestBytes = cacheBytes(managed.path());
	for (const auto& path : {oldestInput, oldestOutput, oldestOutput + ".json"})
	{
		QFile file(path);
		check(file.open(QIODevice::ReadWrite)
				&& file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1), QFileDevice::FileModificationTime),
			"deterministic oldest cache timestamp");
	}
	input.seek(0);
	auto newest = CachePair::create(managed.path(), "Other", input, snapshot);
	check(newest->append(audio(pcm)) && newest->complete(SVC_COMPLETE), "new completed cache fixture");
	const auto newestOutput = newest->outputPath();
	newest.reset();
	const auto newestBytes = cacheBytes(managed.path()) - oldestBytes;
	setCacheLimit(managed.path(), newestBytes);
	check(cacheBytes(managed.path()) == newestBytes && !QFile::exists(oldestInput)
			&& !QFile::exists(oldestOutput) && QFile::exists(newestOutput),
		"global capacity evicts oldest complete pair across engines");
	input.seek(0);
	auto active = CachePair::create(managed.path(), "Reference", input, snapshot);
	check(active->append(audio(pcm)), "active partial cache fixture");
	const auto activePath = active->partialPath();
	QFile unrelated(QDir(managed.path()).filePath("cache/svc/Reference/input/keep.txt"));
	check(unrelated.open(QIODevice::WriteOnly) && unrelated.write("keep") == 4, "unowned fixture");
	unrelated.close();
	check(!clearCache(managed.path()) && QFile::exists(activePath) && !QFile::exists(newestOutput),
		"one-click clear protects active jobs and removes inactive pairs");
	setCacheLimit(managed.path(), 0);
	active.reset();
	check(cacheBytes(managed.path()) == 0 && !QFile::exists(activePath) && QFile::exists(unrelated.fileName()),
		"job release enforces capacity without touching unowned files");
	setCacheLimit(managed.path(), 2LL * 1024 * 1024 * 1024);
	QTemporaryDir shared;
	input.seek(0);
	auto oldSvc = CachePair::create(shared.path(), "Reference", input, snapshot);
	check(oldSvc->append(audio(pcm)) && oldSvc->complete(SVC_COMPLETE), "shared budget SVC fixture");
	const auto oldSvcPath = oldSvc->outputPath();
	const auto oldSvcInput = oldSvc->inputPath();
	oldSvc.reset();
	for (const auto& path : {oldSvcInput, oldSvcPath, oldSvcPath + ".json"})
	{
		QFile file(path);
		check(file.open(QIODevice::ReadWrite)
				&& file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1), QFileDevice::FileModificationTime),
			"shared budget deterministic age");
	}
	const auto svsRoot = QDir(shared.path()).filePath("cache/SVS/DiffSinger");
	check(QDir().mkpath(svsRoot), "shared budget SVS directory");
	QFile tensor(QDir(svsRoot).filePath(QString(64, 'a') + ".tensor"));
	check(tensor.open(QIODevice::WriteOnly) && tensor.write(QByteArray(4096, 'x')) == 4096, "new SVS tensor fixture");
	tensor.close();
	setCacheLimit(shared.path(), 4096);
	check(!QFile::exists(oldSvcPath) && QFile::exists(tensor.fileName())
			&& lmms::aiCache::bytes(shared.path(), lmms::aiCache::Scope::All) == 4096,
		"shared total evicts old SVC before new SVS instead of reserving separate quotas");
	check(lmms::aiCache::clear(shared.path(), lmms::aiCache::Scope::All)
			&& lmms::aiCache::bytes(shared.path(), lmms::aiCache::Scope::All) == 0,
		"global clear covers both cache types");
	setCacheLimit(shared.path(), 2LL * 1024 * 1024 * 1024);
	std::puts("PASS SVC M1: defaults/custom, 30/35 boundaries, silence/stereo, tail padding, limits, random paired "
			  "cache, partial failure, concurrency");
	return 0;
}
