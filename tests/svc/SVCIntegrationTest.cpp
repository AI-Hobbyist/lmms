#include <QComboBox>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtEndian>
#include <QtTest>
#include <cmath>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerView.h"
#include "ProjectJournal.h"
#include "ProjectRenderer.h"
#include "SVCBrowser.h"
#include "SVCCache.h"
#include "SVCCatalog.h"
#include "SVCClip.h"
#include "SVCConversion.h"
#include "SVCTrack.h"
#include "SVCViews.h"
#include "SVCWindow.h"
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

	void capabilityControlsAndBrowser()
	{
		auto profile = svc::Catalog::instance().engine("reference");
		profile.id = "fixture";
		profile.name = QString::fromUtf8("中文模型引擎");
		profile.capabilities = QJsonDocument::fromJson(R"({"schema_version":1,"engine_id":"fixture",
		"models":[{"id":"multi","name":"多权重多说话人","weights":[{"id":"w1","name":"权重甲"},{"id":"w2","name":"权重乙"}],
		"speakers":[{"id":"0","name":"说话人甲"},{"id":"9","name":"说话人乙"}]}],
		"parameters":[{"id":"gain","name":"增益","type":"number","scope":"request","unit":"dB","default":0,"step":1,"minimum":-24,"maximum":24},
		{"id":"method","name":"方法","type":"enum","scope":"request","unit":"","default":"fast","options":[{"id":"fast","name":"快速","available":true},{"id":"missing","name":"不可用","available":false,"reason":"missing dependency"}]},
		{"id":"conditional","name":"条件参数","type":"number","scope":"request","unit":"s","default":1,"step":1,"minimum":0,"maximum":5,"enabled_when":{"speaker_id":"9"}}],
		"limits":{"max_upload_bytes":104857600,"max_seconds":600,"min_seconds":0.1}})")
								   .object();
		const auto installError = svc::Catalog::instance().install(profile);
		QVERIFY2(installError.isEmpty(), qPrintable(installError));
		auto* track = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, Engine::getSong()));
		QVERIFY(track->setSelection(
			{{"engine_id", "fixture"}, {"model_id", "multi"}, {"weight_id", "w2"}, {"speaker_id", "9"}}));
		{
			gui::SVCWindow window(track, m_gui->mainWindow());
			window.show();
			QVERIFY(QTest::qWaitForWindowExposed(&window));
			QTest::qWait(250);
			auto* weights = window.findChild<QComboBox*>("svcWeight");
			auto* speakers = window.findChild<QComboBox*>("svcSpeaker");
			QVERIFY(weights->isVisible() && speakers->isVisible());
			QCOMPARE(weights->currentData().toString(), QString("w2"));
			QCOMPARE(speakers->currentData().toString(), QString("9"));
			auto* conditional = window.findChild<QWidget*>("svcParameter_conditional");
			QVERIFY(conditional->isEnabled());
			speakers->setCurrentIndex(0);
			QVERIFY(!conditional->isEnabled());
			QString error;
			const auto request = svc::Catalog::instance().requestSelection(track->selection(), error);
			QVERIFY(error.isEmpty());
			QVERIFY(!request.value("parameters").toObject().contains("conditional"));
			auto* methods = window.findChild<QWidget*>("svcParameter_method")->findChild<QComboBox*>();
			QVERIFY(!(methods->model()->flags(methods->model()->index(1, 0)) & Qt::ItemIsEnabled));
			gui::SVCBrowser browser(nullptr);
			auto* tree = browser.findChild<QTreeWidget*>("svcBrowserTree");
			const auto* root = tree->topLevelItem(0);
			QVERIFY(root);
			QCOMPARE(root->child(0)->child(0)->childCount(), 0); // Single speaker has no redundant level.
			const QTreeWidgetItem* fixture = nullptr;
			for (int index = 0; index < root->childCount(); ++index)
			{
				if (root->child(index)->text(0) == profile.name) { fixture = root->child(index); }
			}
			QVERIFY(fixture);
			QCOMPARE(fixture->child(0)->childCount(), 2);
			QTest::qWait(250);
			const auto screenshot = window.screen()->grabWindow(window.winId());
			QVERIFY(!screenshot.isNull());
			QVERIFY(screenshot.save(
				"build/tests/svc/SVC-M3-controls-native" + qEnvironmentVariable("LMMS_SVC_DPI_SUFFIX") + ".png"));
			auto metadata = profile.capabilities.value("models").toArray().first().toObject();
			metadata.insert("require_weight_selection", true);
			metadata.insert("weights",
				QJsonArray{
					QJsonObject{{"id", "w1"}, {"name", "权重甲"}, {"supports_f0", false}, {"index_rate_enabled", false},
						{"speakers", QJsonArray{QJsonObject{{"id", "0"}, {"name", "说话人 0"}}}}},
					QJsonObject{{"id", "w2"}, {"name", "权重乙"}, {"supports_f0", true}, {"index_rate_enabled", true},
						{"speakers", metadata.value("speakers")}}});
			profile.capabilities.insert("models", QJsonArray{metadata});
			QVERIFY(svc::Catalog::instance().install(profile).isEmpty());
			weights->setCurrentIndex(0);
			QCOMPARE(speakers->count(), 1);
			QVERIFY(!speakers->isVisible());
			QVERIFY(!svc::selectionContext(track->selection(), metadata).value("supports_f0").toBool());
			weights->setCurrentIndex(1);
			QCOMPARE(speakers->count(), 2);
			QVERIFY(speakers->isVisible());
			QVERIFY(svc::selectionContext(track->selection(), metadata).value("supports_f0").toBool());
			QVERIFY(track->setSelection({{"engine_id", "fixture"}, {"model_id", "multi"}, {"speaker_id", "0"}}));
			QVERIFY(svc::Catalog::instance().install(profile).isEmpty());
			QVERIFY(weights->currentData().toString().isEmpty());
			svc::Catalog::instance().requestSelection(track->selection(), error);
			QVERIFY(!error.isEmpty());
			weights->setCurrentIndex(weights->findData("w2"));
			svc::Catalog::instance().requestSelection(track->selection(), error);
			QVERIFY(error.isEmpty());
			window.close();
		}
		QVERIFY(svc::setChunkDefaults({-65, 24, 8}).isEmpty());
		auto* defaultsTrack = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, Engine::getSong()));
		QCOMPARE(defaultsTrack->chunkConfig().silenceThresholdDbfs, -65.);
		QCOMPARE(track->chunkConfig().silenceThresholdDbfs, -70.);
		QVERIFY(svc::setChunkDefaults({}).isEmpty());
		delete defaultsTrack;
		delete track;
		QTest::qWait(30);
	}

	void referenceRerenderAndAudition()
	{
		auto* track = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, Engine::getSong()));
		track->setName(QString::fromUtf8("SVC 原音与转换试听"));
		auto* clip = static_cast<SVCClip*>(track->createClip(0));
		QVERIFY(clip->setSourceFile(m_source));
		QVERIFY(track->setSelection({{"engine_id", "reference"}, {"model_id", "identity"}, {"speaker_id", "0"},
			{"parameters", QJsonObject{{"gain", 0}}}}));
		{
			gui::SVCWindow window(track, m_gui->mainWindow());
			window.show();
			QVERIFY(QTest::qWaitForWindowExposed(&window));
			QVERIFY(!window.findChild<QComboBox*>("svcSpeaker")->isVisible());
			const auto sourceOnly = clip->playback()->snapshot();
			QCOMPARE(svc::auditionSample(*sourceOnly, 100, svc::AuditionMode::Source, 0, 0)[0], .125f);
			QCOMPARE(svc::auditionSample(*sourceOnly, 100, svc::AuditionMode::Rendered, 0, 0)[0], 0.f);
			auto* button = window.findChild<QPushButton*>("svcReRender");
			QVERIFY(button);
			QTest::mouseClick(button, Qt::LeftButton);
			QTRY_VERIFY_WITH_TIMEOUT(clip->conversionComplete(), 15000);
			const auto first = clip->playback()->snapshot();
			QCOMPARE(svc::auditionSample(*first, 100, svc::AuditionMode::Overlay, 0, 0)[0], .25f);
			QVERIFY(std::abs(svc::auditionSample(*first, 100, svc::AuditionMode::Overlay, 6.020599913, 0)[0] - .375f)
				< .00001f);
			const auto oldGeneration = clip->playback()->generation();
			auto* gain = window.findChild<QWidget*>("svcParameter_gain")->findChild<gui::Knob*>();
			gain->model()->setValue(6);
			QVERIFY(clip->playback()->generation() > oldGeneration && !clip->conversionComplete());
			QCOMPARE(clip->playback()->snapshot()->trackSample(100, 0), .125f); // Previous B survives until rerender.
			QTest::mouseClick(button, Qt::LeftButton);
			QTRY_VERIFY_WITH_TIMEOUT(clip->conversionComplete(), 15000);
			QVERIFY(std::abs(clip->playback()->snapshot()->trackSample(100, 0) - .249420f) < .0001f);
			const auto sum
				= svc::auditionSample(*clip->playback()->snapshot(), 100, svc::AuditionMode::Overlay, 24, 24)[0];
			QVERIFY(sum > 1); // No automatic normalization.
			auto* waveView = window.findChild<gui::SVCWaveform*>();
			QVERIFY(waveView);
			waveView->setStyleSheet("lmms--gui--SVCWaveform { qproperty-svcSourceColor: #ff6600; }");
			waveView->ensurePolished();
			QCOMPARE(waveView->sourceColor(), QColor("#ff6600"));
			waveView->setStyleSheet({});
			QTest::mouseClick(window.findChild<QPushButton*>("svcAuditionPlay"), Qt::LeftButton);
			QVERIFY(window.audition()->playing);
			for (int index = 0; index < 4; ++index)
			{
				Engine::audioEngine()->renderNextPeriod();
			}
			QVERIFY(window.audition()->position > 0);
			QTest::qWait(300);
			QVERIFY(window.screen()
					->grabWindow(window.winId())
					.save("build/tests/svc/SVC-M3-audition-native" + qEnvironmentVariable("LMMS_SVC_DPI_SUFFIX")
						+ ".png"));
			window.close();
			QVERIFY(!window.audition()->playing);
		}
		delete track;
		QTest::qWait(30);
	}

	void rvcLiveHost()
	{
		if (!qEnvironmentVariableIsSet("LMMS_SVC_LIVE")) { QSKIP("Set LMMS_SVC_LIVE for the local RVC host test"); }
		auto& catalog = svc::Catalog::instance();
		QVERIFY2(!catalog.engine("RVC").id.isEmpty(), "RVC module must be deployed beside development LMMS");
		QVERIFY(catalog.setConnection("RVC", {"http://127.0.0.1:8000", "", false}).isEmpty());
		QTRY_VERIFY_WITH_TIMEOUT(catalog.engine("RVC").api != nullptr, 15000);
		const auto profile = catalog.engine("RVC");
		QJsonObject model;
		for (const auto& value : profile.capabilities.value("models").toArray())
		{
			if (value.toObject().value("id") == "芙宁娜") { model = value.toObject(); }
		}
		QVERIFY(!model.isEmpty());
		auto* track = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, Engine::getSong()));
		track->setName(QString::fromUtf8("RVC 芙宁娜 · 实际 API"));
		auto data = wave(16000, 64000, 0);
		for (uint32_t i = 0; i < 64000; ++i)
		{
			qToLittleEndian<int16_t>(
				int16_t(6000 * std::sin(i * 6.28318530718 * 220 / 16000)), data.data() + 44 + i * 2);
		}
		const auto path = m_working.filePath("rvc-live.wav");
		QFile input(path);
		QVERIFY(input.open(QIODevice::WriteOnly));
		QCOMPARE(input.write(data), data.size());
		input.close();
		auto* clip = static_cast<SVCClip*>(track->createClip(0));
		QVERIFY(clip->setSourceFile(path));
		QVERIFY(track->setSelection({{"engine_id", "RVC"}, {"model_id", "芙宁娜"},
			{"weight_id", model.value("weights").toArray().first().toObject().value("id")}, {"speaker_id", "0"},
			{"parameters", QJsonObject{{"index_mode", "off"}, {"chunk_seconds", 1}, {"f0_method", "rmvpe"}}}}));
		bool progressive = false;
		connect(clip, &SVCClip::dataChanged, this, [&] {
			if (!clip->playback()->snapshot()->rendered.empty() && !clip->playback()->finished())
			{
				progressive = true;
				Engine::audioEngine()->renderNextPeriod();
			}
		});
		{
			gui::SVCWindow window(track, m_gui->mainWindow());
			window.show();
			QVERIFY(QTest::qWaitForWindowExposed(&window));
			auto* pitch = window.findChild<QWidget*>("svcParameter_pitch_shift")->findChild<gui::Knob*>();
			QVERIFY(pitch);
			QCOMPARE(pitch->model()->minValue(), -24.f);
			QCOMPARE(pitch->model()->maxValue(), 24.f);
			QTest::mouseClick(window.findChild<QPushButton*>("svcReRender"), Qt::LeftButton);
			QTRY_VERIFY_WITH_TIMEOUT(clip->conversionComplete() || clip->conversionFailed(), 120000);
			QVERIFY2(clip->conversionComplete(), qPrintable(clip->status()));
			QVERIFY(progressive);
			QVERIFY(clip->playback()->finished());
			QDomDocument saved;
			auto root = saved.createElement("clip");
			clip->saveState(saved, root);
			auto* copy = static_cast<SVCClip*>(track->createClip(0));
			copy->restoreState(root.firstChildElement());
			QVERIFY(copy->conversionComplete());
			QCOMPARE(
				copy->playback()->snapshot()->trackSample(1000, 0), clip->playback()->snapshot()->trackSample(1000, 0));
			delete copy;
			QTest::qWait(300);
			QVERIFY(window.screen()->grabWindow(window.winId()).save("build/tests/svc/SVC-M4-RVC-native.png"));
			window.close();
		}
		delete track;
		QTest::qWait(50);
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
