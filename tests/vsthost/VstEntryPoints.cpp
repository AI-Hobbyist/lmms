#include <QtTest>
#include <QDir>
#include <QDomDocument>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QElapsedTimer>
#include <windows.h>
#include <tlhelp32.h>
#include <set>
#include "agent/CommandBus.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "PathUtil.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "EffectChain.h"
#include "DataFile.h"
#include "FileBrowser.h"
#include "PresetPreviewPlayHandle.h"
#include "AudioEngine.h"
#include "AudioDevice.h"

// Native LMMS DLLs import lmms.exe. This test's output name is lmms.exe so
// imports resolve to the same Engine/CommandBus as the test, not a second copy.
#include "vsthost/CatalogJobs.h"
#include "EffectSelectDialog.h"
#include <QTableView>
#include <QLineEdit>
#include <QStandardPaths>

class VstEntryPoints : public QObject
{
	Q_OBJECT
	QString m_fixtureRoot;
	QString m_fixture;
	QTemporaryDir m_fixtureDirectory;
	std::set<DWORD> fixtureProcesses(const char* entry, const wchar_t* moduleName = L"Vst2Baseline.dll")
	{
		std::set<DWORD> result;
		if (GetModuleHandleW(moduleName))
		{
			QTest::qFail("Third-party fixture was loaded in the DAW process", __FILE__, __LINE__);
			return result;
		}
		HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (processes == INVALID_HANDLE_VALUE) { return result; }
		PROCESSENTRY32W process{}; process.dwSize = sizeof(process);
		if (Process32FirstW(processes, &process)) do
		{
			if (process.th32ParentProcessID != GetCurrentProcessId()) { continue; }
			HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process.th32ProcessID);
			if (modules == INVALID_HANDLE_VALUE) { continue; }
			MODULEENTRY32W module{}; module.dwSize = sizeof(module);
			if (Module32FirstW(modules, &module)) do
			{
				if (_wcsicmp(module.szModule, moduleName) == 0)
				{
					result.insert(process.th32ProcessID);
					qInfo() << "entry" << entry << "DAW PID" << GetCurrentProcessId()
						<< "helper PID" << process.th32ProcessID << "helper" << QString::fromWCharArray(process.szExeFile)
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
		auto result = lmms::agent::CommandBus::instance().execute(name, arguments);
		if (!result.ok) { qWarning() << name << result.errorCode << result.errorMessage; }
		return result;
	}
	lmms::InstrumentTrack* track()
	{
		return dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks().at(0));
	}
private slots:
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
		lmms::Engine::init(true);
		lmms::ConfigManager::inst()->setVSTDir(m_fixtureRoot + '/');
		QVERIFY(!lmms::PluginFactory::instance()->pluginInfo("vestige").isNull());
			QVERIFY(!lmms::PluginFactory::instance()->pluginInfo("vsteffect").isNull());
			QVERIFY(lmms::Engine::refreshVstCatalog());
			QTRY_VERIFY_WITH_TIMEOUT(!lmms::Engine::vstCatalog()->snapshot().busy, 10000);
			const auto report = lmms::Engine::vstCatalog()->snapshot().report;
			QVERIFY(report); QCOMPARE(report->entries.size(), std::size_t{6}); QVERIFY(report->failures.empty());
		}
		void catalogPublicationAndSelector()
		{
			auto* jobs = lmms::Engine::vstCatalog(); QVERIFY(jobs);
			const auto requested = jobs->snapshot().requested;
			lmms::ConfigManager::inst()->setValue("vst", "scanroots", "{invalid");
			QString error; QVERIFY(!lmms::Engine::refreshVstCatalog(&error)); QVERIFY(!error.isEmpty());
			QCOMPARE(jobs->snapshot().requested, requested);
			QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
			lmms::gui::EffectSelectDialog selector(nullptr);
			auto* table = selector.findChild<QTableView*>(); QVERIFY(table);
			auto* search = selector.findChild<QLineEdit*>(); QVERIFY(search);
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
			const auto factory = QDir(qEnvironmentVariable("LMMS_PLUGIN_DIR")).absoluteFilePath("../vst-host/x64/Release/Vst3Factory.vst3");
			QVERIFY(QFile::copy(factory, m_fixtureRoot + "/Vst3Factory.vst3"));
			QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2", "vst3"}}}));
			QVERIFY(lmms::Engine::refreshVstCatalog());
			QTRY_VERIFY_WITH_TIMEOUT(!jobs->snapshot().busy, 10000);
			const auto report = jobs->snapshot().report; QVERIFY(report);
			QCOMPARE(report->entries.size(), std::size_t{8}); QVERIFY(report->failures.empty());
			lmms::Plugin::Descriptor::SubPluginFeatures::KeyList keys;
			const auto* descriptor = lmms::PluginFactory::instance()->pluginInfo("vsteffect").descriptor;
			descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
			QCOMPARE(keys.size(), 8);
			int classes = 0;
			for (const auto& key : keys) {
				if (key.attributes.value("format") != "vst3") { continue; }
				++classes; const auto cid = QByteArray::fromHex(key.attributes.value("classid").toLatin1());
				QCOMPARE(cid.size(), 16); QCOMPARE(key.attributes.value("architecture"), QString("64"));
				bool exact = false;
				for (const auto& entry : report->entries) {
					if (entry.identity.format == lmms::vsthost::Format::Vst3 &&
						cid == QByteArray(reinterpret_cast<const char*>(entry.identity.cid.data()), 16)) {
						exact = key.name == entry.name && key.attributes.value("identity") == entry.identity.key() &&
							key.attributes.value("version") == entry.locator.version && key.attributes.value("vendor") == entry.vendor;
					}
				}
				QVERIFY(exact);
			}
			QCOMPARE(classes, 2);
			QVERIFY(lmms::ConfigManager::inst()->setVstScanRoots({{m_fixtureRoot, {"vst2"}}}));
			QVERIFY(lmms::Engine::refreshVstCatalog()); QTRY_VERIFY_WITH_TIMEOUT(!jobs->snapshot().busy, 10000);
			QVERIFY(fixtureProcesses("catalog-only scans leave no native instance").empty());
			selector.close();
		}
	void cleanupTestCase()
	{
			lmms::Engine::destroy();
			QVERIFY(lmms::Engine::vstCatalog() == nullptr);
		QVERIFY(fixtureProcesses("DAW shutdown").empty());
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
		auto root = preset.createElement("preset"); preset.appendChild(root);
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
				if (!key.attributes.value("file").endsWith("Vst2Shell.dll")) { continue; }
				++shellEntries;
				const auto id = key.attributes.value("shellid").toUInt();
				QVERIFY(id == UINT32_C(0xf1020304) || id == UINT32_C(0x01000200));
				QCOMPARE(key.name, id == UINT32_C(0xf1020304) ? QString("Shell Alpha") : QString("Shell Beta"));
			}
			QCOMPARE(shellEntries, 4);
		QJsonObject subKey;
		for (const auto& key : keys)
		{
			if (QDir::fromNativeSeparators(QDir(m_fixtureRoot).absoluteFilePath(key.attributes.value("file"))) != m_fixture) { continue; }
			QJsonObject attributes;
			for (auto it = key.attributes.begin(); it != key.attributes.end(); ++it) { attributes.insert(it.key(), it.value()); }
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
		auto contents = oldProject.readAll(); contents.replace("@ARCH@", architecture.toUtf8());
		QFile frozenProject(temporary.filePath("frozen.mmp"));
		QVERIFY(frozenProject.open(QIODevice::WriteOnly));
		QCOMPARE(frozenProject.write(contents), qint64(contents.size())); frozenProject.close();
		lmms::Engine::getSong()->loadProject(frozenProject.fileName());
		QCOMPARE(lmms::Engine::getSong()->tracks().size(), std::size_t{1});
		QVERIFY(fixtureProcesses("frozen legacy project / uservst parameter state").size() == 1);
		const auto output = temporary.filePath("frozen.wav");
		const auto render = command("export.audio", {{"path", output},
			{"range", QJsonObject{{"start", 0}, {"end", 192}}}});
		QVERIFY(render.ok);
		QVERIFY(fixtureProcesses("offline audio export").size() == 1);
		QElapsedTimer deadline; deadline.start();
		QString status;
		do
		{
			QCoreApplication::processEvents();
			const auto result = command("export.status", {{"task", render.data.value("task")}});
			QVERIFY(result.ok);
			status = result.data.value("status").toString();
			if (status == "completed" || status == "failed" || status == "cancelled") { break; }
			QTest::qWait(10);
		} while (deadline.elapsed() < 30000);
		QCOMPARE(status, QString("completed"));
		QFile wave(output); QVERIFY(wave.open(QIODevice::ReadOnly));
		const auto audio = wave.readAll();
		QVERIFY(audio.startsWith("RIFF") && audio.mid(8, 4) == "WAVE");
		const auto data = audio.indexOf("data", 12);
		QVERIFY(data >= 12 && audio.size() > data + 8 + 44100 * 4);
		bool nonzero = false;
		for (qsizetype i = data + 8; i < audio.size(); ++i) { if (audio[i] != 0) { nonzero = true; break; } }
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
				QDomDocument preset; auto state = preset.createElement("vestige"); preset.appendChild(state);
				state.setAttribute("plugin", path); state.setAttribute("shellid", QString::number(id));
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
						if (QDir::fromNativeSeparators(key.attributes.value("file")) != path ||
						key.attributes.value("shellid").toUInt() != id) { continue; }
					QJsonObject attributes;
					for (auto it = key.attributes.begin(); it != key.attributes.end(); ++it) { attributes.insert(it.key(), it.value()); }
					subKey = {{"name", key.name}, {"attributes", attributes}};
				}
				QVERIFY(!subKey.isEmpty());
				QVERIFY(command("effect.add", {{"owner", "track:0"}, {"plugin", "vsteffect"}, {"subKey", subKey}}).ok);
				QVERIFY(fixtureProcesses("selected shell effect", L"Vst2Shell.dll").size() == 3);
				QVERIFY(command("history.undo").ok);
				QVERIFY(fixtureProcesses("shell effect undo", L"Vst2Shell.dll").size() == 2);
				QVERIFY(command("history.redo").ok);
				QVERIFY(fixtureProcesses("shell effect redo", L"Vst2Shell.dll").size() == 3);
				QTemporaryDir temporary; QVERIFY(temporary.isValid());
				const auto project = temporary.filePath("shell.mmp");
				QVERIFY(lmms::Engine::getSong()->saveProjectFile(project));
				QFile file(project); QVERIFY(file.open(QIODevice::ReadOnly));
				QVERIFY(file.readAll().contains("shellid=\"" + QByteArray::number(id) + "\"")); file.close();
				lmms::Engine::getSong()->clearProject();
				QVERIFY(fixtureProcesses("shell project cleared", L"Vst2Shell.dll").empty());
				lmms::Engine::getSong()->loadProject(project);
				QCOMPARE(lmms::Engine::getSong()->tracks().size(), std::size_t(2));
				QVERIFY(fixtureProcesses("shell project reopened", L"Vst2Shell.dll").size() == 3);
				for (auto* loaded : lmms::Engine::getSong()->tracks())
				{
					auto* instrument = dynamic_cast<lmms::InstrumentTrack*>(loaded); QVERIFY(instrument);
					QDomDocument saved; auto root = saved.createElement("preset"); saved.appendChild(root);
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
