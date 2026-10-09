#include <QDomDocument>
#include <QFile>
#include <QPointer>
#include <QScreen>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerView.h"
#include "ProjectJournal.h"
#include "ProjectRenderer.h"
#include "SVCCache.h"
#include "SVCClip.h"
#include "SVCTrack.h"
#include "SVCViews.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
class SVCManualAudioDevice final : public AudioDummy
{
public:
	using AudioDummy::AudioDummy;

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

class SVCIntegrationTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_working;
	std::unique_ptr<gui::GuiApplication> m_gui;
	QString m_source;

	static QByteArray wave(uint32_t rate, uint32_t frames, int16_t sample)
	{
		QByteArray bytes(44 + frames * 2, 0);
		bytes.replace(0, 4, "RIFF");
		qToLittleEndian<uint32_t>(36 + frames * 2, bytes.data() + 4);
		bytes.replace(8, 8, "WAVEfmt ");
		qToLittleEndian<uint32_t>(16, bytes.data() + 16);
		qToLittleEndian<uint16_t>(1, bytes.data() + 20);
		qToLittleEndian<uint16_t>(1, bytes.data() + 22);
		qToLittleEndian<uint32_t>(rate, bytes.data() + 24);
		qToLittleEndian<uint32_t>(rate * 2, bytes.data() + 28);
		qToLittleEndian<uint16_t>(2, bytes.data() + 32);
		qToLittleEndian<uint16_t>(16, bytes.data() + 34);
		bytes.replace(36, 4, "data");
		qToLittleEndian<uint32_t>(frames * 2, bytes.data() + 40);
		for (uint32_t index = 0; index < frames; ++index)
		{
			qToLittleEndian<int16_t>(sample, bytes.data() + 44 + index * 2);
		}
		return bytes;
	}

private slots:
	void initTestCase()
	{
		QCOMPARE(QGuiApplication::platformName(), QString("windows"));
		QVERIFY(m_working.isValid());
		ConfigManager::inst()->loadConfigFile(m_working.filePath("svc-test-config.xml"));
		ConfigManager::inst()->setWorkingDir(m_working.path() + "/");
		ConfigManager::inst()->setValue("app", "configured", "1");
		ConfigManager::inst()->setValue("audioengine", "audiodev", AudioDummy::name());
		m_gui = std::make_unique<gui::GuiApplication>();
		bool available = false;
		Engine::audioEngine()->setAudioDevice(new SVCManualAudioDevice(available, Engine::audioEngine()), false);
		m_source = m_working.filePath("source.wav");
		QFile file(m_source);
		QVERIFY(file.open(QIODevice::WriteOnly));
		const auto data = wave(44100, 88200, 4096);
		QCOMPARE(file.write(data), data.size());
		file.close();
		Engine::getSong()->clearProject();
		m_gui->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_gui->mainWindow()));
	}

	void deletedClipBeforeViewDelivery()
	{
		auto* track = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, Engine::getSong()));
		QTest::qWait(30);
		QCOMPARE(m_gui->mainWindow()->findChildren<gui::SVCTrackView*>().size(), 1);
		QPointer<SVCClip> clip(static_cast<SVCClip*>(track->createClip(0)));
		delete clip.data();
		QVERIFY(clip.isNull());
		QTest::qWait(30);
		delete track;
		QTest::qWait(30);
		auto* immediatelyDeleted = Track::create(Track::Type::SVC, Engine::getSong());
		delete immediatelyDeleted;
		QTest::qWait(30);
	}

	void progressivePlaybackPersistenceAndRouting()
	{
		static_assert(static_cast<int>(Track::Type::Sample) == 2);
		static_assert(static_cast<int>(Track::Type::SVS) == 7);
		static_assert(static_cast<int>(Track::Type::SVC) == 8);
		auto* song = Engine::getSong();
		auto* track = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, song));
		QVERIFY(track);
		auto* clip = static_cast<SVCClip*>(track->createClip(0));
		QVERIFY(clip->setSourceFile(m_source));
		QVERIFY(!dynamic_cast<SampleTrack*>(track) && !dynamic_cast<SampleClip*>(clip));
		track->setName(QString::fromUtf8("SVC 音频转换"));
		QVERIFY(track->setSelection({{"engine_id", "reference"}, {"model_id", "identity"}, {"speaker_id", "0"},
			{"parameters", QJsonObject{{"gain", 0}}}}));
		const auto source = clip->playback()->snapshot()->source;
		const svc::Segment segment{0, source->frames(), 0, source->frames(), 0, source->rate};
		const auto generation = clip->beginConversion({segment});
		const auto first = wave(source->rate, source->frames() / 2, 16384).mid(44);
		svc_event event{};
		event.size = sizeof(event);
		event.type = SVC_AUDIO;
		event.generation_id = generation;
		event.request_id = "native-reference";
		event.sample_rate = source->rate;
		event.channels = 1;
		event.chunk_index = 1;
		event.total_chunks = 2;
		event.sample_count = first.size() / 2;
		event.bytes = reinterpret_cast<const uint8_t*>(first.constData());
		event.byte_count = first.size();
		QVERIFY(clip->publish(event));
		QCOMPARE(clip->playback()->snapshot()->trackSample(100, 0), .5f);
		QCOMPARE(clip->playback()->snapshot()->trackSample(source->frames() - 1, 0), .125f);
		QVERIFY(!clip->playback()->finished());
		{
			const auto exportPath = m_working.filePath("incomplete.wav");
			OutputSettings settings(44100, 192, OutputSettings::BitDepth::Depth16Bit);
			settings.setInteractiveErrors(false);
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, exportPath);
			QVERIFY(renderer.isReady());
			QSignalSpy rejected(&renderer, &ProjectRenderer::svsExportFailed);
			renderer.startProcessing();
			QCOMPARE(rejected.size(), 1);
			QVERIFY(renderer.renderError().contains("SVC"));
			QVERIFY(!renderer.renderSucceeded() && !QFile::exists(exportPath));
		}
		clip->movePosition(TimePos::ticksPerBar());
		QCOMPARE(clip->playback()->generation(), generation);
		QCOMPARE(clip->playback()->snapshot()->trackSample(100, 0), .5f);
		clip->movePosition(0);
		song->playSong();
		double energy = 0;
		for (int index = 0; index < 12; ++index)
		{
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod())
			{
				energy += frame[0] * frame[0] + frame[1] * frame[1];
			}
		}
		song->stop();
		QVERIFY2(energy > .01, "Published B did not reach LMMS mixer before terminal completion");
		track->setMuted(true);
		QVERIFY(!track->play(0, 64, 0));
		track->setMuted(false);
		clip->setMuted(true);
		QVERIFY(!track->play(0, 64, 0));
		clip->setMuted(false);
		auto* soloPeer = Track::create(Track::Type::Sample, song);
		QTest::qWait(30);
		track->setSolo(true);
		QVERIFY(track->isSolo() && !track->isMuted() && soloPeer->isMuted());
		track->setSolo(false);
		QVERIFY(!track->isSolo() && !soloPeer->isMuted());
		delete soloPeer;
		m_gui->mixerView()->addNewChannel();
		m_gui->mixerView()->addNewChannel();
		track->mixerChannelModel()->setValue(2);
		QVERIFY(Engine::mixer()->isChannelInUse(2));
		m_gui->mixerView()->moveChannelLeft(2);
		QCOMPARE(track->mixerChannelModel()->value(), 1);
		m_gui->mixerView()->deleteChannel(1);
		QCOMPARE(track->mixerChannelModel()->value(), 0);
		QFile input(m_source);
		QVERIFY(input.open(QIODevice::ReadOnly));
		auto pair = svc::CachePair::create(m_working.path(), "Reference", input, track->selection());
		QVERIFY(pair->append(event));
		event.chunk_index = 2;
		event.sample_offset = event.sample_count;
		QVERIFY(clip->publish(event));
		QVERIFY(pair->append(event));
		QVERIFY(pair->complete(SVC_COMPLETE));
		QVERIFY(clip->finishSegment(generation, 0, SVC_COMPLETE, {{"engine", "Reference"}, {"hash", pair->hash()}}));
		QCOMPARE(clip->status(), QString("Complete"));
		QVERIFY(clip->conversionComplete());
		QDomDocument document;
		auto root = document.createElement("test");
		track->saveState(document, root);
		auto* restored = static_cast<SVCTrack*>(Track::create(root.firstChildElement(), song));
		QVERIFY(restored);
		QCOMPARE(restored->selection(), track->selection());
		auto* restoredClip = static_cast<SVCClip*>(restored->getClip(0));
		QCOMPARE(restoredClip->status(), QString("Complete"));
		QVERIFY(restoredClip->conversionComplete());
		QCOMPARE(restoredClip->playback()->snapshot()->trackSample(100, 0), .5f);
		QDomDocument missingSourceDocument;
		auto missingSourceRoot = missingSourceDocument.createElement("test");
		clip->saveState(missingSourceDocument, missingSourceRoot);
		auto missingSourceState = missingSourceRoot.firstChildElement();
		missingSourceState.setAttribute("sourceFile", m_working.filePath("missing-source.wav"));
		restoredClip->restoreState(missingSourceState);
		QVERIFY(!restoredClip->playback() && !restoredClip->conversionComplete());
		QVERIFY(restoredClip->status().contains("Source missing"));
		auto* duplicate = static_cast<SVCClip*>(clip->clone());
		QCOMPARE(duplicate->sourceFile(), clip->sourceFile());
		QCOMPARE(duplicate->playback()->snapshot()->trackSample(100, 0), .5f);
		QPointer<SVCClip> weak(duplicate);
		const auto retainedPlayback = duplicate->playback();
		const auto oldGeneration = retainedPlayback->generation();
		delete duplicate;
		QVERIFY(weak.isNull() && retainedPlayback->generation() > oldGeneration);
		event.generation_id = generation;
		track->setChunkConfig({-60, 25, 5});
		QVERIFY(!clip->conversionComplete());
		QVERIFY(!clip->publish(event));
		QCOMPARE(clip->playback()->snapshot()->trackSample(100, 0), .5f);
		QVERIFY(QFile::remove(pair->outputPath()));
		auto* missing = static_cast<SVCTrack*>(Track::create(root.firstChildElement(), song));
		auto* missingClip = static_cast<SVCClip*>(missing->getClip(0));
		QCOMPARE(missingClip->playback()->snapshot()->trackSample(100, 0), .125f);
		QVERIFY(missingClip->status().contains("missing"));
		delete missing;
		delete restored;
		QTest::qWait(300);
		QVERIFY(!m_gui->mainWindow()->findChildren<gui::SVCClipView*>().isEmpty());
		const auto screenshot = m_gui->mainWindow()->screen()->grabWindow(m_gui->mainWindow()->winId());
		QVERIFY(!screenshot.isNull());
		QVERIFY(screenshot.save(qEnvironmentVariable("LMMS_SVC_EVIDENCE", "SVC-M2-native.png")));
		delete track;
		auto* sample = static_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(dynamic_cast<SampleClip*>(sample->createClip(0)));
		QTest::qWait(30);
		delete sample;
	}

	void cleanupTestCase()
	{
		Engine::getSong()->stop();
		Engine::getSong()->clearProject();
		Engine::getSong()->setModified(false);
		delete static_cast<QWidget*>(m_gui->mainWindow());
		m_gui.reset();
	}
};
QTEST_MAIN(SVCIntegrationTest)
#include "SVCIntegrationTest.moc"
