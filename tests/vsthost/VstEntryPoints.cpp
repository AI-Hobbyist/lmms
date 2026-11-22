#include <chrono>
#include <atomic>
#include <thread>
#include "Mixer.h"
#include "AudioBuffer.h"
#include <QtTest>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QElapsedTimer>
#include <windows.h>
#include <tlhelp32.h>
#include <set>
#include <string_view>
#include "agent/CommandBus.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentPlayHandle.h"
#include "MidiEvent.h"
#include "NotePlayHandle.h"
#include "PathUtil.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "SongEditor.h"
#include "PatternEditor.h"
#include "Clipboard.h"
#include "EffectChain.h"
#include "EffectRackView.h"
#include "MixerView.h"
#include "Effect.h"
#include "EffectControls.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "DataFile.h"
#include "FileBrowser.h"
#include "FileDialog.h"
#include "PresetPreviewPlayHandle.h"
#include "AudioEngine.h"
#include "AudioDevice.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "InstrumentView.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "EffectControlDialog.h"

// Native LMMS DLLs import lmms.exe. This test's output name is lmms.exe so
// imports resolve to the same Engine/CommandBus as the test, not a second copy.
#include "vsthost/CatalogJobs.h"
#include "vsthost/NativeVstPresetDrop.h"
#include "vsthost/NativeVstPreset.h"
#include "EffectSelectDialog.h"
#include "PluginBrowser.h"
#include <QTreeWidget>
#include "vsthost/VstCatalogSelection.h"
#include "vsthost/VstNodePorts.h"
#include <QTableView>
#include <QLineEdit>
#include <QLabel>
#include <QStandardPaths>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QPushButton>
#include <QScreen>
#include <QFileSystemModel>
#include "DummyEffect.h"
#include <QSignalSpy>
#include <limits>
#include <QScopeGuard>
#include <array>
#include <QListView>

class VstEntryPoints : public QObject
{
	Q_OBJECT
	QString m_fixtureRoot;
	QString m_fixture;
	QTemporaryDir m_fixtureDirectory;
	QElapsedTimer m_caseTimer;
	std::unique_ptr<lmms::gui::GuiApplication> m_liveGui;
	bool m_editorFunctionOnly = false;
	std::set<DWORD> fixtureProcesses(const char* entry, const wchar_t* moduleName = L"Vst2Baseline.dll")
	{
		std::set<DWORD> result;
		if (GetModuleHandleW(moduleName))
		{
			QTest::qFail("Third-party fixture was loaded in the DAW process", __FILE__, __LINE__);
			return result;
		}
		HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (processes == INVALID_HANDLE_VALUE)
		{
			return result;
		}
		PROCESSENTRY32W process{};
		process.dwSize = sizeof(process);
		if (Process32FirstW(processes, &process))
			do
			{
				if (process.th32ParentProcessID != GetCurrentProcessId())
				{
					continue;
				}
				HANDLE modules
					= CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process.th32ProcessID);
				if (modules == INVALID_HANDLE_VALUE)
				{
					continue;
				}
				MODULEENTRY32W module{};
				module.dwSize = sizeof(module);
				if (Module32FirstW(modules, &module))
					do
					{
						if (_wcsicmp(module.szModule, moduleName) == 0)
						{
							result.insert(process.th32ProcessID);
							qInfo() << "entry" << entry << "DAW PID" << GetCurrentProcessId() << "helper PID"
									<< process.th32ProcessID << "helper" << QString::fromWCharArray(process.szExeFile)
									<< "module" << QString::fromWCharArray(module.szExePath);
						}
					} while (Module32NextW(modules, &module));
				CloseHandle(modules);
			} while (Process32NextW(processes, &process));
		CloseHandle(processes);
		return result;
	}
	lmms::agent::CommandResult command(const QString& name, QJsonObject arguments = {})
	{
		QElapsedTimer commandTimer;
		commandTimer.start();
		auto result = lmms::agent::CommandBus::instance().execute(name, arguments);
		qInfo() << "entry_command_timing" << name << "elapsed_ms" << commandTimer.elapsed();
		if (!result.ok)
		{
			qWarning() << name << result.errorCode << result.errorMessage;
		}
		return result;
	}
	lmms::InstrumentTrack* track()
	{
		return dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks().at(0));
	}
	static bool dropStringPair(QWidget* destination, const QByteArray& payload)
	{
		QMimeData mime;
		mime.setData(lmms::Clipboard::mimeType(lmms::Clipboard::MimeType::StringPair), payload);
		QDragEnterEvent enter(QPoint(8, 8), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(destination, &enter);
		if (!enter.isAccepted())
		{
			return false;
		}
		QDropEvent drop(QPointF(8, 8), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(destination, &drop);
		return drop.isAccepted();
	}

	static bool dragEditorControl(HWND editor, const QPointF& start, const QPointF& change)
	{
		RECT frame{};
		MONITORINFO monitor{sizeof(MONITORINFO)};
		if (!GetWindowRect(editor, &frame)
			|| !GetMonitorInfoW(MonitorFromWindow(editor, MONITOR_DEFAULTTONEAREST), &monitor))
		{
			return false;
		}
		const auto work = monitor.rcWork;
		const auto x = std::clamp(frame.left, work.left, std::max(work.left, work.right - (frame.right - frame.left)));
		const auto y = std::clamp(frame.top, work.top, std::max(work.top, work.bottom - (frame.bottom - frame.top)));
		RECT bounds{};
		if (!GetClientRect(editor, &bounds))
		{
			return false;
		}
		const bool wasTopmost = (GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
		if (!SetWindowPos(editor, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE))
		{
			return false;
		}
		const auto restoreOrder = qScopeGuard([editor, wasTopmost] {
			if (!wasTopmost)
			{
				SetWindowPos(editor, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
		});
		SetForegroundWindow(editor);
		QTest::qWait(100);
		POINT origin{};
		if (!ClientToScreen(editor, &origin))
		{
			return false;
		}
		const auto point = [&bounds, origin](const QPointF& fraction) {
			return POINT{origin.x + std::lround(fraction.x() * bounds.right),
				origin.y + std::lround(fraction.y() * bounds.bottom)};
		};
		const auto first = point(start);
		const auto hit = WindowFromPoint(first);
		if (hit != editor && !IsChild(editor, hit))
		{
			return false;
		}
		if (!SetCursorPos(first.x, first.y))
		{
			return false;
		}
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		if (SendInput(1, &input, sizeof(input)) != 1)
		{
			return false;
		}
		const auto release = qScopeGuard([&input] {
			input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
			SendInput(1, &input, sizeof(input));
		});
		for (int step = 1; step <= 5; ++step)
		{
			QTest::qWait(25);
			const auto next = point(start + change * (step / 5.0));
			if (!SetCursorPos(next.x, next.y))
			{
				return false;
			}
		}
		QTest::qWait(25);
		return true;
	}
private slots:
	void init() { m_caseTimer.start(); }
	void cleanup()
	{
		qInfo() << "entry_case_timing" << QTest::currentTestFunction() << QTest::currentDataTag() << "elapsed_ms"
				<< m_caseTimer.elapsed();
	}
	void initTestCase()
	{
		const auto sourceRoot = qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT");
		QVERIFY(!sourceRoot.isEmpty());
		QVERIFY(m_fixtureDirectory.isValid());
		m_fixtureRoot = m_fixtureDirectory.path();
		// This suite covers production entry paths. Fault DLLs have their own
		// deadline suites and must not turn every effect-list query into a fault scan.
		for (const auto* architecture : {"x86", "x64"})
		{
			const auto relative = QString(architecture) + "/fixtures/Release/";
			QVERIFY(QDir().mkpath(m_fixtureRoot + '/' + relative));
			for (const auto* fixture : {"Vst2Baseline.dll", "Vst2Shell.dll"})
			{
				QVERIFY(QFile::copy(sourceRoot + '/' + relative + fixture, m_fixtureRoot + '/' + relative + fixture));
			}
		}
		QDir::setSearchPaths("plugins", {qEnvironmentVariable("LMMS_NATIVE_PLUGIN_DIR")});
		lmms::ConfigManager::inst()->setVSTDir(m_fixtureRoot + '/');
		QStandardPaths::setTestModeEnabled(true);
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
		lmms::NotePlayHandleManager::init();
		if (qEnvironmentVariableIsSet("LMMS_VST_LIVE_GUI"))
		{
			lmms::ConfigManager::inst()->loadConfigFile(m_fixtureRoot + "/test-config.xml");
			lmms::ConfigManager::inst()->setWorkingDir(m_fixtureRoot + '/');
			lmms::ConfigManager::inst()->setValue("audioengine", "audiodev", "SDL (Simple DirectMedia Layer)");
			lmms::ConfigManager::inst()->setValue("audioengine", "mididev", "Dummy (no MIDI support)");
			lmms::ConfigManager::inst()->setValue("audioengine", "framesperaudiobuffer", "256");
			lmms::ConfigManager::inst()->setValue("ui", "vstembedmethod", "win32");
			lmms::ConfigManager::inst()->setValue("app", "configured", "1");
			m_liveGui = std::make_unique<lmms::gui::GuiApplication>();
			m_liveGui->mainWindow()->show();
		}
		else
		{
			lmms::Engine::init(true);
		}
		lmms::ConfigManager::inst()->setVSTDir(m_fixtureRoot + '/');
		QVERIFY(!lmms::PluginFactory::instance()->pluginInfo("vestige").isNull());
		QVERIFY(!lmms::PluginFactory::instance()->pluginInfo("vsteffect").isNull());
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 30000);
		const auto report = lmms::Engine::vstCatalog()->snapshot().report;
		QVERIFY(report);
		QCOMPARE(report->entries.size(), std::size_t{6});
		QVERIFY(report->failures.empty());
	}
	// Exercise the catalog worker's real preset output with the installed helpers.
	void scanExportsNativePresets()
	{
		auto* config = lmms::ConfigManager::inst();
		config->setWorkingDir(m_fixtureRoot + '/');
		const auto nativeFixture = QDir(qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT"))
			.absoluteFilePath("../vst-host/x64/Release/Vst3Native.vst3");
		QVERIFY(QFile::copy(nativeFixture, m_fixtureRoot + "/Vst3Native.vst3"));
		QVERIFY(config->setVstScanRoots({{m_fixtureRoot, {"vst2", "vst3"}}}));
		const auto root = config->userPresetsDir();
		QVERIFY(lmms::Engine::refreshVstCatalog(nullptr, false, true));
		auto* jobs = lmms::Engine::vstCatalog();
		QTRY_VERIFY_WITH_TIMEOUT(!jobs->snapshot().busy, 60000);
		const auto report = jobs->snapshot().report;
		QVERIFY(report);
		QVERIFY(report->cachedModules > 0);
		QVERIFY(report->presetsWritten > 0);
		QDirIterator files(root, {"*.xpf"}, QDir::Files, QDirIterator::Subdirectories);
		qsizetype instruments = 0;
		qsizetype effects = 0;
		QSet<QString> formats;
		QString instrumentPath;
		QString effectPath;
		while (files.hasNext())
		{
			const auto path = files.next();
			const auto normalized = QDir::fromNativeSeparators(path);
			lmms::DataFile file(path);
			const auto preset = lmms::vsthost::readNativeVstPresetDrop(file);
			QVERIFY2(preset.hasAvailableIdentity(), qPrintable(path));
			const auto format = preset.key.attributes.value("format").toUpper();
			formats.insert(format);
			const auto role = preset.instrument ? QStringLiteral("乐器") : QStringLiteral("效果");
			QVERIFY2(normalized.contains(QStringLiteral("/VST/") + format + '/' + role + '/'), qPrintable(path));
			if (preset.instrument)
			{
				++instruments;
				if (format == "VST3") { instrumentPath = path; }
			}
			else
			{
				++effects;
				if (format == "VST2" && preset.state.hasAttribute("chunk")) { effectPath = path; }
			}
		}
		QCOMPARE(formats, (QSet<QString>{"VST2", "VST3"}));
		QVERIFY(instruments > 0);
		QVERIFY(effects > 0);

		if (m_liveGui)
		{
			const auto configurable = std::find_if(report->entries.cbegin(), report->entries.cend(), [](const auto& entry) {
				return entry.identity.format == lmms::vsthost::Format::Vst3 && !lmms::vsthost::isVstInstrument(entry)
					&& entry.layoutOutputs == 2 && (entry.supportedMainLayouts & (1u << 1));
			});
			QVERIFY(configurable != report->entries.cend());
			config->setValue("ui", "vstloadingmode", "native");
			lmms::gui::EffectSelectDialog selector(m_liveGui->mainWindow());
			selector.show();
			auto* search = selector.findChild<QLineEdit*>();
			auto* table = selector.findChild<QTableView*>();
			QVERIFY(search && table);
			search->setText(QRegularExpression::escape(configurable->name));
			QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 1, 3000);
			table->setCurrentIndex(table->model()->index(0, 0));
			const auto combos = selector.findChildren<QComboBox*>();
			auto combo = std::find_if(combos.cbegin(), combos.cend(), [](const auto* candidate) {
				return candidate->accessibleName() == "VST channel layout";
			});
			QVERIFY(combo != combos.cend());
			const int mono = (*combo)->findText("1 in / 1 out", Qt::MatchContains);
			QVERIFY(mono > 0);
			(*combo)->setCurrentIndex(mono);
			auto* mixerChain = &lmms::Engine::mixer()->mixerChannel(0)->m_fxChain;
			std::unique_ptr<lmms::Effect> selected(selector.instantiateSelectedPlugin(mixerChain));
			QVERIFY(selected && selected->isOkay());
			QCOMPARE(selected->key().attributes.value("mainlayout"), QStringLiteral("1"));
			auto* owner = dynamic_cast<lmms::vsthost::VstNodeOwner*>(selected.get());
			QVERIFY(owner && owner->vstNodeProcessor());
			using lmms::vsthost::NodePortDirection;
			QCOMPARE(owner->vstNodeProcessor()->nodeAudioIndex(0, NodePortDirection::Output), 0u);
			QCOMPARE(owner->vstNodeProcessor()->nodeAudioIndex(1, NodePortDirection::Output), UINT32_MAX);
		}

		QVERIFY(!effectPath.isEmpty());
		lmms::DataFile effectFile(effectPath);
		const auto effectPreset = lmms::vsthost::readNativeVstPresetDrop(effectFile);
		auto* chain = &lmms::Engine::mixer()->mixerChannel(0)->m_fxChain;
		const auto oldCount = chain->effectCount();
		if (m_liveGui)
		{
			m_editorFunctionOnly = true;
			auto* mixer = m_liveGui->mixerView();
			mixer->setCurrentMixerChannel(0);
			mixer->show();
			lmms::gui::EffectRackView* destination = nullptr;
			for (auto* rack : mixer->findChildren<lmms::gui::EffectRackView*>())
			{
				if (rack->model() == chain)
				{
					destination = rack;
					break;
				}
			}
			QVERIFY(destination);
			QVERIFY(dropStringPair(destination, "presetfile:" + effectPath.toUtf8()));
		}
		else
		{
			auto effect = lmms::vsthost::createNativeVstPresetEffect(effectPreset, chain);
			QVERIFY(effect);
			QCOMPARE(chain->effectCount(), oldCount);
			chain->appendEffect(effect.release());
		}
		QCOMPARE(chain->effectCount(), oldCount + 1);
		auto* effect = chain->effectAt(oldCount);
		QVERIFY(effect);
		QDomDocument effectDocument;
		auto parent = effectDocument.createElement("effects");
		const auto savedEffect = effect->saveState(effectDocument, parent)
			.firstChildElement("native_vst_effect_controls");
		QCOMPARE(savedEffect.attribute("chunk"), effectPreset.state.attribute("chunk"));
		if (m_liveGui)
		{
			auto* target = static_cast<lmms::InstrumentTrack*>(
				lmms::Track::create(lmms::Track::Type::Instrument, lmms::Engine::getSong()));
			QVERIFY(target->loadInstrument("tripleoscillator"));
			target->mixerChannelModel()->setValue(0);
			auto* owner = dynamic_cast<lmms::vsthost::VstNodeOwner*>(effect);
			QVERIFY(owner && owner->vstNodeProcessor());
			auto* processor = owner->vstNodeProcessor();
			std::atomic_bool heardSignal{false};
			{
				const auto guard = lmms::Engine::audioEngine()->requestChangesGuard();
				processor->setNodeAudioReceiver([&heardSignal](std::span<const float> audio, unsigned) {
					if (std::any_of(audio.begin(), audio.end(), [](float sample) { return std::abs(sample) > 0.001f; }))
					{
						heardSignal.store(true, std::memory_order_relaxed);
					}
				});
			}
			const auto detachMeter = qScopeGuard([processor] {
				const auto guard = lmms::Engine::audioEngine()->requestChangesGuard();
				processor->setNodeAudioReceiver({});
			});
			target->processInEvent(lmms::MidiEvent(lmms::MidiNoteOn, 0, 57, 100));
			const auto noteOff = qScopeGuard([target] {
				target->processInEvent(lmms::MidiEvent(lmms::MidiNoteOff, 0, 57, 0));
			});
			QTRY_VERIFY_WITH_TIMEOUT(heardSignal.load(std::memory_order_relaxed), 3000);
		}

		QVERIFY(!instrumentPath.isEmpty());
		lmms::DataFile instrumentFile(instrumentPath);
		auto* song = lmms::Engine::getSong();
		lmms::InstrumentTrack* track = nullptr;
		if (m_liveGui)
		{
			const auto before = song->tracks().size();
			QVERIFY(dropStringPair(m_liveGui->songEditor()->m_editor, "presetfile:" + instrumentPath.toUtf8()));
			QCOMPARE(song->tracks().size(), before + 1);
			track = dynamic_cast<lmms::InstrumentTrack*>(song->tracks().back());
		}
		else
		{
			track = static_cast<lmms::InstrumentTrack*>(lmms::Track::create(lmms::Track::Type::Instrument, song));
			QVERIFY(track->replaceInstrument(instrumentFile));
		}
		QVERIFY(track);
		QVERIFY(lmms::vsthost::nativeVstPresetRestored(track->instrument()));
		const auto expected = lmms::vsthost::readNativeVstPresetDrop(instrumentFile);
		QCOMPARE(track->instrument()->key().attributes.value("classid"), expected.key.attributes.value("classid"));
	}

	void layoutFamilyAcrossModules()
	{
		using namespace lmms::vsthost;
		CatalogEntry mono;
		mono.identity.format = Format::Vst3;
		mono.vendor = "Fixture Vendor";
		mono.product = "Fixture Effect";
		mono.name = "Fixture Effect Mono";
		mono.locator.modulePath = "C:/fixtures/Fixture Effect Mono.vst3";
		mono.layoutInputs = 1;
		mono.layoutOutputs = 1;
		auto stereo = mono;
		stereo.name = "Fixture Effect Stereo";
		stereo.locator.modulePath = "C:/fixtures/Fixture Effect Stereo.vst3";
		stereo.layoutInputs = 2;
		stereo.layoutOutputs = 2;
		const auto monoKey = vstCatalogKey(nullptr, mono);
		const auto stereoKey = vstCatalogKey(nullptr, stereo);
		QVERIFY(!monoKey.attributes.value("layoutfamily").isEmpty());
		QCOMPARE(monoKey.attributes.value("layoutfamily"), stereoKey.attributes.value("layoutfamily"));
		QVERIFY(monoKey.attributes.value("file") != stereoKey.attributes.value("file"));

		stereo.vendor = "A different vendor";
		QVERIFY(monoKey.attributes.value("layoutfamily")
			!= vstCatalogKey(nullptr, stereo).attributes.value("layoutfamily"));

		mono.name = mono.product;
		stereo.name = stereo.product;
		stereo.vendor = mono.vendor;
		QVERIFY(vstCatalogKey(nullptr, mono).attributes.value("layoutfamily")
			!= vstCatalogKey(nullptr, stereo).attributes.value("layoutfamily"));
	}

	void catalogPublicationAndSelector()
	{
		auto* jobs = lmms::Engine::vstCatalog();
		QVERIFY(jobs);
		const auto requested = jobs->snapshot().requested;
		lmms::ConfigManager::inst()->setValue("vst", "scanroots", "{invalid");
		QString error;
		QVERIFY(!lmms::Engine::refreshVstCatalog(&error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(jobs->snapshot().requested, requested);
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
		lmms::gui::EffectSelectDialog selector(nullptr);
		auto* table = selector.findChild<QTableView*>();
		QVERIFY(table);
		auto* search = selector.findChild<QLineEdit*>();
		QVERIFY(search);
		search->setText("^Shell Alpha$");
		QCOMPARE(table->model()->rowCount(), 2);
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({}));
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!jobs->snapshot().busy, 10000);
		QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 0, 3000);
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!jobs->snapshot().busy, 10000);
		QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 2, 3000);
		const auto factory = QDir(qEnvironmentVariable("LMMS_PLUGIN_DIR"))
								 .absoluteFilePath("../vst-host/x64/Release/Vst3Factory.vst3");
		QVERIFY(QFile::copy(factory, m_fixtureRoot + "/Vst3Factory.vst3"));
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2", "vst3"}}}));
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!jobs->snapshot().busy, 10000);
		const auto report = jobs->snapshot().report;
		QVERIFY(report);
		QCOMPARE(report->entries.size(), std::size_t{8});
		QVERIFY(report->failures.empty());
		lmms::Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		const auto* descriptor = lmms::PluginFactory::instance()->pluginInfo("vsteffect").descriptor;
		descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
		QCOMPARE(keys.size(), 7);
		const auto* instrumentDescriptor = lmms::PluginFactory::instance()->pluginInfo("vestige").descriptor;
		lmms::Plugin::Descriptor::SubPluginFeatures::KeyList instruments;
		instrumentDescriptor->subPluginFeatures->listSubPluginKeys(instrumentDescriptor, instruments);
		QCOMPARE(instruments.size(), 1);
		QCOMPARE(QString::fromUtf8(instruments.front().desc->name), QString("vestige"));
		QCOMPARE(instruments.front().name, QString("LMMS Beta"));
		lmms::gui::PluginBrowser sidebar(nullptr);
		auto* tree = sidebar.findChild<QTreeWidget*>();
		QVERIFY(tree);
		bool foundInstrument = false;
		for (auto* item : sidebar.findChildren<lmms::gui::PluginDescWidget*>())
		{
			foundInstrument |= item->name() == instruments.front().name;
		}
		QVERIFY(foundInstrument);
		int classes = 0;
		for (const auto& key : keys)
		{
			if (key.attributes.value("format") != "vst3")
			{
				continue;
			}
			++classes;
			const auto cid = QByteArray::fromHex(key.attributes.value("classid").toLatin1());
			QCOMPARE(cid.size(), 16);
			QCOMPARE(key.attributes.value("architecture"), QString("64"));
			bool exact = false;
			for (const auto& entry : report->entries)
			{
				if (entry.identity.format == lmms::vsthost::Format::Vst3
					&& cid == QByteArray(reinterpret_cast<const char*>(entry.identity.cid.data()), 16))
				{
					exact = key.name == entry.name && key.attributes.value("identity") == entry.identity.key()
						&& key.attributes.value("version") == entry.locator.version
						&& key.attributes.value("vendor") == entry.vendor;
				}
			}
			QVERIFY(exact);
		}
		QCOMPARE(classes, 1);
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!jobs->snapshot().busy, 10000);
		QVERIFY(fixtureProcesses("catalog-only scans leave no native instance").empty());
		selector.close();
	}
	void liveGuiAndAudio_data()
	{
		QTest::addColumn<QString>("architecture");
		QTest::addColumn<bool>("native");
		QTest::addColumn<bool>("instrument");
		for (const auto* architecture : {"x86", "x64"})
		{
			for (const bool native : {false, true})
			{
				for (const bool instrument : {true, false})
				{
					const auto tag
						= QString("%1-%2-%3")
							  .arg(architecture, native ? "vst3" : "vst2", instrument ? "instrument" : "effect");
					QTest::newRow(tag.toUtf8().constData()) << QString(architecture) << native << instrument;
				}
			}
		}
	}

	void installedNativeEditors_data()
	{
		QTest::addColumn<QString>("path");
		QTest::addColumn<QString>("classId");
		QTest::addColumn<bool>("instrument");

		QFile samples(qEnvironmentVariable("LMMS_VST_ACCEPTANCE_SAMPLES"));
		if (!samples.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			return;
		}
		while (!samples.atEnd())
		{
			const auto fields = QString::fromUtf8(samples.readLine()).trimmed().split('\t');
			QCOMPARE(fields.size(), 4);
			QTest::newRow(fields[0].toUtf8().constData()) << fields[1] << fields[2] << (fields[3] == "instrument");
		}
	}

	void installedNativeEditors()
	{
		if (!m_liveGui || !qEnvironmentVariableIsSet("LMMS_VST_ACCEPTANCE_SAMPLES"))
		{
			QSKIP("Requires native Windows GUI and an installed plug-in manifest");
		}
		m_editorFunctionOnly = true;
		QFETCH(QString, path);
		QFETCH(QString, classId);
		QFETCH(bool, instrument);
		const bool vst3 = classId.size() == 32;
		lmms::Plugin::Descriptor::SubPluginFeatures::Key key;
		key.attributes["file"] = path;
		key.attributes["format"] = vst3 ? "vst3" : "vst2";
		key.attributes["architecture"] = vst3 ? "64" : "32";
		if (vst3)
		{
			key.attributes["classid"] = classId;
		}
		qInfo() << "acceptance identity" << path << classId << instrument << key.attributes;

		const auto module = QFileInfo(path).fileName().toStdWString();
		const auto previousHelpers = fixtureProcesses("before native add", module.c_str());
		lmms::InstrumentTrack* target = nullptr;
		std::unique_ptr<QWidget> view;
		lmms::vsthost::VstNodeOwner* owner = nullptr;
		QMap<QString, lmms::AutomatableModel*> models;
		if (instrument)
		{
			const QByteArray name(QTest::currentDataTag());
			const bool pattern = name == "DSK-Ethereal-PadZ-2";
			const bool node = name == "Waves-CODEX-Stereo";
			auto* song = lmms::Engine::getSong();
			QWidget* destination = nullptr;
			lmms::TrackContainer* tracks = song;
			const char* entry = "song editor drop";
			if (node)
			{
				song->setModified(false);
				song->createNodeProject();
				m_liveGui->mainWindow()->showNodeWorkflow();
				auto* breadcrumb = m_liveGui->mainWindow()->findChild<QLabel*>("nodeWorkflowBreadcrumb");
				QVERIFY(breadcrumb);
				QVERIFY(QMetaObject::invokeMethod(breadcrumb, "linkActivated", Q_ARG(QString, QStringLiteral("song"))));
				destination = m_liveGui->mainWindow()->findChild<QWidget*>("nodeWorkflowCanvas");
				entry = "node editor drop";
			}
			else
			{
				lmms::gui::TrackContainerView* editor = pattern
					? static_cast<lmms::gui::TrackContainerView*>(m_liveGui->patternEditor()->m_editor)
					: static_cast<lmms::gui::TrackContainerView*>(m_liveGui->songEditor()->m_editor);
				destination = editor;
				tracks = editor->model();
				entry = pattern ? "pattern editor drop" : "song editor drop";
			}
			QVERIFY(destination);
			const auto before = tracks->tracks().size();
			const auto info = lmms::PluginFactory::instance()->pluginInfo("native_vst_instrument");
			QVERIFY(!info.isNull());
			key.desc = info.descriptor;
			lmms::Engine::setDndPluginKey(&key);
			const auto clearDragKey = qScopeGuard([] { lmms::Engine::setDndPluginKey(nullptr); });
			QVERIFY(dropStringPair(destination, "instrument:native_vst_instrument"));
			QCOMPARE(tracks->tracks().size(), before + 1);
			target = dynamic_cast<lmms::InstrumentTrack*>(tracks->tracks().back());
			QVERIFY(target && target->instrument());
			auto* plugin = target->instrument();
			QCOMPARE(QString::fromUtf8(plugin->descriptor()->name), QStringLiteral("native_vst_instrument"));
			qInfo() << "acceptance entry" << entry;
			models = plugin->parameterModels();
			owner = dynamic_cast<lmms::vsthost::VstNodeOwner*>(plugin);
			view.reset(plugin->createView(m_liveGui->mainWindow()));
		}
		else
		{
			auto* song = lmms::Engine::getSong();
			QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
			target = dynamic_cast<lmms::InstrumentTrack*>(song->tracks().back());
			QVERIFY(target);
			QVERIFY(target->loadInstrument("tripleoscillator"));
			const bool mixerEffect = QByteArray(QTest::currentDataTag()) == "ModernEqualizer";
			auto* chain = mixerEffect ? &lmms::Engine::mixer()->mixerChannel(0)->m_fxChain
				: target->audioBusHandle()->effects();
			lmms::Effect* activeEffect = nullptr;
			if (mixerEffect)
			{
				auto* mixer = m_liveGui->mixerView();
				mixer->setCurrentMixerChannel(0);
				mixer->show();
				target->mixerChannelModel()->setValue(0);
				lmms::gui::EffectRackView* rack = nullptr;
				for (auto* candidate : mixer->findChildren<lmms::gui::EffectRackView*>())
				{
					if (candidate->model() == chain)
					{
						rack = candidate;
						break;
					}
				}
				QVERIFY(rack);
				const auto directory = lmms::ConfigManager::inst()->userPresetsDir();
				QVERIFY(QDir().mkpath(directory));
				const auto presetPath = QDir(directory).filePath("ModernEqualizer-acceptance.xpf");
				{
					std::unique_ptr<lmms::Effect> source(lmms::Effect::instantiate("native_vst_effect", chain, &key));
					QVERIFY(source && source->isOkay());
					auto* parameter = source->controls()->parameterModels().value("param8");
					QVERIFY(parameter);
					parameter->setValue(0.625f);
					lmms::DataFile preset(lmms::DataFile::Type::EffectSettings);
					auto item = source->saveState(preset, preset.content());
					item.setAttribute("name", "native_vst_effect");
					item.appendChild(lmms::vsthost::nativeVstPresetKey(preset, source->key()));
					QVERIFY(preset.writeFile(presetPath));
				}
				const auto count = chain->effectCount();
				QVERIFY(dropStringPair(rack, "presetfile:" + presetPath.toUtf8()));
				QCOMPARE(chain->effectCount(), count + 1);
				activeEffect = chain->effectAt(count);
				QVERIFY(activeEffect);
				QCOMPARE(activeEffect->key().attributes.value("file"), path);
				const auto* parameter = activeEffect->controls()->parameterModels().value("param8");
				QVERIFY(parameter);
				QCOMPARE(parameter->value<float>(), 0.625f);
				qInfo() << "acceptance entry mixer xpf drop" << presetPath << "param8" << parameter->value<float>();
			}
			else
			{
				std::unique_ptr<lmms::Effect> effect(lmms::Effect::instantiate("native_vst_effect", chain, &key));
				QVERIFY(effect && effect->isOkay());
				activeEffect = effect.get();
				chain->appendEffect(effect.release());
				qInfo() << "acceptance entry instrument effect rack insert";
			}
			models = activeEffect->controls()->parameterModels();
			owner = dynamic_cast<lmms::vsthost::VstNodeOwner*>(activeEffect);
			view.reset(activeEffect->controls()->createView());
		}
		QVERIFY(owner);
		auto* processor = owner->vstNodeProcessor();
		QVERIFY(processor);
		if (QByteArray(QTest::currentDataTag()) == "Magma-BB-Tubes-Mono")
		{
			using lmms::vsthost::NodePortDirection;
			QCOMPARE(processor->nodeAudioIndex(0, NodePortDirection::Output), 0u);
			QCOMPARE(processor->nodeAudioIndex(1, NodePortDirection::Output), UINT32_MAX);
			qInfo() << "acceptance selected mono class" << classId;
		}
		QVERIFY(view);
		view->setWindowFlag(Qt::Window);
		view->show();
		auto* editorButton = view->findChild<QPushButton*>();
		QVERIFY(editorButton && editorButton->isEnabled());

		const auto newlyLoadedHelpers = [&] {
			auto helpers = fixtureProcesses("before native editor request", module.c_str());
			for (const auto pid : previousHelpers) { helpers.erase(pid); }
			return helpers;
		};
		std::set<DWORD> helpers;
		QTRY_COMPARE_WITH_TIMEOUT((helpers = newlyLoadedHelpers()).size(), std::size_t{1}, 3000);
		const DWORD helper = *helpers.begin();
		editorButton->click();
		struct EditorSearch
		{
			DWORD helper;
			HWND window = nullptr;
		};
		const auto editorWindow = [helper] {
			EditorSearch search{helper};
			const auto inspect = [](HWND window, LPARAM context) -> BOOL {
				auto& search = *reinterpret_cast<EditorSearch*>(context);
				DWORD pid = 0;
				GetWindowThreadProcessId(window, &pid);
				if (pid == search.helper && IsWindowVisible(window))
				{
					search.window = window;
					return FALSE;
				}
				return TRUE;
			};
			std::pair<WNDENUMPROC, EditorSearch*> enumeration{inspect, &search};
			EnumWindows([](HWND window, LPARAM context) -> BOOL {
				const auto& [inspect, search]
					= *reinterpret_cast<std::pair<WNDENUMPROC, EditorSearch*>*>(context);
				const auto query = reinterpret_cast<LPARAM>(search);
				if (!inspect(window, query)) { return FALSE; }
				EnumChildWindows(window, inspect, query);
				return search->window == nullptr;
			}, reinterpret_cast<LPARAM>(&enumeration));
			return search.window;
		};
		HWND editor = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((editor = editorWindow()) != nullptr, 5000);
		qInfo() << "acceptance editor" << reinterpret_cast<quintptr>(editor) << "helper" << helper;
		QTest::qWait(400);
		const auto captureDirectory = qEnvironmentVariable("LMMS_VST_ACCEPTANCE_CAPTURES");
		if (!captureDirectory.isEmpty())
		{
			RECT window{};
			QVERIFY(GetWindowRect(editor, &window));
			auto* screen = QGuiApplication::screenAt(QPoint(window.left, window.top));
			QVERIFY(screen);
			const auto screenshot = screen->grabWindow(reinterpret_cast<WId>(editor));
			const auto destination = QDir(captureDirectory).filePath(QString::fromUtf8(QTest::currentDataTag()) + ".png");
			QVERIFY2(!screenshot.isNull() && screenshot.save(destination), qPrintable(destination));
		}
		const auto controlsPath = qEnvironmentVariable("LMMS_VST_ACCEPTANCE_CONTROLS");
		QFile controls(controlsPath);
		if (!controlsPath.isEmpty())
		{
			QVERIFY2(controls.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(controlsPath));
			while (!controls.atEnd())
			{
				const auto fields = controls.readLine().trimmed().split('\t');
				QCOMPARE(fields.size(), 5);
				if (fields[0] != QTest::currentDataTag())
				{
					continue;
				}
				QMap<QString, float> before;
				for (auto it = models.cbegin(); it != models.cend(); ++it)
				{
					before.insert(it.key(), it.value()->value<float>());
				}
				QVERIFY(dragEditorControl(editor, {fields[1].toDouble(), fields[2].toDouble()},
					{fields[3].toDouble(), fields[4].toDouble()}));
				QTest::qWait(700);
				bool changed = false;
				for (auto it = models.cbegin(); it != models.cend(); ++it)
				{
					const auto actual = it.value()->value<float>();
					if (actual != before.value(it.key()))
					{
						qInfo() << "acceptance editor parameter" << it.key() << before.value(it.key()) << actual;
						changed = true;
					}
				}
				QVERIFY2(changed, "Native editor input did not produce a host parameter update");
				QVERIFY(IsWindowVisible(editor));
				break;
			}
		}

		std::atomic_uint blocks{0};
		std::atomic<float> peak{0};
		{
			const auto guard = lmms::Engine::audioEngine()->requestChangesGuard();
			processor->setNodeAudioReceiver([&](std::span<const float> audio, unsigned) {
				float largest = peak.load(std::memory_order_relaxed);
				for (const float sample : audio)
				{
					largest = std::max(largest, std::abs(sample));
				}
				peak.store(largest, std::memory_order_relaxed);
				blocks.fetch_add(1, std::memory_order_relaxed);
			});
		}
		const auto detachMeter = qScopeGuard([processor] {
			const auto guard = lmms::Engine::audioEngine()->requestChangesGuard();
			processor->setNodeAudioReceiver({});
		});
		target->processInEvent(lmms::MidiEvent(lmms::MidiNoteOn, 0, 57, 100));
		QTest::qWait(650);
		target->processInEvent(lmms::MidiEvent(lmms::MidiNoteOff, 0, 57, 0));
		const auto measuredPeak = peak.load(std::memory_order_relaxed);
		const auto processedBlocks = blocks.load(std::memory_order_relaxed);
		qInfo() << "acceptance realtime output" << "blocks" << processedBlocks << "peak" << measuredPeak;
		QVERIFY2(processedBlocks != 0, "The plug-in did not process any realtime audio blocks");
		QVERIFY2(IsWindowVisible(editor), "Editor disappeared during realtime audio");

		QVERIFY2(!models.isEmpty(), "Editor opened, but no writable host parameter was exposed");
		const auto parameter = models.constBegin();
		const auto before = parameter.value()->value<float>();
		const auto requested = before < 0.5f ? 0.75f : 0.25f;
		parameter.value()->setValue(requested, true);
		QTest::qWait(600);
		qInfo() << "acceptance host parameter" << parameter.key() << before << requested
			<< parameter.value()->value<float>();
		QVERIFY2(IsWindow(editor) && IsWindowVisible(editor), "Editor disappeared after host parameter input");
		QVERIFY2(fixtureProcesses("after host parameter", module.c_str()).contains(helper),
			"The sandbox process disappeared after host parameter input");
		QVERIFY2(parameter.value()->value<float>() != before, "The plug-in did not accept the requested parameter value");
		QVERIFY2(measuredPeak > 0.001f, "No signal at the plug-in output with the loaded plug-in state");
	}

	void nativeEditorParameterAndLayout()
	{
		if (!m_liveGui) { QSKIP("Requires the native Windows GUI"); }
		m_editorFunctionOnly = true;
		auto* song = lmms::Engine::getSong();
		song->createNodeProject();
		QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
		const auto path = QDir(qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT"))
			.absoluteFilePath("../vst-host/x64/Release/Vst3Native.vst3");
		lmms::Plugin::Descriptor::SubPluginFeatures::Key key;
		key.attributes["file"] = path;
		key.attributes["format"] = "vst3";
		key.attributes["architecture"] = "64";
		key.attributes["classid"] = "0002000154761032fedcba9824681357";
		auto* targetTrack = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks().back());
		QVERIFY(targetTrack);
		const auto finishAudio = qScopeGuard([] {
			lmms::Engine::audioEngine()->audioDev()->stopProcessing();
			lmms::Engine::getSong()->setModified(false);
		});
		auto* plugin = targetTrack->loadInstrument("native_vst_instrument", &key);
		QVERIFY(plugin);
		auto* owner = dynamic_cast<lmms::vsthost::VstNodeOwner*>(plugin);
		QVERIFY(owner);
		auto* processor = owner->vstNodeProcessor();
		QVERIFY(processor);
		auto* gain = plugin->parameterModels().value("vst3param_f0000101");
		QVERIFY(gain);
		if (qEnvironmentVariableIsSet("LMMS_VST3_FIXTURE_PARAMETER_CAPABILITIES"))
		{
			const auto models = plugin->parameterModels();
			QCOMPARE(models.size(), 2);
			QVERIFY(models.contains("vst3param_0000002a"));
			QCOMPARE(models.value("vst3param_0000002a")->step<float>(), 1.f);
		}
		using lmms::vsthost::NodePortDirection;
		QCOMPARE(processor->nodeAudioIndex(0, NodePortDirection::Output), 0u);
		QCOMPARE(processor->nodeAudioIndex(1, NodePortDirection::Output), 1u);
		const auto trackId = targetTrack->nodeWorkflowId().toString(QUuid::WithoutBraces);
		const auto scope = QStringLiteral("source/") + trackId + '/';
		const auto outputPort = QStringLiteral("source-plugin/") + trackId + "/audio-out:0:1";
		const auto destination = QStringLiteral("track-output/") + trackId + "/in-R";
		QVERIFY(song->setNodeCustomConnection(scope, outputPort, destination, true));

		std::unique_ptr<QWidget> view(plugin->createView(m_liveGui->mainWindow()));
		QVERIFY(view);
		view->setWindowFlag(Qt::Window);
		view->show();
		if (qEnvironmentVariableIsSet("LMMS_VST3_FIXTURE_PARAMETER_CAPABILITIES"))
		{
			auto* parameters = view->findChild<QComboBox*>();
			QVERIFY(parameters);
			const auto trim = parameters->findText("Panel trim (plug-in editor only)");
			QVERIFY(trim >= 0);
			QVERIFY(!parameters->itemData(trim).isValid());
			QCOMPARE(parameters->findText("Meter"), -1);
		}
		auto* editorButton = view->findChild<QPushButton*>();
		QVERIFY(editorButton);
		QVERIFY(editorButton->isEnabled());
		editorButton->click();
		const auto closeEditor = qScopeGuard([editorButton] { editorButton->click(); });
		const auto helpers = fixtureProcesses("native editor parameter and layout", L"Vst3Native.vst3");
		QCOMPARE(helpers.size(), std::size_t{1});
		struct EditorSearch
		{
			DWORD helper;
			HWND editor = nullptr;
		};
		const auto findEditor = [&] {
			EditorSearch search{*helpers.begin()};
			const auto inspect = [](HWND window, LPARAM context) -> BOOL {
				auto& found = *reinterpret_cast<EditorSearch*>(context);
				DWORD pid = 0;
				GetWindowThreadProcessId(window, &pid);
				if (pid != found.helper) { return TRUE; }
				wchar_t text[64]{};
				if (IsWindowVisible(window)
					&& SendMessageTimeoutW(window, WM_GETTEXT, std::size(text), reinterpret_cast<LPARAM>(text),
						SMTO_ABORTIFHUNG, 100, nullptr)
					&& std::wstring_view(text) == L"LMMS VST3 fixture editor")
				{
					found.editor = window;
					return FALSE;
				}
				return TRUE;
			};
			EnumChildWindows(reinterpret_cast<HWND>(m_liveGui->mainWindow()->winId()), inspect,
				reinterpret_cast<LPARAM>(&search));
			if (!search.editor)
			{
				std::pair<WNDENUMPROC, EditorSearch*> details{inspect, &search};
				EnumWindows([](HWND window, LPARAM context) -> BOOL {
					const auto details = reinterpret_cast<std::pair<WNDENUMPROC, EditorSearch*>*>(context);
					EnumChildWindows(window, details->first, reinterpret_cast<LPARAM>(details->second));
					return details->second->editor == nullptr;
				}, reinterpret_cast<LPARAM>(&details));
			}
			return search.editor;
		};
		HWND editor = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((editor = findEditor()) != nullptr, 5000);
		DWORD_PTR result = 0;
		QVERIFY(SendMessageTimeoutW(editor, WM_APP + 38, 0, 0, SMTO_ABORTIFHUNG, 3000, &result));
		QCOMPARE(result, DWORD_PTR{1});
		QTRY_COMPARE_WITH_TIMEOUT(gain->value<float>(), 0.625f, 5000);
		QSignalSpy refreshed(song, &lmms::Song::nodeTrackRoutingChanged);
		QVERIFY(SendMessageTimeoutW(editor, WM_APP + 39, 0, 0, SMTO_ABORTIFHUNG, 3000, &result));
		QCOMPARE(result, DWORD_PTR{1});
		QTRY_COMPARE_WITH_TIMEOUT(processor->nodeAudioIndex(1, NodePortDirection::Output), UINT32_MAX, 5000);
		QCOMPARE(processor->nodeAudioIndex(0, NodePortDirection::Output), 0u);
		QTRY_VERIFY_WITH_TIMEOUT(!refreshed.isEmpty(), 5000);
		const auto connections = song->nodeCustomConnections(scope);
		QVERIFY(std::any_of(connections.cbegin(), connections.cend(), [&](const auto& edge) {
			return edge.source == outputPort && edge.destination == destination;
		}));
	}

	void liveGuiAndAudio()
	{
		if (!m_liveGui)
		{
			QSKIP("Separate realtime suite exercises actual GUI and SDL workers");
		}
		QFETCH(QString, architecture);
		QFETCH(bool, native);
		QFETCH(bool, instrument);
		lmms::Engine::getSong()->clearProject();
		QTest::qWait(20);
		QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
		auto path = native ? QDir(qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT"))
								 .absoluteFilePath("../vst-host/" + architecture + "/Release/Vst3Native.vst3")
						   : m_fixtureRoot + '/' + architecture + "/fixtures/Release/Vst2Baseline.dll";
		lmms::Plugin::Descriptor::SubPluginFeatures::Key key;
		key.attributes["file"] = path;
		if (native)
		{
			key.attributes["format"] = "vst3";
			key.attributes["architecture"] = architecture == "x86" ? "32" : "64";
			key.attributes["classid"]
				= instrument ? "0002000154761032fedcba9824681357" : "040302f1cdab01ef1357246898765432";
			// Optional local commercial acceptance, without bundling licensed modules.
			const auto external = qEnvironmentVariable("LMMS_VST_LIVE_NATIVE_MODULE");
			if (!external.isEmpty())
			{
				QVERIFY(architecture == "x64");
				path = external;
				key.attributes["file"] = path;
				key.attributes["classid"]
					= qEnvironmentVariable(instrument ? "LMMS_VST_LIVE_INSTRUMENT_CID" : "LMMS_VST_LIVE_EFFECT_CID");
			}
		}
		QWidget* view = nullptr;
		std::unique_ptr<QWidget> effectView;
		if (instrument)
		{
			QDomDocument doc;
			auto state = doc.createElement("vestige");
			for (auto it = key.attributes.cbegin(); it != key.attributes.cend(); ++it)
			{
				state.setAttribute(it.key(), it.value());
			}
			state.setAttribute("plugin", path);
			auto* plugin = track()->loadInstrument("vestige");
			QVERIFY(plugin);
			QElapsedTimer loadTimer;
			loadTimer.start();
			plugin->restoreState(state);
			qInfo() << "entry_instrument_load_ms" << loadTimer.elapsed() << "module" << path;
			QTest::qWait(20);
			lmms::gui::InstrumentTrackView* trackView = nullptr;
			for (auto* candidate : m_liveGui->mainWindow()->findChildren<lmms::gui::InstrumentTrackView*>())
			{
				if (candidate->model() == track())
				{
					trackView = candidate;
					break;
				}
			}
			QVERIFY(trackView);
			auto* window = trackView->getInstrumentTrackWindow();
			window->show();
			for (auto* child : window->findChildren<QWidget*>())
			{
				if (auto* instrumentView = dynamic_cast<lmms::gui::InstrumentView*>(child))
				{
					view = instrumentView;
					break;
				}
			}
			QVERIFY(view);
			QVERIFY(QMetaObject::invokeMethod(view, "toggleGUI", Qt::DirectConnection));
		}
		else
		{
			QVERIFY(track()->loadInstrument("tripleoscillator"));
			auto* chain = track()->audioBusHandle()->effects();
			auto* effect = lmms::Effect::instantiate("vsteffect", chain, &key);
			QVERIFY(effect);
			QVERIFY(!effect->dontRun());
			chain->appendEffect(effect);
			effectView.reset(effect->controls()->createView());
			view = effectView.get();
			QVERIFY(view);
			QVERIFY(QMetaObject::invokeMethod(view, "togglePluginUI", Qt::DirectConnection, Q_ARG(bool, true)));
		}
		view->show();
		QTest::qWait(50);
		const auto moduleName = QFileInfo(path).fileName().toStdWString();
		const auto* module = moduleName.c_str();
		const auto children = fixtureProcesses("before realtime note", module);
		QCOMPARE(children.size(), std::size_t(1));
		const auto editorVisible = [&] {
			struct Search
			{
				const std::set<DWORD>* children;
				bool visible = false;
			} search{&children};
			const auto inspect = [](HWND window, LPARAM context) -> BOOL {
				auto& found = *reinterpret_cast<Search*>(context);
				DWORD pid = 0;
				GetWindowThreadProcessId(window, &pid);
				found.visible |= found.children->contains(pid) && IsWindowVisible(window);
				return TRUE;
			};
			EnumWindows(inspect, reinterpret_cast<LPARAM>(&search));
			EnumChildWindows(
				reinterpret_cast<HWND>(m_liveGui->mainWindow()->winId()), inspect, reinterpret_cast<LPARAM>(&search));
			EnumChildWindows(reinterpret_cast<HWND>(view->winId()), inspect, reinterpret_cast<LPARAM>(&search));
			return search.visible;
		};
		QVERIFY2(editorVisible(), "editor did not open through official GUI entry");
		const bool songPlayback
			= instrument && native && !qEnvironmentVariable("LMMS_VST_LIVE_NATIVE_MODULE").isEmpty();
		if (songPlayback)
		{
			QVERIFY(command("clip.create", {{"track", 0}, {"length", 192}}).ok);
			QVERIFY(command("midi.addNotes",
				{{"track", 0}, {"clip", 0},
					{"notes", QJsonArray{QJsonObject{{"position", 0}, {"length", 192}, {"key", 57}, {"volume", 100}}}}})
					.ok);
			QVERIFY(command("transport.playSong").ok);
		}
		else
		{
			track()->processInEvent(lmms::MidiEvent(lmms::MidiNoteOn, 0, 57, 100));
		}
		float peak = 0;
		// Observe the attack as well as the sustain; commercial presets may decay
		// before a delayed meter check even though the actual output was valid.
		for (unsigned poll = 0; poll < 40; ++poll)
		{
			QTest::qWait(15);
			auto guard = lmms::Engine::audioEngine()->requestChangesGuard();
			peak = std::max(peak, lmms::Engine::mixer()->mixerChannel(0)->m_peakLeft);
		}
		QVERIFY2(fixtureProcesses("after realtime note", module) == children, "helper vanished after realtime audio");
		QVERIFY2(editorVisible(), "editor vanished after realtime audio");
		qInfo() << "entry_realtime_peak" << peak;
		const bool emptySampler = instrument && native && !qEnvironmentVariable("LMMS_VST_LIVE_NATIVE_MODULE").isEmpty()
			&& qEnvironmentVariable("LMMS_VST_LIVE_EMPTY_SAMPLER") == "1";
		if (!emptySampler)
		{
			QVERIFY2(peak > 0.001f, "official entry is silent on actual audio worker threads");
		}
		else
		{
			qInfo() << "Empty sampler: output energy is not an acceptance requirement";
		}
		if (instrument)
		{
			QVERIFY(QMetaObject::invokeMethod(view, "toggleGUI", Qt::DirectConnection));
			QVERIFY(QMetaObject::invokeMethod(view, "toggleGUI", Qt::DirectConnection));
		}
		else
		{
			QVERIFY(QMetaObject::invokeMethod(view, "togglePluginUI", Qt::DirectConnection, Q_ARG(bool, false)));
			QVERIFY(QMetaObject::invokeMethod(view, "togglePluginUI", Qt::DirectConnection, Q_ARG(bool, true)));
		}
		QTest::qWait(100);
		QVERIFY(fixtureProcesses("after editor reopen", module) == children);
		QVERIFY2(editorVisible(), "editor failed to reopen while realtime audio was active");
		if (songPlayback)
		{
			QVERIFY(command("transport.stop").ok);
		}
		else
		{
			track()->processInEvent(lmms::MidiEvent(lmms::MidiNoteOff, 0, 57, 0));
		}
		// The instrument window owns its view. Only the standalone effect view
		// belongs to this test; deleting an instrument tab would leave its tab bar dangling.
		effectView.reset();
		lmms::Engine::getSong()->clearProject();
		QTest::qWait(20);
	}
	void nativeEffects()
	{
		QElapsedTimer phaseTimer;
		phaseTimer.start();
		const auto phase = [&](const char* name) {
			qInfo() << "native_effect_phase" << name << "elapsed_ms" << phaseTimer.restart();
		};
		auto* lifecycleDevice = lmms::Engine::audioEngine()->audioDev();
		const bool resumeLifecycleDevice = lifecycleDevice->isRunning();
		if (resumeLifecycleDevice)
		{
			lifecycleDevice->stopProcessing();
		}
		const auto resumeLifecycle = qScopeGuard([&] {
			if (resumeLifecycleDevice)
			{
				lifecycleDevice->startProcessing();
			}
		});
		const auto nativeRoot = m_fixtureRoot + "/native";
		for (const auto* architecture : {"x86", "x64"})
		{
			QVERIFY(QDir().mkpath(nativeRoot + '/' + architecture));
			const auto source
				= QDir(qEnvironmentVariable("LMMS_PLUGIN_DIR"))
					  .absoluteFilePath(QString("../vst-host/") + architecture + "/Release/Vst3Native.vst3");
			QVERIFY(QFile::copy(source, nativeRoot + '/' + architecture + "/Vst3Native.vst3"));
		}
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{nativeRoot, {"vst3"}}}));
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 10000);
		lmms::Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		const auto* descriptor = lmms::PluginFactory::instance()->pluginInfo("vsteffect").descriptor;
		descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
		QCOMPARE(keys.size(), 2);
		lmms::Plugin::Descriptor::SubPluginFeatures::KeyList instrumentKeys;
		const auto* instrumentDescriptor = lmms::PluginFactory::instance()->pluginInfo("vestige").descriptor;
		instrumentDescriptor->subPluginFeatures->listSubPluginKeys(instrumentDescriptor, instrumentKeys);
		QCOMPARE(instrumentKeys.size(), 2);
		phase("catalog");
		for (auto key : keys)
		{
			QCOMPARE(key.attributes.value("format"), QString("vst3"));
			QVERIFY(!key.attributes.value("binarypath").isEmpty());
			QCOMPARE(key.attributes.value("fingerprint").size(), 64);
			lmms::Engine::getSong()->clearProject();
			lmms::Engine::projectJournal()->clearJournal();
			QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
			for (const auto& damaged : {QString(), QString("not-hex"), QString(64, 'g'),
					 key.attributes.value("fingerprint").left(63), key.attributes.value("fingerprint") + "0"})
			{
				auto badKey = key;
				badKey.attributes["fingerprint"] = damaged;
				std::unique_ptr<lmms::Effect> rejected(
					lmms::Effect::instantiate("vsteffect", track()->audioBusHandle()->effects(), &badKey));
				QVERIFY(rejected);
				QVERIFY(rejected->dontRun());
				QVERIFY(
					fixtureProcesses("malformed effect fingerprint creates no instance", L"Vst3Native.vst3").empty());
			}
			QJsonObject attributes;
			for (auto it = key.attributes.begin(); it != key.attributes.end(); ++it)
			{
				attributes.insert(it.key(), it.value());
			}
			const QJsonObject subKey{{"name", key.name}, {"attributes", attributes}};
			{
				// Stop the device only for the explicitly driven offline block sequence.
				auto* device = lmms::Engine::audioEngine()->audioDev();
				const bool wasRunning = device->isRunning();
				device->stopProcessing();
				const auto resumeAudio = qScopeGuard([device, wasRunning] {
					if (wasRunning)
					{
						device->startProcessing();
					}
				});
				QVERIFY(!device->isRunning());
				qputenv("LMMS_VST3_FIXTURE_VERIFY_BLOCKS", "1");
				QVERIFY(command("effect.add", {{"owner", "track:0"}, {"plugin", "vsteffect"}, {"subKey", subKey}}).ok);
				qunsetenv("LMMS_VST3_FIXTURE_VERIFY_BLOCKS");
				QCOMPARE(fixtureProcesses("native effect.add", L"Vst3Native.vst3").size(), std::size_t(1));
				auto* nativeEffect = track()->audioBusHandle()->effects()->effectAt(0);
				const auto models = nativeEffect->controls()->parameterModels();
				auto* gain = dynamic_cast<lmms::FloatModel*>(models.value("vst3param_f0000101"));
				QVERIFY(gain);
				const auto period = lmms::Engine::audioEngine()->framesPerPeriod();
				constexpr std::array<float, 5> wetLevels{0.0f, 0.5f, 1.0f, 0.5f, 1.0f};
				unsigned mixIndex = 0;
				for (const auto count : {lmms::f_cnt_t(1), lmms::f_cnt_t(17), lmms::f_cnt_t(65), period - 1, period})
				{
					const auto wet = wetLevels[mixIndex++];
					nativeEffect->wetDryModel()->saveJournallingState(false);
					nativeEffect->wetDryModel()->setValue(wet);
					nativeEffect->wetDryModel()->restoreJournallingState();
					const auto mixedGain = wet * gain->value() + (1 - wet);
					lmms::AudioBuffer buffer(count, 2);
					buffer.allocateInterleavedBuffer();
					std::fill(buffer.buffer(0).begin(), buffer.buffer(0).end(), 1.0f);
					std::fill(buffer.buffer(1).begin(), buffer.buffer(1).end(), -0.5f);
					std::fill_n(
						buffer.interleavedBuffer().asSampleFrames().data(), count, lmms::SampleFrame(1.0f, -0.5f));
					buffer.updateSilenceFlags(0b11);
					QVERIFY(nativeEffect->processAudioBuffer(buffer));
					for (unsigned frame = 0; frame < count; ++frame)
					{
						QCOMPARE(buffer.buffer(0)[frame], mixedGain);
						QCOMPARE(buffer.buffer(1)[frame], -0.5f * mixedGain);
					}
				}
				QVERIFY(command("history.undo").ok);
				QVERIFY(fixtureProcesses("native effect undo", L"Vst3Native.vst3").empty());
			}
			phase("effect offline and undo");
			QVERIFY(command("history.redo").ok);
			QCOMPARE(fixtureProcesses("native effect redo", L"Vst3Native.vst3").size(), std::size_t(1));
			const QString gainKey("vst3param_f0000101");
			const QString bypassKey("vst3param_0000002a");
			auto* effectControls = track()->audioBusHandle()->effects()->effectAt(0)->controls();
			auto effectModels = effectControls->parameterModels();
			QCOMPARE(effectModels.size(), 2);
			QVERIFY(effectModels.contains(gainKey));
			QVERIFY(effectModels.contains(bypassKey));
			auto* effectAutomation = new lmms::AutomationClip(dynamic_cast<lmms::AutomationTrack*>(
				lmms::Track::create(lmms::Track::Type::Automation, lmms::Engine::getSong())));
			QVERIFY(effectAutomation->addObject(effectModels.value(gainKey)));
			effectAutomation->putValue(lmms::TimePos(0), 0.375f, false);
			effectAutomation->putValue(lmms::TimePos(192), 0.875f, false);
			QVERIFY(effectModels.value(gainKey)->isAutomated());
			QVERIFY(!effectModels.value(bypassKey)->isAutomated());
			QTemporaryDir temporary;
			QVERIFY(temporary.isValid());
			const auto project = temporary.filePath("native.mmp");
			QVERIFY(lmms::Engine::getSong()->saveProjectFile(project));
			QFile saved(project);
			QVERIFY(saved.open(QIODevice::ReadOnly));
			const auto xml = saved.readAll();
			QVERIFY(xml.contains("vst3state=") && xml.contains(key.attributes.value("classid").toUtf8()));
			QVERIFY(xml.contains("<automationclip "));
			QVERIFY(xml.contains("<vst3param_f0000101 "));
			QVERIFY(!xml.contains("<param0 "));
			lmms::Engine::getSong()->clearProject();
			QVERIFY(fixtureProcesses("native project clear", L"Vst3Native.vst3").empty());
			qputenv("LMMS_VST3_FIXTURE_REVERSE_PARAMS", "1");
			lmms::Engine::getSong()->loadProject(project);
			qunsetenv("LMMS_VST3_FIXTURE_REVERSE_PARAMS");
			QCOMPARE(fixtureProcesses("native effect project reload", L"Vst3Native.vst3").size(), std::size_t(1));
			effectModels = track()->audioBusHandle()->effects()->effectAt(0)->controls()->parameterModels();
			QCOMPARE(effectModels.value(gainKey)->displayName(), QString("1"));
			QCOMPARE(effectModels.value(bypassKey)->displayName(), QString("0"));
			QVERIFY(effectModels.value(gainKey)->isAutomated());
			QVERIFY(!effectModels.value(bypassKey)->isAutomated());
			const auto effectClips = lmms::AutomationClip::clipsForModel(effectModels.value(gainKey));
			QCOMPARE(effectClips.size(), std::size_t(1));
			effectAutomation = effectClips.front();
			QCOMPARE(effectAutomation->firstObject(), effectModels.value(gainKey));
			QCOMPARE(effectAutomation->getTimeMap().size(), 2);
			QCOMPARE(effectAutomation->valueAt(lmms::TimePos(192)), 0.875f);
			lmms::Engine::getSong()->clearProject();
			QVERIFY(fixtureProcesses("native cleanup", L"Vst3Native.vst3").empty());
			phase("effect automation project reload");
			const auto instrumentChoice
				= std::find_if(instrumentKeys.begin(), instrumentKeys.end(), [&](const auto& candidate) {
					  return candidate.attributes.value("architecture") == key.attributes.value("architecture");
				  });
			QVERIFY(instrumentChoice != instrumentKeys.end());
			key = *instrumentChoice;
			QVERIFY(command("track.create", {{"type", "instrument"}}).ok);

			{
				auto instrumentKey = key;
				instrumentKey.desc = lmms::PluginFactory::instance()->pluginInfo("vestige").descriptor;
				auto* instrument = track()->loadInstrument("vestige", &instrumentKey);
				QVERIFY(instrument);
				QDomDocument selected;
				auto parent = selected.createElement("root");
				selected.appendChild(parent);
				const auto saved = instrument->saveState(selected, parent);
				QCOMPARE(saved.attribute("format"), QString("vst3"));
				QCOMPARE(saved.attribute("classid"), key.attributes.value("classid"));
				QCOMPARE(fixtureProcesses("sidebar identity delegates to VeSTige", L"Vst3Native.vst3").size(),
					std::size_t(1));
				lmms::Engine::getSong()->clearProject();
				QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
			}
			const QJsonObject nativeArguments{{"track", 0}, {"plugin", "vestige"},
				{"path", key.attributes.value("file")}, {"classid", key.attributes.value("classid").toUpper()},
				{"architecture", key.attributes.value("architecture")}};
			QVERIFY(command("instrument.load", nativeArguments).ok);
			QCOMPARE(fixtureProcesses("native command load", L"Vst3Native.vst3").size(), std::size_t(1));
			QVERIFY(command("history.undo").ok);
			QVERIFY(fixtureProcesses("native command undo", L"Vst3Native.vst3").empty());
			QVERIFY(command("history.redo").ok);
			const auto nativePeers = fixtureProcesses("native command redo", L"Vst3Native.vst3");
			QCOMPARE(nativePeers.size(), std::size_t(1));
			for (const auto* invalid :
				{"missing", "bad-cid", "unknown-cid", "wrong-architecture", "malformed-architecture"})
			{
				auto arguments = nativeArguments;
				const QString fault(invalid);
				if (fault == "missing")
				{
					arguments.remove("classid");
					arguments.remove("architecture");
				}
				else if (fault == "bad-cid")
				{
					arguments.insert("classid", "not-a-cid");
				}
				else if (fault == "unknown-cid")
				{
					arguments.insert("classid", QString(32, '0'));
				}
				else if (fault == "wrong-architecture")
				{
					arguments.insert("architecture", key.attributes.value("architecture") == "32" ? "64" : "32");
				}
				else
				{
					arguments.insert("architecture", "x64");
				}
				const auto rejected = command("instrument.load", arguments);
				QVERIFY(!rejected.ok);
				QCOMPARE(rejected.errorCode, QString("invalid_arguments"));
				QVERIFY(fixtureProcesses("invalid native selection retains active instance", L"Vst3Native.vst3")
					== nativePeers);
			}

			phase("instrument commands");
			QDomDocument preset;
			auto state = preset.createElement("vestige");
			preset.appendChild(state);
			for (auto it = key.attributes.begin(); it != key.attributes.end(); ++it)
			{
				state.setAttribute(it.key(), it.value());
			}
			state.setAttribute("plugin", key.attributes.value("file"));
			state.setAttribute("pluginname", key.name);
			for (const auto& damaged : {QString(), QString("not-hex"), QString(64, 'g'),
					 key.attributes.value("fingerprint").left(63), key.attributes.value("fingerprint") + "0"})
			{
				auto badState = state.cloneNode(true).toElement();
				badState.setAttribute("fingerprint", damaged);
				track()->instrument()->restoreState(badState);
				QVERIFY(fixtureProcesses("malformed instrument fingerprint retains active helper", L"Vst3Native.vst3")
					== nativePeers);
			}
			track()->instrument()->restoreState(state);
			phase("instrument class restore");
			QCOMPARE(fixtureProcesses("native instrument class restore", L"Vst3Native.vst3").size(), std::size_t(1));
			auto savedState = track()->instrument()->saveState(preset, state);
			QCOMPARE(savedState.attribute("classid"), key.attributes.value("classid"));
			QCOMPARE(savedState.attribute("architecture"), key.attributes.value("architecture"));
			QCOMPARE(savedState.attribute("pluginname"), key.name);
			QCOMPARE(savedState.attribute("vendor"), key.attributes.value("vendor"));
			QVERIFY(savedState.hasAttribute("vst3state"));
			QVERIFY(!savedState.hasAttribute("chunk"));
			phase("instrument save state");
			QVERIFY(command("track.clone", {{"track", 0}}).ok);
			phase("clone command");
			QCOMPARE(fixtureProcesses("native instrument clone", L"Vst3Native.vst3").size(), std::size_t(2));
			auto* cloneTrack = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks().at(1));
			QVERIFY(cloneTrack);
			phase("clone identities");
			QVERIFY(command("history.undo").ok);
			QCOMPARE(fixtureProcesses("native instrument undo clone", L"Vst3Native.vst3").size(), std::size_t(1));
			phase("clone undo");
			QVERIFY(command("history.redo").ok);
			QCOMPARE(fixtureProcesses("native instrument redo clone", L"Vst3Native.vst3").size(), std::size_t(2));
			cloneTrack = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks().at(1));
			QVERIFY(cloneTrack);
			phase("clone redo");
			phase("instrument restore clone undo redo");
			const auto instrumentModels = track()->instrument()->parameterModels();
			QCOMPARE(instrumentModels.size(), 2);
			QVERIFY(instrumentModels.contains(gainKey));
			QVERIFY(instrumentModels.contains(bypassKey));
			auto* instrumentAutomation = new lmms::AutomationClip(dynamic_cast<lmms::AutomationTrack*>(
				lmms::Track::create(lmms::Track::Type::Automation, lmms::Engine::getSong())));
			QVERIFY(instrumentAutomation->addObject(instrumentModels.value(gainKey)));
			instrumentAutomation->putValue(lmms::TimePos(0), 0.625f, false);
			instrumentAutomation->putValue(lmms::TimePos(192), 0.25f, false);
			const auto instrumentProject = temporary.filePath("native-instrument.mmp");
			QVERIFY(instrumentModels.value(gainKey)->isAutomated());
			QVERIFY(lmms::Engine::getSong()->saveProjectFile(instrumentProject));
			lmms::Engine::getSong()->clearProject();
			QVERIFY(fixtureProcesses("native instrument project clear", L"Vst3Native.vst3").empty());
			qputenv("LMMS_VST3_FIXTURE_REVERSE_PARAMS", "1");
			lmms::Engine::getSong()->loadProject(instrumentProject);
			qunsetenv("LMMS_VST3_FIXTURE_REVERSE_PARAMS");
			QCOMPARE(fixtureProcesses("native instrument project reload", L"Vst3Native.vst3").size(), std::size_t(2));
			const auto reopenedModels = track()->instrument()->parameterModels();
			QCOMPARE(reopenedModels.value(gainKey)->displayName(), QString("1"));
			QCOMPARE(reopenedModels.value(bypassKey)->displayName(), QString("0"));
			QVERIFY(reopenedModels.value(gainKey)->isAutomated());
			QVERIFY(!reopenedModels.value(bypassKey)->isAutomated());
			const auto instrumentClips = lmms::AutomationClip::clipsForModel(reopenedModels.value(gainKey));
			QCOMPARE(instrumentClips.size(), std::size_t(1));
			instrumentAutomation = instrumentClips.front();
			QCOMPARE(instrumentAutomation->firstObject(), reopenedModels.value(gainKey));
			QCOMPARE(instrumentAutomation->getTimeMap().size(), 2);
			QCOMPARE(instrumentAutomation->valueAt(lmms::TimePos(192)), 0.25f);
			const auto reopenedState = track()->instrument()->saveState(preset, state);
			QVERIFY(!reopenedState.firstChildElement(gainKey).isNull());
			QVERIFY(reopenedState.firstChildElement("param0").isNull());
			QCOMPARE(reopenedState.attribute("pluginname"), key.name);
			QCOMPARE(reopenedState.attribute("vendor"), key.attributes.value("vendor"));
			lmms::Engine::getSong()->clearProject();
			QVERIFY(fixtureProcesses("native instrument cleanup", L"Vst3Native.vst3").empty());
			phase("instrument automation project reload");
		}
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 10000);
	}
	void nativeBundleLocator()
	{
		for (const auto* architecture : {"x86", "x64"})
		{
			lmms::Engine::getSong()->clearProject();
			lmms::Engine::projectJournal()->clearJournal();
			const auto bundle = m_fixtureRoot + "/bundles/" + architecture + "/Vst3Native.vst3";
			const auto contents = bundle + "/Contents/";
			const auto binaryDirectory = contents + (QString(architecture) == "x86" ? "x86-win" : "x86_64-win");
			QVERIFY(QDir().mkpath(binaryDirectory));
			QVERIFY(QDir().mkpath(contents + "Resources"));
			const auto source
				= QDir(qEnvironmentVariable("LMMS_PLUGIN_DIR"))
					  .absoluteFilePath(QString("../vst-host/") + architecture + "/Release/Vst3Native.vst3");
			QVERIFY(QFile::copy(source, binaryDirectory + "/Vst3Native.vst3"));
			// Exercise the real Open button for ordinary module files and bundles.
			QString pickedBundle;
			for (const auto& requested : {source, bundle})
			{
				lmms::gui::FileDialog chooser(
					nullptr, "Open VST plugin", QFileInfo(bundle).absolutePath(), "VST files (*.dll *.vst3)");
				chooser.setAcceptedDirectorySuffix("vst3");
				chooser.selectFile(requested);
				chooser.show();
				QVERIFY(QTest::qWaitForWindowExposed(&chooser));
				auto* list = chooser.findChild<QListView*>("listView");
				QVERIFY(list);
				auto* filesystem = qobject_cast<QFileSystemModel*>(list->model());
				QVERIFY(filesystem);
				const auto index = filesystem->index(requested);
				QVERIFY(index.isValid());
				QTRY_VERIFY_WITH_TIMEOUT(!list->visualRect(index).isEmpty(), 3000);
				list->clearSelection();
				auto* filename = chooser.findChild<QLineEdit*>("fileNameEdit");
				QVERIFY(filename);
				filename->clear();
				QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualRect(index).center());
				QTRY_COMPARE_WITH_TIMEOUT(lmms::vsthost::scanPathKey(chooser.selectedFiles().value(0)),
					lmms::vsthost::scanPathKey(requested), 3000);
				auto* buttons = chooser.findChild<QDialogButtonBox*>();
				QVERIFY(buttons);
				auto* open = buttons->button(QDialogButtonBox::Open);
				QVERIFY(open);
				QVERIFY(open->isEnabled());
				QTest::mouseClick(open, Qt::LeftButton);
				QCOMPARE(chooser.result(), int(QDialog::Accepted));
				QCOMPARE(chooser.selectedFiles().size(), 1);
				QCOMPARE(
					lmms::vsthost::scanPathKey(chooser.selectedFiles().front()), lmms::vsthost::scanPathKey(requested));
				if (requested == bundle)
				{
					pickedBundle = chooser.selectedFiles().front();
				}
			}
			QFile resource(contents + "Resources/revision.txt");
			QVERIFY(resource.open(QIODevice::WriteOnly));
			QCOMPARE(resource.write("before"), qint64(6));
			resource.close();
			QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{bundle, {"vst3"}}}));
			QVERIFY(lmms::Engine::refreshVstCatalog());
			QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 10000);
			const auto initial = lmms::Engine::vstCatalog()->snapshot().report;
			QVERIFY(initial);
			QCOMPARE(initial->entries.size(), std::size_t(2));
			QVERIFY(initial->failures.empty());
			const auto& entry = initial->entries.front();
			const QJsonObject arguments{{"track", 0}, {"plugin", "vestige"}, {"path", pickedBundle},
				{"classid",
					QString::fromLatin1(
						QByteArray(reinterpret_cast<const char*>(entry.identity.cid.data()), 16).toHex())},
				{"architecture", QString(architecture) == "x86" ? "32" : "64"}};
			QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
			QVERIFY(command("instrument.load", arguments).ok);
			const auto active = fixtureProcesses("native bundle command load", L"Vst3Native.vst3");
			QCOMPARE(active.size(), std::size_t(1));
			QVERIFY(resource.open(QIODevice::WriteOnly | QIODevice::Truncate));
			QCOMPARE(resource.write("after"), qint64(5));
			resource.close();
			const auto rejected = command("instrument.load", arguments);
			QVERIFY(!rejected.ok);
			QCOMPARE(rejected.errorCode, QString("invalid_arguments"));
			QVERIFY(rejected.errorMessage.contains("resources have changed"));
			QVERIFY(fixtureProcesses("stale bundle preflight retains helper", L"Vst3Native.vst3") == active);
			QVERIFY(lmms::Engine::refreshVstCatalog(nullptr, true));
			QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 10000);
			const auto refreshed = lmms::Engine::vstCatalog()->snapshot().report;
			QVERIFY(refreshed);
			QCOMPARE(refreshed->entries.size(), std::size_t(2));
			QVERIFY(refreshed->failures.empty());
			QVERIFY(refreshed->entries.front().locator.fingerprint != entry.locator.fingerprint);
			QVERIFY(command("instrument.load", arguments).ok);
			const auto replaced = fixtureProcesses("refreshed bundle command load", L"Vst3Native.vst3");
			QCOMPARE(replaced.size(), std::size_t(1));
			QVERIFY(replaced != active);
			lmms::Engine::getSong()->clearProject();
			QVERIFY(fixtureProcesses("native bundle cleanup", L"Vst3Native.vst3").empty());
		}
		QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
		QVERIFY(lmms::Engine::refreshVstCatalog());
		QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 10000);
	}
	void cleanupTestCase()
	{
		if (m_liveGui)
		{
			if (m_editorFunctionOnly)
			{
				lmms::Engine::audioEngine()->audioDev()->stopProcessing();
				lmms::Engine::getSong()->setModified(false);
			}
			delete static_cast<QWidget*>(m_liveGui->mainWindow());
			m_liveGui.reset();
		}
		else
		{
			lmms::Engine::destroy();
		}
		if (!m_editorFunctionOnly)
		{
			QVERIFY(lmms::Engine::vstCatalog() == nullptr);
			QVERIFY(fixtureProcesses("DAW shutdown").empty());
		}
		lmms::NotePlayHandleManager::free();
	}
	void entries_data()
	{
		QTest::addColumn<QString>("architecture");
		QTest::newRow("x86") << "x86";
		QTest::newRow("x64") << "x64";
	}
	void entries()
	{
		QFETCH(QString, architecture);
		m_fixture = QDir(m_fixtureRoot).absoluteFilePath(architecture + "/fixtures/Release/Vst2Baseline.dll");
		QVERIFY(QFile::exists(m_fixture));
		lmms::Engine::getSong()->clearProject();
		lmms::Engine::projectJournal()->clearJournal();
		QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(command("instrument.load", {{"track", 0}, {"plugin", "vestige"}, {"path", m_fixture}}).ok);
		QVERIFY(fixtureProcesses("MCP instrument.load").size() == 1);
		QCOMPARE(QString(track()->instrument()->descriptor()->name), QString("vestige"));
		QVERIFY(command("history.undo").ok);
		QVERIFY(fixtureProcesses("snapshot undo instrument").empty());
		QVERIFY(command("history.redo").ok);
		QVERIFY(fixtureProcesses("snapshot redo instrument").size() == 1);

		// Browser registry selection and file-loader seam, used by file dialogs,
		// context loading, drag/drop and instrument window drops.
		const auto browser = lmms::PluginFactory::instance()->pluginSupportingExtension("dll");
		QVERIFY(!browser.info.isNull());
		QCOMPARE(QString(browser.info.descriptor->name), QString("vestige"));
		track()->instrument()->loadFile(m_fixture);
		QVERIFY(fixtureProcesses("browser/drag file loader and reload").size() == 1);

		QDomDocument preset;
		auto root = preset.createElement("preset");
		preset.appendChild(root);
		auto state = track()->instrument()->saveState(preset, root);
		QVERIFY(state.hasAttribute("plugin"));
		QVERIFY(state.hasAttribute("chunk"));
		const auto relative = lmms::PathUtil::toShortestRelative(m_fixture);
		QVERIFY(relative.startsWith("uservst:"));
		state.setAttribute("plugin", relative);
		track()->instrument()->restoreState(state);
		QVERIFY(fixtureProcesses("preset restore and uservst legacy path").size() == 1);
		QVERIFY(command("track.clone", {{"track", 0}, {"name", "clone"}}).ok);
		QVERIFY(fixtureProcesses("track clone").size() == 2);
		QVERIFY(command("history.undo").ok);
		QVERIFY(fixtureProcesses("snapshot undo clone").size() == 1);
		QVERIFY(command("history.redo").ok);
		QVERIFY(fixtureProcesses("snapshot redo clone").size() == 2);
		const auto playingHelpers = fixtureProcesses("before catalog refresh");
		QVERIFY(lmms::Engine::refreshVstCatalog(nullptr, true));
		QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 10000);
		QVERIFY(fixtureProcesses("live instances survive catalog refresh") == playingHelpers);

		lmms::Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		const auto* descriptor = lmms::PluginFactory::instance()->pluginInfo("vsteffect").descriptor;
		descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
		qInfo() << "effect registry root" << lmms::ConfigManager::inst()->vstDir() << "keys" << keys.size();
		int shellEntries = 0;
		for (const auto& key : keys)
		{
			if (!key.attributes.value("file").endsWith("Vst2Shell.dll"))
			{
				continue;
			}
			++shellEntries;
			const auto id = key.attributes.value("shellid").toUInt();
			QVERIFY(id == UINT32_C(0xf1020304) || id == UINT32_C(0x01000200));
			QCOMPARE(key.name, id == UINT32_C(0xf1020304) ? QString("Shell Alpha") : QString("Shell Beta"));
		}
		QCOMPARE(shellEntries, 4);
		QJsonObject subKey;
		for (const auto& key : keys)
		{
			if (QDir::fromNativeSeparators(QDir(m_fixtureRoot).absoluteFilePath(key.attributes.value("file")))
				!= m_fixture)
			{
				continue;
			}
			QJsonObject attributes;
			for (auto it = key.attributes.begin(); it != key.attributes.end(); ++it)
			{
				attributes.insert(it.key(), it.value());
			}
			subKey = {{"name", key.name}, {"attributes", attributes}};
		}
		QVERIFY(!subKey.isEmpty());
		QVERIFY(command("effect.add", {{"owner", "track:0"}, {"plugin", "vsteffect"}, {"subKey", subKey}}).ok);
		QVERIFY(fixtureProcesses("MCP effect.add / effect selection instantiate").size() == 3);
		QCOMPARE(command("query.trackDetail", {{"track", 0}}).data.value("effects").toArray().size(), 1);
		QVERIFY(command("history.undo").ok);
		QVERIFY(fixtureProcesses("snapshot undo effect").size() == 2);
		QVERIFY(command("history.redo").ok);
		QVERIFY(fixtureProcesses("snapshot redo effect").size() == 3);

		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const auto project = temporary.filePath("legacy.mmp");
		QVERIFY(lmms::Engine::getSong()->saveProjectFile(project));
		lmms::Engine::getSong()->clearProject();
		QVERIFY(fixtureProcesses("project clear").empty());
		lmms::Engine::getSong()->loadProject(project);
		QCOMPARE(lmms::Engine::getSong()->tracks().size(), std::size_t{2});
		QVERIFY(fixtureProcesses("project open / instrument and effect state restore").size() == 3);
		lmms::Engine::getSong()->clearProject();
		QVERIFY(fixtureProcesses("project cleanup").empty());

		QFile oldProject(QFINDTESTDATA("legacy-project.mmp.in"));
		QVERIFY(oldProject.open(QIODevice::ReadOnly));
		auto contents = oldProject.readAll();
		contents.replace("@ARCH@", architecture.toUtf8());
		QFile frozenProject(temporary.filePath("frozen.mmp"));
		QVERIFY(frozenProject.open(QIODevice::WriteOnly));
		QCOMPARE(frozenProject.write(contents), qint64(contents.size()));
		frozenProject.close();
		lmms::Engine::getSong()->loadProject(frozenProject.fileName());
		QCOMPARE(lmms::Engine::getSong()->tracks().size(), std::size_t{1});
		QVERIFY(fixtureProcesses("frozen legacy project / uservst parameter state").size() == 1);
		const auto output = temporary.filePath("frozen.wav");
		const auto render
			= command("export.audio", {{"path", output}, {"range", QJsonObject{{"start", 0}, {"end", 192}}}});
		QVERIFY(render.ok);
		QVERIFY(fixtureProcesses("offline audio export").size() == 1);
		QElapsedTimer deadline;
		deadline.start();
		QString status;
		do
		{
			QCoreApplication::processEvents();
			const auto result = command("export.status", {{"task", render.data.value("task")}});
			QVERIFY(result.ok);
			status = result.data.value("status").toString();
			if (status == "completed" || status == "failed" || status == "cancelled")
			{
				break;
			}
			QTest::qWait(10);
		} while (deadline.elapsed() < 30000);
		QCOMPARE(status, QString("completed"));
		QFile wave(output);
		QVERIFY(wave.open(QIODevice::ReadOnly));
		const auto audio = wave.readAll();
		QVERIFY(audio.startsWith("RIFF") && audio.mid(8, 4) == "WAVE");
		const auto data = audio.indexOf("data", 12);
		QVERIFY(data >= 12 && audio.size() > data + 8 + 44100 * 4);
		bool nonzero = false;
		for (qsizetype i = data + 8; i < audio.size(); ++i)
		{
			if (audio[i] != 0)
			{
				nonzero = true;
				break;
			}
		}
		QVERIFY(nonzero);
		lmms::Engine::getSong()->clearProject();
		QVERIFY(fixtureProcesses("offline cleanup").empty());
	}
	void shellState_data() { entries_data(); }
	void shellState()
	{
		QFETCH(QString, architecture);
		const auto relative = architecture + "/fixtures/Release/Vst2Shell.dll";
		const auto path = QDir(m_fixtureRoot).absoluteFilePath(relative);
		for (const auto id : {UINT32_C(0xf1020304), UINT32_C(0x01000200)})
		{
			lmms::Engine::getSong()->clearProject();
			QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
			QVERIFY(command("instrument.load", {{"track", 0}, {"plugin", "vestige"}}).ok);
			QDomDocument preset;
			auto state = preset.createElement("vestige");
			preset.appendChild(state);
			state.setAttribute("plugin", path);
			state.setAttribute("shellid", QString::number(id));
			track()->instrument()->restoreState(state);
			QVERIFY(fixtureProcesses("selected shell instrument", L"Vst2Shell.dll").size() == 1);
			QVERIFY(command("track.clone", {{"track", 0}}).ok);
			QVERIFY(fixtureProcesses("selected shell clone", L"Vst2Shell.dll").size() == 2);
			lmms::Plugin::Descriptor::SubPluginFeatures::KeyList keys;
			const auto* descriptor = lmms::PluginFactory::instance()->pluginInfo("vsteffect").descriptor;
			descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
			QJsonObject subKey;
			for (const auto& key : keys)
			{
				if (QDir::fromNativeSeparators(key.attributes.value("file")) != path
					|| key.attributes.value("shellid").toUInt() != id)
				{
					continue;
				}
				QJsonObject attributes;
				for (auto it = key.attributes.begin(); it != key.attributes.end(); ++it)
				{
					attributes.insert(it.key(), it.value());
				}
				subKey = {{"name", key.name}, {"attributes", attributes}};
			}
			QVERIFY(!subKey.isEmpty());
			QVERIFY(command("effect.add", {{"owner", "track:0"}, {"plugin", "vsteffect"}, {"subKey", subKey}}).ok);
			QVERIFY(fixtureProcesses("selected shell effect", L"Vst2Shell.dll").size() == 3);
			QVERIFY(command("history.undo").ok);
			QVERIFY(fixtureProcesses("shell effect undo", L"Vst2Shell.dll").size() == 2);
			QVERIFY(command("history.redo").ok);
			QVERIFY(fixtureProcesses("shell effect redo", L"Vst2Shell.dll").size() == 3);
			QTemporaryDir temporary;
			QVERIFY(temporary.isValid());
			const auto project = temporary.filePath("shell.mmp");
			QVERIFY(lmms::Engine::getSong()->saveProjectFile(project));
			QFile file(project);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QVERIFY(file.readAll().contains("shellid=\"" + QByteArray::number(id) + "\""));
			file.close();
			lmms::Engine::getSong()->clearProject();
			QVERIFY(fixtureProcesses("shell project cleared", L"Vst2Shell.dll").empty());
			lmms::Engine::getSong()->loadProject(project);
			QCOMPARE(lmms::Engine::getSong()->tracks().size(), std::size_t(2));
			QVERIFY(fixtureProcesses("shell project reopened", L"Vst2Shell.dll").size() == 3);
			for (auto* loaded : lmms::Engine::getSong()->tracks())
			{
				auto* instrument = dynamic_cast<lmms::InstrumentTrack*>(loaded);
				QVERIFY(instrument);
				QDomDocument saved;
				auto root = saved.createElement("preset");
				saved.appendChild(root);
				const auto actual = instrument->instrument()->saveState(saved, root);
				QCOMPARE(actual.attribute("shellid").toUInt(), id);
			}
			lmms::Engine::getSong()->clearProject();
			QVERIFY(fixtureProcesses("shell project cleanup", L"Vst2Shell.dll").empty());
		}
	}
	void previews_data() { entries_data(); }
	void previews()
	{
		QFETCH(QString, architecture);
		m_fixture = QDir(m_fixtureRoot).absoluteFilePath(architecture + "/fixtures/Release/Vst2Baseline.dll");
		QVERIFY(command("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(command("instrument.load", {{"track", 0}, {"plugin", "vestige"}, {"path", m_fixture}}).ok);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		// Drive the browser's actual keyboard preview route with a saved VST preset.
		lmms::DataFile previewPreset(lmms::DataFile::Type::InstrumentTrackSettings);
		track()->savePreset(previewPreset, previewPreset.content());
		QVERIFY(previewPreset.writeFile(temporary.filePath("preview.xpf")));
		lmms::PresetPreviewPlayHandle::init();
		{
			lmms::gui::FileBrowserTreeWidget browserTree(nullptr);
			auto* item = new lmms::gui::FileItem(&browserTree, "preview.xpf", temporary.path());
			browserTree.setCurrentItem(item);
			QTest::keyClick(&browserTree, Qt::Key_Space);
			QVERIFY(fixtureProcesses("browser keyboard preset preview").size() == 2);
			// Navigation stops the preview without launching another one.
			QTest::keyClick(&browserTree, Qt::Key_Left);
		}
		// The application retains the global preview track until Engine::destroy.
		// Loading the next preview replaces its plugin on that same track.
		lmms::Engine::getSong()->clearProject();
		QVERIFY(fixtureProcesses("browser preview retained track").size() == 1);
	}
};
QTEST_MAIN(VstEntryPoints)
#include "VstEntryPoints.moc"
