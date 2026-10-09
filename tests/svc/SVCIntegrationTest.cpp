#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDomDocument>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtEndian>
#include <QtTest>
#include <cmath>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "FadeButton.h"
#include "InstrumentTrack.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "PluginBrowser.h"
#include "ProjectJournal.h"
#include "ProjectRenderer.h"
#include "RenameDialog.h"
#include "SVCBrowser.h"
#include "SVCCache.h"
#include "SVCCatalog.h"
#include "SVCClip.h"
#include "SVCConversion.h"
#include "SVCSettingsPage.h"
#include "SVCTrack.h"
#include "SVCViews.h"
#include "SVCWindow.h"
#include "SVSBrowser.h"
#include "SVSClip.h"
#include "SVSCanvas.h"
#include "Timeline.h"
#include "TimeLineWidget.h"
#include "SVSTrack.h"
#include "SVSViews.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "SampleTrack.h"
#include "SetupDialog.h"
#include "Song.h"
#include "embed.h"

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
			QCOMPARE(root->child(0)->childCount(), 0); // Single speaker has no redundant level.
			const QTreeWidgetItem* fixture = nullptr;
			for (int index = 0; index < tree->topLevelItemCount(); ++index)
			{
				if (tree->topLevelItem(index)->text(0) == profile.name) { fixture = tree->topLevelItem(index); }
			}
			QVERIFY(fixture);
			QCOMPARE(fixture->child(0)->childCount(), 2);
			QVERIFY(!fixture->child(0)->child(0)->icon(0).isNull());
			auto* search = browser.findChild<QLineEdit*>("svcBrowserSearch");
			QVERIFY(search);
			QCOMPARE(browser.title(), QString("SVC"));
			QCOMPARE(search->maxLength(), 64);
			QVERIFY(search->isClearButtonEnabled());
			search->setText(QString::fromUtf8("说话人乙"));
			QVERIFY(!fixture->isHidden());
			QVERIFY(fixture->child(0)->child(0)->isHidden());
			QVERIFY(!fixture->child(0)->child(1)->isHidden());
			search->setText(profile.name);
			QVERIFY(!fixture->child(0)->child(0)->isHidden());
			search->setText("NO-SVC-MATCH");
			QVERIFY(fixture->isHidden());
			QVERIFY(svc::Catalog::instance().install(profile).isEmpty());
			for (int index = 0; index < tree->topLevelItemCount(); ++index)
			{
				QVERIFY(tree->topLevelItem(index)->isHidden());
			}
			search->clear();
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

	void svsSidebarSeparationAndSearch()
	{
		QCOMPARE(m_gui->mainWindow()->findChildren<gui::SVSBrowser*>().size(), 1);
		QVERIFY(!m_gui->mainWindow()->findChild<QAction*>("svsAddTrackAction"));
		const auto* instruments = m_gui->mainWindow()->findChild<gui::PluginBrowser*>();
		QVERIFY(instruments);
		const auto* instrumentTree = instruments->findChild<QTreeWidget*>();
		QVERIFY(instrumentTree);
		for (int index = 0; index < instrumentTree->topLevelItemCount(); ++index)
		{
			QVERIFY(instrumentTree->topLevelItem(index)->text(0) != "Singing Voice Synthesis");
		}
		int sidebarIndex = 0;
		for (const auto* page : m_gui->mainWindow()->findChildren<gui::SideBarWidget*>())
		{
			const auto title = page->title();
			QToolButton* tab = nullptr;
			for (auto* button : m_gui->mainWindow()->findChildren<QToolButton*>())
			{
				if (button->toolTip() == title) { tab = button; }
			}
			QVERIFY(tab);
			if (!tab->isChecked()) { QTest::mouseClick(tab, Qt::LeftButton); }
			QTest::qWait(300);
			const auto evidenceName = title == "SVC" || title == "SVS"
				? title + "-sidebar-main-native.png"
				: QString("sidebar-header-%1-native.png").arg(sidebarIndex);
			++sidebarIndex;
			QVERIFY(m_gui->mainWindow()
					->screen()
					->grabWindow(m_gui->mainWindow()->winId())
					.save("build/tests/svc/" + evidenceName));
			QTest::mouseClick(tab, Qt::LeftButton);
		}
		gui::SVSBrowser browser(nullptr);
		browser.resize(320, 600);
		browser.show();
		QVERIFY(QTest::qWaitForWindowExposed(&browser));
		QCOMPARE(browser.title(), QString("SVS"));
		auto* search = browser.findChild<QLineEdit*>("svsBrowserSearch");
		auto* tree = browser.findChild<QTreeWidget*>("svsBrowserTree");
		QVERIFY(search && tree);
		QCOMPARE(search->maxLength(), 64);
		QVERIFY(search->isClearButtonEnabled());
		// Exercise filtering through the real page with deterministic catalog widgets, without changing installed
		// engines.
		static const PixmapLoader logo("sample_track");
		static Plugin::Descriptor descriptor{"svs", "Singing Voice Synthesis", "Native singing voice synthesis", "LMMS",
			1, Plugin::Type::SVS, &logo, "", nullptr};
		auto* engine = new QTreeWidgetItem(tree, {"Test SVS Engine"});
		auto* first = new QTreeWidgetItem(engine);
		auto* second = new QTreeWidgetItem(engine);
		tree->setItemWidget(first, 0,
			new gui::PluginDescWidget(
				{&descriptor, QString::fromUtf8("声库甲"), {{"pluginId", "fixture"}, {"voiceId", "a"}}}, tree));
		tree->setItemWidget(second, 0,
			new gui::PluginDescWidget(
				{&descriptor, QString::fromUtf8("声库乙"), {{"pluginId", "fixture"}, {"voiceId", "b"}}}, tree));
		engine->setExpanded(true);
		search->setText(QString::fromUtf8("声库乙"));
		QVERIFY(!engine->isHidden());
		QVERIFY(first->isHidden());
		QVERIFY(!second->isHidden());
		search->setText("test svs engine");
		QVERIFY(!first->isHidden() && !second->isHidden());
		search->setText("NO-SVS-MATCH");
		QVERIFY(engine->isHidden());
		search->clear();
		QVERIFY(!engine->isHidden() && !first->isHidden() && !second->isHidden());
		QTest::qWait(300);
		QVERIFY(browser.screen()->grabWindow(browser.winId()).save("build/tests/svc/SVS-sidebar-search-native.png"));
		browser.close();
	}

	void svcSettingsResize()
	{
		gui::SetupDialog dialog(gui::SetupDialog::ConfigTab::SvcSettings);
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		dialog.resize(dialog.width(), 680);
		QTest::qWait(100);
		QCOMPARE(dialog.height(), 680);
		dialog.resize(dialog.width(), 600);
		QTest::qWait(200);
		qInfo() << "SVC settings requested height 600; actual" << dialog.height() << "minimum"
				<< dialog.minimumHeight();
		QVERIFY(dialog.screen()->grabWindow(dialog.winId()).save("build/tests/svc/SVC-settings-resize-native.png"));
		QCOMPARE(dialog.height(), 600);
		auto* scroll = dialog.findChild<QScrollArea*>("svcSettingsScroll");
		QVERIFY(scroll);
		QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QTest::qWait(100);
		QVERIFY(dialog.screen()->grabWindow(dialog.winId()).save("build/tests/svc/SVC-settings-bottom-native.png"));
		dialog.close();
	}

	void voiceClipNames()
	{
		for (const auto type : {Track::Type::SVC, Track::Type::SVS})
		{
			auto* track = Track::create(type, Engine::getSong());
			auto* clip = track->createClip(0);
			clip->changeLength(TimePos::ticksPerBar() * 4);
			gui::ClipView* view = nullptr;
			const auto findView = [&] {
				for (auto* candidate : m_gui->mainWindow()->findChildren<gui::ClipView*>())
				{
					if (candidate->getClip() == clip) { view = candidate; }
				}
				return view != nullptr;
			};
			QTRY_VERIFY(findView());
			const auto name = QString::fromUtf8(type == Track::Type::SVC ? "转换主唱片段" : "合成主唱片段");
			const auto rename = [&](bool cancel) {
				bool invoked = false;
				QTimer watchdog;
				watchdog.setSingleShot(true);
				connect(&watchdog, &QTimer::timeout, this, [] {
					if (auto* dialog = QApplication::activeModalWidget()) { dialog->close(); }
					if (auto* menu = QApplication::activePopupWidget()) { menu->close(); }
				});
				watchdog.start(5000);
				QTimer::singleShot(100, this, [&] {
					auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
					QVERIFY(menu);
					QAction* action = nullptr;
					for (auto* candidate : menu->actions())
					{
						if (candidate->text() == "Change name") { action = candidate; }
					}
					QVERIFY(action);
					QTimer::singleShot(100, this, [&] {
						auto* dialog = qobject_cast<gui::RenameDialog*>(QApplication::activeModalWidget());
						QVERIFY(dialog);
						auto* input = dialog->findChild<QLineEdit*>();
						QVERIFY(input);
						input->setText(cancel ? "cancelled name" : name);
						QTest::qWait(100);
						QVERIFY(dialog->screen()
								->grabWindow(dialog->winId())
								.save(QString("build/tests/svc/%1-rename-native.png").arg(int(type))));
						QTest::keyClick(input, cancel ? Qt::Key_Escape : Qt::Key_Return);
						invoked = true;
					});
					action->trigger();
					menu->close();
				});
				QContextMenuEvent event(
					QContextMenuEvent::Mouse, QPoint(5, 5), view->mapToGlobal(QPoint(5, 5)), Qt::NoModifier);
				QApplication::sendEvent(view, &event);
				QVERIFY(invoked);
			};
			rename(false);
			QCOMPARE(clip->name(), name);
			rename(true);
			QCOMPARE(clip->name(), name);
			QDomDocument document;
			auto element = document.createElement("clip");
			document.appendChild(element);
			const auto saved = clip->saveState(document, element);
			auto* restored = track->createClip(TimePos::ticksPerBar() * 4);
			restored->restoreState(saved);
			QCOMPARE(restored->name(), name);
			if (type == Track::Type::SVC)
			{
				auto* svcTrack = static_cast<SVCTrack*>(track);
				auto* target = static_cast<SVCClip*>(clip);
				auto* untouched = static_cast<SVCClip*>(restored);
				QVERIFY(target->setSourceFile(m_source));
				QVERIFY(untouched->setSourceFile(m_source));
				QVERIFY(svcTrack->setSelection({{"engine_id", "reference"}, {"model_id", "identity"},
					{"speaker_id", "0"}, {"parameters", QJsonObject{{"gain", 0}}}}));
				const auto generation = untouched->playback()->generation();
				const auto status = untouched->status();
				QSignalSpy wholeTrack(svcTrack, &SVCTrack::renderRequested);
				bool invoked = false;
				QTimer::singleShot(100, this, [&] {
					auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
					QVERIFY(menu);
					for (auto* action : menu->actions())
					{
						if (action->text() == "Re-render")
						{
							action->trigger();
							invoked = true;
						}
					}
					menu->close();
				});
				QContextMenuEvent event(
					QContextMenuEvent::Mouse, QPoint(5, 5), view->mapToGlobal(QPoint(5, 5)), Qt::NoModifier);
				QApplication::sendEvent(view, &event);
				QVERIFY(invoked);
				QTRY_VERIFY_WITH_TIMEOUT(target->conversionComplete() || target->conversionFailed(), 10000);
				QVERIFY2(target->conversionComplete(), qPrintable(target->status()));
				QCOMPARE(wholeTrack.count(), 0);
				QCOMPARE(untouched->playback()->generation(), generation);
				QCOMPARE(untouched->status(), status);
				QVERIFY(untouched->cacheReferences().isEmpty());
			}
			QTest::qWait(100);
			QVERIFY(m_gui->mainWindow()
					->screen()
					->grabWindow(m_gui->mainWindow()->winId())
					.save(QString("build/tests/svc/%1-renamed-clip-native.png").arg(int(type))));
			delete track;
			QTest::qWait(30);
		}
	}

	void copySampleSliceToSVC()
	{
		auto* song = Engine::getSong();
		auto* sourceTrack = static_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* source = static_cast<SampleClip*>(sourceTrack->createClip(96));
		source->setSampleFile(m_source);
		source->setName("Copied slice");
		source->setAutoResize(false);
		source->setStartTimeOffset(-24);
		source->changeLength(48);
		source->setColor(QColor("#be6699"));
		auto* first = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, song));
		auto* second = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, song));
		first->setName(QString::fromUtf8("目标主唱"));
		second->setName("Other SVC");
		const auto selection = first->selection();
		gui::SampleClipView* view = nullptr;
		QTRY_VERIFY(([&] {
			for (auto* candidate : m_gui->mainWindow()->findChildren<gui::SampleClipView*>())
			{
				if (candidate->getClip() == source) { view = candidate; }
			}
			return view != nullptr;
		})());
		auto copy = [&] {
			bool invoked = false;
			QTimer watchdog;
			watchdog.setSingleShot(true);
			connect(&watchdog, &QTimer::timeout, this, [] {
				if (auto* menu = QApplication::activePopupWidget()) { menu->close(); }
			});
			watchdog.start(5000);
			QTimer::singleShot(100, this, [&] {
				auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
				if (!menu) { return; }
				auto* submenu = menu->findChild<QMenu*>("sampleCopyToSVC");
				if (submenu && submenu->isEnabled() && submenu->actions().size() == 2
					&& submenu->actions().first()->text() == first->name())
				{
					submenu->popup(menu->mapToGlobal(QPoint(menu->width(), 0)));
					QTest::qWait(100);
					submenu->screen()->grabWindow(submenu->winId()).save("build/tests/svc/sample-copy-menu-native.png");
					submenu->actions().first()->trigger();
					invoked = true;
					submenu->close();
				}
				menu->close();
			});
			QContextMenuEvent event(
				QContextMenuEvent::Mouse, QPoint(5, 5), view->mapToGlobal(QPoint(5, 5)), Qt::NoModifier);
			QApplication::sendEvent(view, &event);
			QVERIFY(invoked);
		};
		copy();
		QCOMPARE(first->getClips().size(), size_t(1));
		QVERIFY(second->getClips().empty());
		QCOMPARE(sourceTrack->getClips().size(), size_t(1));
		auto* result = static_cast<SVCClip*>(first->getClips().front());
		QCOMPARE(result->startPosition(), source->startPosition());
		QCOMPARE(result->startTimeOffset(), source->startTimeOffset());
		QCOMPARE(result->length(), source->length());
		QCOMPARE(result->name(), source->name());
		QCOMPARE(result->color(), source->color());
		QCOMPARE(first->selection(), selection);
		QCOMPARE(result->playback()->snapshot()->source->rate, uint32_t(source->sample().sampleRate()));
		QCOMPARE(result->playback()->snapshot()->source->frames(), uint64_t(source->sample().sampleSize()));
		QVERIFY(!result->conversionComplete());
		QVERIFY(QFile::exists(result->sourceFile()));
		// A fileless recording with reversal must copy its decoded samples as well.
		std::vector<SampleFrame> frames(1024);
		for (size_t n = 0; n < frames.size(); ++n)
		{
			frames[n][0] = float(n) / 2048;
			frames[n][1] = -float(n) / 2048;
		}
		const auto encoded
			= QByteArray(reinterpret_cast<const char*>(frames.data()), frames.size() * sizeof(SampleFrame)).toBase64();
		source->setSampleBuffer(SampleBuffer::fromBase64(QString::fromLatin1(encoded), 44100));
		source->setReversed(true);
		copy();
		QCOMPARE(first->getClips().size(), size_t(2));
		auto* recorded = static_cast<SVCClip*>(first->getClips().back());
		QCOMPARE(recorded->playback()->snapshot()->trackSample(0, 0), frames.back()[0]);
		QCOMPARE(recorded->playback()->snapshot()->trackSample(1023, 1), frames.front()[1]);
		QDomDocument document;
		auto root = document.createElement("test");
		recorded->saveState(document, root);
		auto* restored = static_cast<SVCClip*>(second->createClip(0));
		restored->restoreState(root.firstChildElement());
		QCOMPARE(restored->playback()->snapshot()->trackSample(0, 0), frames.back()[0]);
		QTest::qWait(100);
		delete sourceTrack;
		delete first;
		delete second;
		QTest::qWait(30);
	}

	void voiceTrackMixerAndActivity()
	{
		for (const auto type : {Track::Type::SVC, Track::Type::SVS})
		{
			auto* track = Track::create(type, Engine::getSong());
			track->setName(QString::fromUtf8("人声混音通道"));
			track->setColor(QColor("#42b8a5"));
			gui::TrackView* view = nullptr;
			QTRY_VERIFY(([&] {
				for (auto* candidate : m_gui->mainWindow()->findChildren<gui::TrackView*>())
				{
					if (candidate->getTrack() == track) { view = candidate; }
				}
				return view != nullptr;
			})());
			auto* lamp = view->findChild<gui::FadeButton*>("voiceTrackActivity");
			QVERIFY(lamp);
			QCOMPARE(lamp->size(), QSize(8, 28));
			track->setMuted(true);
			QVERIFY(lamp->muted());
			track->setMuted(false);
			QVERIFY(!lamp->muted());
			auto* model = type == Track::Type::SVC ? static_cast<SVCTrack*>(track)->mixerChannelModel()
												   : static_cast<SVSTrack*>(track)->mixerChannelModel();
			const auto count = Engine::mixer()->numChannels();
			std::unique_ptr<QMenu> menu(view->createMixerMenu("Channel %1: %2", "New mixer channel"));
			QVERIFY(menu && menu->title().startsWith("Channel 0:"));
			menu->actions().first()->trigger();
			QCOMPARE(Engine::mixer()->numChannels(), count + 1);
			QCOMPARE(model->value(), count);
			QCOMPARE(Engine::mixer()->mixerChannel(count)->m_name, track->name());
			QCOMPARE(Engine::mixer()->mixerChannel(count)->color(), track->color());
			menu.reset(view->createMixerMenu("Assign to", "New mixer channel"));
			menu->actions().at(2)->trigger();
			QCOMPARE(model->value(), 0);
			const auto idle = lamp->grab().toImage();
			if (type == Track::Type::SVC)
			{
				auto* clip = static_cast<SVCClip*>(track->createClip(0));
				QVERIFY(clip->setSourceFile(m_source));
				QSignalSpy activity(static_cast<SVCTrack*>(track), &SVCTrack::playbackActivity);
				QVERIFY(static_cast<SVCTrack*>(track)->play(0, 64, 0));
				QCOMPARE(activity.count(), 1);
			}
			else
			{
				auto* voiceTrack = static_cast<SVSTrack*>(track);
				auto* clip = static_cast<SVSClip*>(track->createClip(0));
				svs::Note note;
				note.id = "activity";
				note.duration = 48;
				clip->setNotes({note});
				voiceTrack->play(0, 64, 0);
			}
			QTest::qWait(30);
			QVERIFY(lamp->grab().toImage() != idle);
			QVERIFY(m_gui->mainWindow()
					->screen()
					->grabWindow(m_gui->mainWindow()->winId())
					.save(QString("build/tests/svc/%1-activity-native.png").arg(int(type))));
			if (type == Track::Type::SVS) { static_cast<SVSTrack*>(track)->play(48, 64, 0); }
			QTest::qWait(400);
			QCOMPARE(lamp->grab().toImage(), idle);
			menu.reset();
			delete track;
			QTest::qWait(30);
		}
	}

	void svsClipTimelineNative()
	{
		auto* song = Engine::getSong();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		auto* clip = static_cast<SVSClip*>(track->createClip(193));
		clip->setAutoResize(false);
		clip->changeLength(384);
		clip->setStartTimeOffset(-24);
		{
			gui::SVSPianoRoll editor(clip);
			editor.resize(900, 600);
			editor.show();
			QVERIFY(QTest::qWaitForWindowExposed(&editor));
			gui::SVSCanvas* canvas = nullptr;
			for (auto* area : editor.findChildren<gui::SVSCanvas*>())
			{
				if (!area->isParameterLane()) { canvas = area; }
			}
			QVERIFY(canvas);
			canvas->setScroll(0, 72);
			song->getTimeline(Song::PlayMode::Song).setTicks(0);
			QTest::qWait(100);
			const auto ruler = canvas->grab(QRect(0, 0, canvas->width(), 24)).toImage();
			clip->movePosition(577);
			QTest::qWait(50);
			QCOMPARE(canvas->grab(QRect(0, 0, canvas->width(), 24)).toImage(), ruler);
			QTest::mouseClick(
				canvas->timeLine(), Qt::LeftButton, Qt::NoModifier, QPoint(int(canvas->pointAt(37, 60).x()), 12));
			QCOMPARE(song->getTimeline(Song::PlayMode::MidiClip).ticks(), 37);
			QCOMPARE(song->getTimeline(Song::PlayMode::Song).ticks(), 0);
			QTest::mouseClick(
				canvas->timeLine(), Qt::LeftButton, Qt::NoModifier, QPoint(int(canvas->pointAt(0, 60).x()), 12));
			QCOMPARE(song->getTimeline(Song::PlayMode::MidiClip).ticks(), 0);
			QCOMPARE(song->getTimeline(Song::PlayMode::Song).ticks(), 0);
			QTest::qWait(100);
			QVERIFY(editor.screen()->grabWindow(editor.winId()).save("build/tests/svc/SVS-clip-timeline-native.png"));
			editor.close();
		}
		delete track;
		QTest::qWait(30);
	}

	void svsInternalTransportNative()
	{
		auto* song = Engine::getSong();
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, song));
		QTest::qWait(50);
		auto* clip = static_cast<SVSClip*>(track->createClip(576));
		clip->setAutoResize(false);
		clip->changeLength(96);
		clip->setStartTimeOffset(-24);
		svs::Note note;
		note.id = "preview";
		note.tick = 24;
		note.duration = 96;
		clip->setNotes({note});
		auto* other = static_cast<SVSClip*>(track->createClip(576));
		other->setNotes({note});
		QSignalSpy starts(track, &SVSTrack::noteStarted);
		auto& main = song->getTimeline(Song::PlayMode::Song);
		auto& local = song->getTimeline(Song::PlayMode::MidiClip);
		main.setTicks(321);
		main.setFrameOffset(3);
		local.setLoopEnabled(false);
		local.setStopBehaviour(Timeline::StopBehaviour::BackToStart);
		local.setTicks(24);
		{
			gui::SVSPianoRoll editor(clip);
			editor.resize(1000, 650);
			editor.show();
			QVERIFY(QTest::qWaitForWindowExposed(&editor));
			auto* play = editor.findChild<QToolButton*>("svsPlayButton");
			auto* stop = editor.findChild<QToolButton*>("svsStopButton");
			QVERIFY(play && stop && editor.findChild<gui::TimeLineWidget*>("svsClipTimeline"));
			QTest::mouseClick(play, Qt::LeftButton);
			QCOMPARE(song->playMode(), Song::PlayMode::MidiClip);
			QCOMPARE(song->previewClip(), static_cast<const Clip*>(clip));
			for (int period = 0; local.ticks() < 48 && period < 1000; ++period)
			{
				Engine::audioEngine()->renderNextPeriod();
			}
			QVERIFY(local.ticks() >= 48);
			QCOMPARE(starts.count(), 1); // Only the selected overlapping clip contributes.
			QCOMPARE(main.ticks(), 321);
			QCOMPARE(main.frameOffset(), 3.f);
			const auto pausedTick = local.ticks();
			QTest::mouseClick(play, Qt::LeftButton);
			QVERIFY(song->isPaused());
			Engine::audioEngine()->renderNextPeriod();
			QCOMPARE(local.ticks(), pausedTick);
			QTest::mouseClick(play, Qt::LeftButton);
			QVERIFY(song->isPlaying());
			local.setTicks(119);
			for (int period = 0; local.ticks() >= 119 && period < 1000; ++period)
			{
				Engine::audioEngine()->renderNextPeriod();
			}
			QVERIFY(local.ticks() >= 24 && local.ticks() < 119);
			QTest::mouseClick(stop, Qt::LeftButton);
			QCOMPARE(local.ticks(), 24);
			QCOMPARE(main.ticks(), 321);
			local.setStopBehaviour(Timeline::StopBehaviour::BackToZero);
			local.setTicks(48);
			QTest::mouseClick(play, Qt::LeftButton);
			QTest::mouseClick(stop, Qt::LeftButton);
			QCOMPARE(local.ticks(), 24); // Cropped clip beginning, same as MIDI.
			local.setStopBehaviour(Timeline::StopBehaviour::KeepPosition);
			local.setTicks(60);
			QTest::mouseClick(play, Qt::LeftButton);
			local.setTicks(72);
			QTest::mouseClick(stop, Qt::LeftButton);
			QCOMPARE(local.ticks(), 72);
			QCOMPARE(main.ticks(), 321);
			gui::SVSCanvas* canvas = nullptr;
			for (auto* area : editor.findChildren<gui::SVSCanvas*>())
			{
				if (!area->isParameterLane()) { canvas = area; }
			}
			QVERIFY(canvas);
			QTest::keyClick(canvas, Qt::Key_Space);
			QCOMPARE(song->previewClip(), static_cast<const Clip*>(clip));
			QTest::keyClick(canvas, Qt::Key_Space, Qt::ShiftModifier);
			QVERIFY(song->isPaused());
			QTest::keyClick(canvas, Qt::Key_Space, Qt::ShiftModifier);
			QVERIFY(song->isPlaying());
			QTest::keyClick(canvas, Qt::Key_Space);
			QVERIFY(song->isStopped());
			QCOMPARE(main.ticks(), 321);
			QTest::qWait(100);
			QVERIFY(
				editor.screen()->grabWindow(editor.winId()).save("build/tests/svc/SVS-internal-transport-native.png"));
			editor.close();
		}
		// Switching away from running Song playback preserves its marker too.
		main.setStopBehaviour(Timeline::StopBehaviour::BackToZero);
		song->playSong();
		main.setTicks(400);
		song->playSVSClip(clip);
		QCOMPARE(main.ticks(), 400);
		delete clip; // Removing the preview target stops playback safely.
		QVERIFY(song->isStopped());
		song->playSVSClip(other);
		delete track; // Stop before the derived track's activity state is destroyed.
		QVERIFY(song->isStopped());
		local.setStopBehaviour(Timeline::StopBehaviour::BackToStart);
		main.setStopBehaviour(Timeline::StopBehaviour::BackToStart);
		QTest::qWait(30);
	}

	void svsNoteActivityRhythm()
	{
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		QTRY_VERIFY(([&] {
			for (auto* view : m_gui->mainWindow()->findChildren<gui::SVSTrackView*>())
			{
				if (view->getTrack() == track) { return true; }
			}
			return false;
		})());
		auto* clip = static_cast<SVSClip*>(track->createClip(192));
		clip->setAutoResize(false);
		clip->changeLength(192);
		clip->setStartTimeOffset(-24);
		svs::Note first, second;
		first.id = "first";
		first.tick = 24;
		first.duration = 24;
		second.id = "second";
		second.tick = 60;
		second.duration = 24;
		clip->setNotes({first, second});
		QSignalSpy starts(track, &SVSTrack::noteStarted), ends(track, &SVSTrack::noteEnded);
		for (int tick = 191; tick <= 252; ++tick)
		{
			track->play(tick, 64, 0);
			if (tick == 215)
			{
				QCOMPARE(starts.count(), 1);
				QCOMPARE(ends.count(), 0);
			}
			if (tick == 227)
			{
				QCOMPARE(starts.count(), 1);
				QCOMPARE(ends.count(), 1);
			}
			if (tick == 239)
			{
				QCOMPARE(starts.count(), 2);
				QCOMPARE(ends.count(), 1);
			}
		}
		QCOMPARE(starts.count(), 2);
		QCOMPARE(ends.count(), 2);
		track->play(192, 64, 0); // Seek into the first note.
		QCOMPARE(starts.count(), 3);
		track->setMuted(true);
		QCOMPARE(ends.count(), 3);
		track->setMuted(false);
		track->play(228, 64, 0);
		QCOMPARE(starts.count(), 4);
		emit Engine::getSong()->playbackStateChanged(); // Stopped/paused clears held notes.
		QCOMPARE(ends.count(), 4);
		delete track;
		QTest::qWait(30);
	}

	void svsGhostNotes()
	{
		auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
		auto* clip = static_cast<SVSClip*>(track->createClip(0));
		svs::Note first, second;
		first.id = "one";
		first.tick = 12;
		first.duration = 48;
		first.pitch = 60;
		second.id = "two";
		second.tick = 80;
		second.duration = 48;
		second.pitch = 67;
		clip->setNotes({first, second});
		clip->setStartTimeOffset(-24);
		clip->changeLength(72);
		auto* piano = m_gui->pianoRoll();

		auto* instrument = static_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		instrument->loadInstrument("tripleoscillator");
		QTest::qWait(100);
		auto* midi = static_cast<MidiClip*>(instrument->createClip(0));
		midi->addNote(Note(48, 0, 64), false);
		piano->setCurrentMidiClip(midi);
		piano->parentWidget()->show();
		piano->show();
		QTest::qWait(200);
		gui::SVSClipView* view = nullptr;
		QTRY_VERIFY(([&] {
			for (auto* candidate : m_gui->mainWindow()->findChildren<gui::SVSClipView*>())
			{
				if (candidate->getClip() == clip) { view = candidate; }
			}
			return view != nullptr;
		})());
		bool invoked = false;
		QTimer watchdog;
		watchdog.setSingleShot(true);
		connect(&watchdog, &QTimer::timeout, this, [] {
			if (auto* menu = QApplication::activePopupWidget()) { menu->close(); }
		});
		watchdog.start(5000);
		QTimer::singleShot(100, this, [&] {
			auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
			if (!menu) { return; }
			for (auto* action : menu->actions())
			{
				if (action->text() == "Set as ghost in piano-roll")
				{
					action->trigger();
					invoked = true;
					break;
				}
			}
			menu->close();
		});
		QContextMenuEvent event(
			QContextMenuEvent::Mouse, QPoint(5, 5), view->mapToGlobal(QPoint(5, 5)), Qt::NoModifier);
		QApplication::sendEvent(view, &event);
		QVERIFY(invoked);
		QTest::qWait(200);
		QVERIFY(piano->screen()->grabWindow(piano->winId()).save("build/tests/svc/SVS-ghost-notes-native.png"));
		QDomDocument document;
		auto root = document.createElement("pianoroll");
		piano->saveSettings(document, root);
		const auto notes = root.firstChildElement("ghostnotes").elementsByTagName("ghostnote");
		QCOMPARE(notes.size(), 2);
		QCOMPARE(notes.at(0).toElement().attribute("key").toInt(), 60);
		QCOMPARE(notes.at(0).toElement().attribute("pos").toInt(), 0);
		QCOMPARE(notes.at(0).toElement().attribute("len").toInt(), 36);
		QCOMPARE(notes.at(1).toElement().attribute("pos").toInt(), 56);
		QCOMPARE(notes.at(1).toElement().attribute("len").toInt(), 16);
		// A second invocation replaces the snapshot rather than accumulating notes.
		piano->setGhostSVSClip(clip);
		QDomDocument repeated;
		auto repeatRoot = repeated.createElement("pianoroll");
		piano->saveSettings(repeated, repeatRoot);
		QCOMPARE(repeatRoot.firstChildElement("ghostnotes").elementsByTagName("ghostnote").size(), 2);
		piano->setGhostSVSClip(nullptr);
		piano->setCurrentMidiClip(nullptr);
		piano->parentWidget()->hide();
		// Destroy fixture views before their model so periodic updates cannot hit
		// a view still awaiting deferred deletion after instrument teardown.
		for (auto* candidate : m_gui->mainWindow()->findChildren<gui::TrackView*>())
		{
			if (candidate->getTrack() == instrument) { candidate->close(); }
		}
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		delete instrument;
		QTest::qWait(30);
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
		gui::SVCBrowser browser(nullptr);
		auto* tree = browser.findChild<QTreeWidget*>("svcBrowserTree");
		QVERIFY(tree);
		const auto hasRvcCategory = [&] {
			for (int index = 0; index < tree->topLevelItemCount(); ++index)
			{
				if (tree->topLevelItem(index)->text(0) == catalog.engine("RVC").name) { return true; }
			}
			return false;
		};
		QVERIFY(catalog.setConnection("RVC", {"http://127.0.0.1:1", "", false}).isEmpty());
		QTRY_VERIFY_WITH_TIMEOUT(catalog.status("RVC").startsWith("Offline:"), 30000);
		QVERIFY(!catalog.engine("RVC").api);
		QVERIFY(!hasRvcCategory());
		{
			gui::SVCSettingsPage settings(nullptr);
			settings.setWindowTitle(QString::fromUtf8("SVC 引擎异步连接与限次重试"));
			settings.show();
			QVERIFY(QTest::qWaitForWindowExposed(&settings));
			auto* interval = settings.findChild<QSpinBox*>("svcReconnectInterval");
			auto* retries = settings.findChild<QSpinBox*>("svcReconnectRetries");
			auto* start = settings.findChild<QPushButton*>("svcAutoReconnect");
			auto* address = settings.findChild<QLineEdit*>("svcAddress_RVC");
			QVERIFY(interval && retries && start && address);
			interval->setValue(1);
			retries->setValue(2);
			int heartbeats = 0;
			QTimer heartbeat;
			connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; });
			heartbeat.start(10);
			QTest::mouseClick(start, Qt::LeftButton);
			QTRY_VERIFY_WITH_TIMEOUT(catalog.status("RVC").contains("automatic retries exhausted (2)"), 30000);
			QVERIFY(heartbeats > 10);
			QVERIFY(!hasRvcCategory());
			const auto exhausted = catalog.status("RVC");
			QTest::qWait(1500);
			QCOMPARE(catalog.status("RVC"), exhausted);
			address->setText("http://127.0.0.1:8000");
			QTest::mouseClick(start, Qt::LeftButton);
			QTRY_VERIFY_WITH_TIMEOUT(catalog.engine("RVC").api != nullptr, 15000);
			QVERIFY(hasRvcCategory());
			QVERIFY(settings.save());
			QVERIFY(catalog.engine("RVC").api); // Saving unchanged settings does not disconnect healthy engines.
			QTest::qWait(300);
			QVERIFY(settings.screen()->grabWindow(settings.winId()).save("build/tests/svc/SVC-M5-settings-native.png"));
			settings.close();
		}
		QVERIFY(catalog.setReconnectPolicy({}).isEmpty());
		const auto profile = catalog.engine("RVC");
		QJsonObject model;
		for (const auto& value : profile.capabilities.value("models").toArray())
		{
			if (value.toObject().value("id") == "芙宁娜") { model = value.toObject(); }
		}
		QVERIFY(!model.isEmpty());
		auto* track = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, Engine::getSong()));
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
		QCOMPARE(track->name(), model.value("name").toString());
		bool progressive = false;
		int conversionHeartbeats = 0;
		QTimer conversionHeartbeat;
		connect(&conversionHeartbeat, &QTimer::timeout, this, [&] { ++conversionHeartbeats; });
		conversionHeartbeat.start(10);
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
			auto* pitch = window.findChild<QSlider*>("svcSlider_pitch_shift");
			auto* pitchValue = window.findChild<QDoubleSpinBox*>("svcValue_pitch_shift");
			QVERIFY(pitch && pitchValue);
			QCOMPARE(pitch->property("minimumValue").toDouble(), -24.);
			QCOMPARE(pitch->property("maximumValue").toDouble(), 24.);
			QVERIFY(!window.findChild<QComboBox*>("svcSpeaker")->isVisible());
			pitch->setValue(5625);
			QCOMPARE(pitchValue->value(), 3.);
			QCOMPARE(track->selection().value("parameters").toObject().value("pitch_shift").toDouble(), 3.);
			pitchValue->setValue(-30);
			QCOMPARE(pitch->property("minimumValue").toDouble(), -30.);
			pitchValue->setValue(0);
			auto* rate = window.findChild<QDoubleSpinBox*>("svcValue_resample_sr");
			QVERIFY(rate);
			rate->setValue(12000);
			QCOMPARE(rate->value(), 0.);
			rate->setValue(32000);
			QCOMPARE(track->selection().value("parameters").toObject().value("resample_sr").toDouble(), 32000.);
			rate->setValue(0);
			for (const auto& value : svc::parameterDefinitions(profile, model))
			{
				const auto p = value.toObject();
				if (p.value("type") == "enum") { continue; }
				QVERIFY(window.findChild<QSlider*>("svcSlider_" + p.value("id").toString()));
				QVERIFY(window.findChild<QDoubleSpinBox*>("svcValue_" + p.value("id").toString()));
			}
			QTest::mouseClick(window.findChild<QPushButton*>("svcReRender"), Qt::LeftButton);
			QTRY_VERIFY_WITH_TIMEOUT(clip->conversionComplete() || clip->conversionFailed(), 120000);
			QVERIFY2(clip->conversionComplete(), qPrintable(clip->status()));
			QVERIFY(progressive);
			QVERIFY(conversionHeartbeats > 10);
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
