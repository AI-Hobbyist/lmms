#include "vsthost/PluginCatalog.h"
#include "vsthost/Vst3Scanner.h"
#include <QtTest>
#include <QDomDocument>
#include <QFile>
#include <QDir>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <windows.h>
#include <tlhelp32.h>
#include "Engine.h"
#include "AudioEngine.h"
#include "AudioDevice.h"
#include "AudioBusHandle.h"
#include "Mixer.h"
#include "DummyEffect.h"
#include "AudioBuffer.h"
#include "AudioDelayLine.h"
#include "SampleFrame.h"
#include "VstPlugin.h"
#include "MidiEvent.h"
#include "communication.h"
#include "AutomatableModel.h"
#include "ProjectJournal.h"
#include <QScopeGuard>
#include "vsthost/CatalogIo.h"
#include <vector>
#include <thread>

class EditorProxy : public lmms::RemotePlugin
{
public:
	HWND window = nullptr;
	bool processMessage(const message& message) override
	{
		if (message.id == lmms::IdVstPluginWindowID)
		{ window = reinterpret_cast<HWND>(static_cast<intptr_t>(message.getInt())); return true; }
		return RemotePlugin::processMessage(message);
	}
};

class Vst2CompatibilityTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { lmms::Engine::init(true); }
	void cleanupTestCase() { lmms::Engine::destroy(); }
	void vst3Facade_data()
	{
		QTest::addColumn<QString>("architecture"); QTest::addColumn<int>("classIndex");
		QTest::newRow("x64-f32") << "x64" << 0; QTest::newRow("x64-f64") << "x64" << 1;
		QTest::newRow("x86-f32") << "x86" << 0; QTest::newRow("x86-f64") << "x86" << 1;
	}
	void vst3Facade()
	{
		QFETCH(QString, architecture); QFETCH(int, classIndex);
		const auto root = qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT");
		const auto path = QDir(root).absoluteFilePath("../vst-host/" + architecture + "/Release/Vst3Native.vst3");
		const auto helper = QDir(qEnvironmentVariable("LMMS_PLUGIN_DIR")).absoluteFilePath(
			architecture == "x86" ? "32/RemoteVstHost32.exe" : "RemoteVstHost64.exe");
		const auto scan = lmms::vsthost::scanVst3(helper.toStdWString(), path.toStdWString());
		QVERIFY(scan.error == lmms::vsthost::Error::None); QCOMPARE(scan.classes.size(), std::size_t(3));
		lmms::vsthost::CatalogEntry entry; entry.identity.cid = scan.classes[classIndex].cid;
		entry.identity.architecture = architecture == "x86" ? lmms::vsthost::Architecture::X86 : lmms::vsthost::Architecture::X64;
		entry.locator.modulePath = path; entry.name = "Native class " + QString::number(classIndex); entry.vendor = "Fixture vendor";
		entry.locator.binaryPath = QDir::cleanPath(path);
		entry.locator.version = QString::fromUtf8(scan.classes[classIndex].version);
		const lmms::vsthost::CatalogIo io(QDir(qEnvironmentVariable("LMMS_PLUGIN_DIR")).absoluteFilePath("RemoteCatalogIo.exe"), 15000, {});
		const auto fingerprint = io.request({{"op", "fingerprint"}, {"path", QDir::cleanPath(path)}, {"binary", QDir::cleanPath(path)}});
		QVERIFY(fingerprint.error == lmms::vsthost::Error::None);
		entry.locator.fingerprint = QByteArray::fromHex(fingerprint.object.value("fingerprint").toString().toLatin1());
		QCOMPARE(entry.locator.fingerprint.size(), 32);
		for (int invalid = 0; invalid < 5; ++invalid)
		{
			auto stale = entry;
			if (invalid == 0) { stale.locator.fingerprint[0] ^= 1; }
			if (invalid == 1) { stale.locator.version = "missing-version"; }
			if (invalid == 2) { stale.locator.binaryPath += ".missing"; }
			if (invalid == 3) { stale.identity.architecture = architecture == "x86" ? lmms::vsthost::Architecture::X64 : lmms::vsthost::Architecture::X86; }
			if (invalid == 4) { stale.locator.modulePath += ".missing"; }
			lmms::VstPlugin rejected(path, 0, "none", &stale);
			QVERIFY(rejected.failed()); QVERIFY(!rejected.isRunning());
		}
		lmms::VstPlugin plugin(path, 0, "none", &entry);
		QVERIFY(!plugin.failed() && plugin.isRunning()); QCOMPARE(plugin.parameterDump().size(), 2); QVERIFY(plugin.hasEditor());
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		std::vector<lmms::SampleFrame> input(frames, lmms::SampleFrame(1, 1)), output(frames);
		QVERIFY(plugin.process(input.data(), output.data()));
		for (const auto& sample : output) { QCOMPARE(sample.left(), classIndex ? 0.75f : 0.25f); }
		// Process the actual requested extent, including buffers shorter than a period.
		for (const auto count : {lmms::f_cnt_t(1), lmms::f_cnt_t(17), lmms::f_cnt_t(65), frames - 1, frames})
		{
			const lmms::SampleFrame guard(123.0f, -123.0f);
			std::vector<lmms::SampleFrame> shortInput(count + 2, lmms::SampleFrame(1.0f, -0.5f));
			std::vector<lmms::SampleFrame> shortOutput(count + 2, guard);
			QVERIFY(plugin.process(shortInput.data() + 1, shortOutput.data() + 1, count));
			QCOMPARE(shortOutput.front().left(), guard.left()); QCOMPARE(shortOutput.front().right(), guard.right());
			QCOMPARE(shortOutput.back().left(), guard.left()); QCOMPARE(shortOutput.back().right(), guard.right());
			for (unsigned frame = 1; frame <= count; ++frame)
			{
				QCOMPARE(shortOutput[frame].left(), classIndex ? 0.75f : 0.25f);
				QCOMPARE(shortOutput[frame].right(), classIndex ? -0.375f : -0.125f);
			}
		}
		lmms::SampleFrame untouched(123.0f, -123.0f);
		QVERIFY(!plugin.process(input.data(), &untouched, 0));
		QCOMPARE(untouched.left(), 123.0f); QCOMPARE(untouched.right(), -123.0f);
		QVERIFY(plugin.isRunning());
		plugin.setParam(0, 0.5f);
		plugin.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOn, 3, 60, 127), 17);
		plugin.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOff, 3, 60, 0), 23);
		QVERIFY(plugin.process(input.data(), output.data()));
		for (unsigned frame = 0; frame < frames; ++frame)
		{ QCOMPARE(output[frame].left(), frame >= 17 && frame < 23 ? 1.5f : 0.5f); }
		QDomDocument saved; auto state = saved.createElement("vstplugin"); saved.appendChild(state);
		plugin.saveSettings(saved, state); QCOMPARE(state.attribute("format"), QString("vst3"));
		QCOMPARE(state.attribute("pluginname"), entry.name); QCOMPARE(state.attribute("vendor"), entry.vendor);
		QVERIFY(!state.attribute("vst3state").isEmpty() && !state.hasAttribute("chunk"));
		// Rejected project data must neither reach a healthy native instance nor
		// overwrite the imported text when the project is saved again.
		const auto good = QByteArray::fromBase64(state.attribute("vst3state").toLatin1());
		for (unsigned invalid = 0; invalid < 8; ++invalid)
		{
			auto broken = state.cloneNode(true).toElement(); auto bytes = good;
			if (invalid == 0) { bytes.truncate(3); }
			if (invalid == 1) { bytes[0] = 2; }
			if (invalid == 2) { bytes[4] = char(0xff); }
			if (invalid == 3) { bytes[12] = 2; }
			if (invalid == 4) { bytes[12] = 0; }
			QString encoded = QString::fromLatin1(bytes.toBase64());
			if (invalid == 5) { encoded += "!"; }
			if (invalid == 6) { encoded = ""; }
			if (invalid == 7) { encoded = QString(((lmms::vsthost::MaxControlBytes + 2ull) / 3) * 4 + 1, 'A'); }
			broken.setAttribute("vst3state", encoded); plugin.loadSettings(broken);
			QVERIFY(plugin.isRunning() && !plugin.failed());
			QVERIFY(plugin.process(input.data(), output.data())); QCOMPARE(output[0].left(), 0.5f);
			QDomDocument retained; auto retainedState = retained.createElement("vstplugin"); retained.appendChild(retainedState);
			plugin.saveSettings(retained, retainedState); QCOMPARE(retainedState.attribute("vst3state"), encoded);
			plugin.loadSettings(state); QVERIFY(plugin.isRunning());
		}
		QDomDocument restored; auto restoredState = restored.createElement("vstplugin"); restored.appendChild(restoredState);
		plugin.saveSettings(restored, restoredState); QCOMPARE(QByteArray::fromBase64(restoredState.attribute("vst3state").toLatin1()), good);
		{
			lmms::VstPlugin rejectedState(path, 0, "none", &entry); QVERIFY(rejectedState.isRunning());
			QByteArray opaque(16, 0); opaque[0] = 1;
			auto broken = state.cloneNode(true).toElement(); broken.setAttribute("vst3state", QString::fromLatin1(opaque.toBase64()));
			rejectedState.loadSettings(broken); QVERIFY(rejectedState.failed() && !rejectedState.isRunning());
			QDomDocument retained; auto retainedState = retained.createElement("vstplugin"); retained.appendChild(retainedState);
			rejectedState.saveSettings(retained, retainedState); QCOMPARE(retainedState.attribute("vst3state"), broken.attribute("vst3state"));
		}
		plugin.setParam(0, 0.875f); plugin.loadSettings(state);
		QCOMPARE(plugin.parameterDump().value("param0").section(':', 2).toFloat(), 0.5f);
		std::thread queuedParameter([&] { plugin.setParam(0, 0.625f); }); queuedParameter.join();
		QDomDocument immediate; auto immediateState = immediate.createElement("vstplugin"); immediate.appendChild(immediateState);
		plugin.saveSettings(immediate, immediateState);
		const auto pending = QByteArray::fromBase64(immediateState.attribute("vst3state").toLatin1());
		QCOMPARE(pending.size(), 32);
		const auto pendingBytes = std::span(reinterpret_cast<const std::uint8_t*>(pending.constData()), std::size_t(pending.size()));
		QCOMPARE(std::bit_cast<double>(lmms::vsthost::get(pendingBytes, 16, 8)), 0.625);
		QCOMPARE(std::bit_cast<double>(lmms::vsthost::get(pendingBytes, 24, 8)), 0.625);
		plugin.loadSettings(state);
		plugin.setParam(1, 0.25f); plugin.idleUpdate(); plugin.setParam(0, 0.875f);
		QCOMPARE(plugin.parameterDump().value("param0").section(':', 2).toFloat(), 0.875f);
		QVERIFY(plugin.process(input.data(), output.data())); QCOMPARE(output[0].left(), 0.875f);
		plugin.showUI(); QCOMPARE(plugin.isUIVisible(), 1); plugin.hideUI(); QCOMPARE(plugin.isUIVisible(), 0);

		plugin.idleUpdate(); // Drain preceding DSP feedback before testing a GUI gesture.
		lmms::FloatModel model(0.875f, 0.0f, 1.0f, 0.0f);
		plugin.bindParameterModel(0, &model);
		connect(&model, &lmms::FloatModel::dataChanged, &plugin, [&] { plugin.setParam(0, model.value()); });
		QSignalSpy began(&plugin, &lmms::VstPlugin::parameterEditBegan);
		QSignalSpy performed(&plugin, &lmms::VstPlugin::parameterEdited);
		QSignalSpy ended(&plugin, &lmms::VstPlugin::parameterEditEnded);
		plugin.showUI();
		HWND editor = nullptr;
		EnumWindows([](HWND window, LPARAM pointer) -> BOOL
		{
			auto& editor = *reinterpret_cast<HWND*>(pointer);
			editor = FindWindowExW(window, nullptr, L"STATIC", L"LMMS VST3 fixture editor");
			return editor ? FALSE : TRUE;
		}, reinterpret_cast<LPARAM>(&editor));
		QVERIFY(editor && IsWindow(editor));
		DWORD pid = 0; GetWindowThreadProcessId(editor, &pid); QVERIFY(pid != GetCurrentProcessId());
		DWORD_PTR reply = 0;
		QVERIFY(SendMessageTimeoutW(editor, WM_APP + 38, 0, 0, SMTO_ABORTIFHUNG, 1000, &reply));
		QCOMPARE(reply, DWORD_PTR(1));
		// A host setter must consume any genuine GUI edits carried in its reply.
		plugin.setParam(1, 0.5f);
		QCOMPARE(model.value(), 0.625f);
		QCOMPARE(began.count(), 1); QCOMPARE(performed.count(), 1); QCOMPARE(ended.count(), 1);
		QVERIFY(model.isJournallingStateStackEmpty());
		plugin.idleUpdate();
		QCOMPARE(performed.count(), 1); // The host-setter echo must remain suppressed.
		plugin.hideUI();
		QVERIFY(GetModuleHandleW(L"Vst3Native.vst3") == nullptr);
		std::fill(input.begin(), input.end(), lmms::SampleFrame(1.f, 1.f));
		for (const auto* variable : {"LMMS_VST3_FIXTURE_NO_EDITOR", "LMMS_VST3_FIXTURE_UNSUPPORTED_EDITOR"})
		{
			qputenv(variable, "1");
			lmms::VstPlugin generic(path, 0, "none", &entry);
			qunsetenv(variable);
			QVERIFY(!generic.failed() && generic.isRunning()); QVERIFY(!generic.hasEditor());
			QCOMPARE(generic.parameterDump().size(), 2);
			generic.showUI(); QCOMPARE(generic.isUIVisible(), 0);
			QVERIFY(!generic.failed() && generic.isRunning());
			generic.setParam(0, 0.5f);
			QVERIFY(generic.process(input.data(), output.data()));
			for (const auto& sample : output) { QCOMPARE(sample.left(), 0.5f); QCOMPARE(sample.right(), 0.5f); }
		}

	}

	void legacyProxy_data()
	{
		QTest::addColumn<QString>("architecture");
		QTest::newRow("x86") << "x86";
		QTest::newRow("x64") << "x64";
	}
	void legacyProxy()
	{
		QFETCH(QString, architecture);
		const auto root = qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT");
		QVERIFY2(!root.isEmpty(), "LMMS_VST_FIXTURE_ROOT must point to the built x86/x64 fixtures");
		const auto path = QDir(root).absoluteFilePath(architecture + "/fixtures/Release/Vst2Baseline.dll");
		QVERIFY2(QFile::exists(path), qPrintable(path));
		lmms::VstPlugin plugin(path);
		QVERIFY(!plugin.failed());
		QCOMPARE(plugin.name(), QString("LMMS baseline"));
		QVERIFY(GetModuleHandleW(L"Vst2Baseline.dll") == nullptr);
		// Capture actual child-process module loading; the fixture must be absent
		// from the test/DAW process and present in the selected helper.
		bool loadedInChild = false;
		const auto processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		QVERIFY(processes != INVALID_HANDLE_VALUE);
		PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
		if (Process32FirstW(processes, &entry)) do
		{
			if (entry.th32ParentProcessID != GetCurrentProcessId()) { continue; }
			const auto modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, entry.th32ProcessID);
			if (modules == INVALID_HANDLE_VALUE) { continue; }
			MODULEENTRY32W module{}; module.dwSize = sizeof(module);
			if (Module32FirstW(modules, &module)) do
			{
				if (QString::fromWCharArray(module.szModule) == "Vst2Baseline.dll")
				{
					loadedInChild = true;
					qInfo() << "VST module" << QString::fromWCharArray(module.szExePath)
						<< "child PID" << entry.th32ProcessID << "helper" << QString::fromWCharArray(entry.szExeFile);
				}
			} while (Module32NextW(modules, &module));
			CloseHandle(modules);
		} while (Process32NextW(processes, &entry));
		CloseHandle(processes);
		QVERIFY(loadedInChild);

		QFile settings(QFINDTESTDATA("legacy-vst2.xml"));
		QVERIFY(settings.open(QIODevice::ReadOnly));
		QDomDocument document;
		QVERIFY(document.setContent(settings.readAll()));
		plugin.loadSettings(document.documentElement());
		plugin.idleUpdate();
		QCOMPARE(plugin.currentProgram(), 1);
		QCOMPARE(plugin.parameterDump().size(), 1);
		QCOMPARE(plugin.parameterDump().value("param0").section(':', 2).toFloat(), 0.25f);
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		std::vector<lmms::SampleFrame> input(frames, lmms::SampleFrame(1.0f, -0.5f)), output(frames);
		QVERIFY(plugin.process(input.data(), output.data()));
		for (const auto& sample : output)
		{
			QCOMPARE(sample.left(), 0.25f);
			QCOMPARE(sample.right(), -0.125f);
		}
		// Send in reverse order to exercise the existing helper's chronological sorting.
		plugin.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOff, 2, 60, 0), 23);
		plugin.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOn, 2, 60, 127), 17);
		QVERIFY(plugin.process(input.data(), output.data()));
		for (unsigned frame = 0; frame < frames; ++frame)
		{
			const float note = frame >= 17 && frame < 23 ? 1.0f : 0.0f;
			QCOMPARE(output[frame].left(), 0.25f + note);
			QCOMPARE(output[frame].right(), -0.125f + note);
		}
		QDomDocument saved;
		auto element = saved.createElement("vestige"); saved.appendChild(element);
		plugin.saveSettings(saved, element);
		QVERIFY(element.hasAttribute("chunk"));
		plugin.setParam(0, 0.75f);
		plugin.loadSettings(element);
		QCOMPARE(plugin.parameterDump().value("param0").section(':', 2).toFloat(), 0.25f);
	}
		void nativeEditor_data() { legacyProxy_data(); }
		void shellEditorFeedback_data() { legacyProxy_data(); }
		void shellEditorFeedback()
		{
			QFETCH(QString, architecture);
			const auto path = QDir(qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT"))
				.absoluteFilePath(architecture + "/fixtures/Release/Vst2Shell.dll");
			lmms::VstPlugin plugin(path, UINT32_C(0xf1020304), "none");
			QVERIFY(!plugin.failed());
			lmms::FloatModel model(0.25f, 0.0f, 1.0f, 0.0f);
			plugin.bindParameterModel(0, &model);
			connect(&model, &lmms::FloatModel::dataChanged, &plugin, [&] { plugin.setParam(0, model.value()); });
			QSignalSpy began(&plugin, &lmms::VstPlugin::parameterEditBegan);
			QSignalSpy performed(&plugin, &lmms::VstPlugin::parameterEdited);
			QSignalSpy ended(&plugin, &lmms::VstPlugin::parameterEditEnded);
			auto* journal = lmms::Engine::projectJournal();
			const auto oldJournalling = journal->isJournalling();
			const auto restoreJournal = qScopeGuard([&] { journal->clearJournal(); journal->setJournalling(oldJournalling); });
			journal->clearJournal(); journal->setJournalling(true);
			struct Search { DWORD parent; HWND editor = nullptr; } search{GetCurrentProcessId()};
			EnumWindows([](HWND top, LPARAM pointer) -> BOOL
			{
				auto& search = *reinterpret_cast<Search*>(pointer);
				DWORD pid = 0; GetWindowThreadProcessId(top, &pid);
				if (pid == search.parent) { return TRUE; }
				const auto processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
				if (processes == INVALID_HANDLE_VALUE) { return TRUE; }
				PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry); bool child = false;
				if (Process32FirstW(processes, &entry)) do { child |= entry.th32ProcessID == pid && entry.th32ParentProcessID == search.parent; } while (Process32NextW(processes, &entry));
				CloseHandle(processes);
				if (child) { search.editor = FindWindowExW(top, nullptr, L"STATIC", L"LMMS VST2 fixture"); }
				return search.editor ? FALSE : TRUE;
			}, reinterpret_cast<LPARAM>(&search));
			QVERIFY(search.editor && IsWindow(search.editor));
			DWORD pid = 0; GetWindowThreadProcessId(search.editor, &pid); QVERIFY(pid != GetCurrentProcessId());
			DWORD_PTR nativeResult = 0;
			QVERIFY(SendMessageTimeoutW(search.editor, WM_APP + 37, 0, 0, SMTO_ABORTIFHUNG, 1000, &nativeResult));
			QCOMPARE(nativeResult, DWORD_PTR(1));
			plugin.idleUpdate();
			QTRY_COMPARE(ended.count(), 1);
			QCOMPARE(began.count(), 1); QCOMPARE(performed.count(), 2);
			QCOMPARE(model.value(), 0.625f); QVERIFY(model.isJournallingStateStackEmpty());
			QCOMPARE(plugin.parameterDump().value("param0").section(':', 2).toFloat(), 0.625f);
			QCOMPARE(journal->undoDepth(), 1);
			journal->undo(); QCOMPARE(model.value(), 0.25f);
			QCOMPARE(plugin.parameterDump().value("param0").section(':', 2).toFloat(), 0.25f);
			journal->redo(); QCOMPARE(model.value(), 0.625f);
			QCOMPARE(plugin.parameterDump().value("param0").section(':', 2).toFloat(), 0.625f);
			plugin.setParam(0, 0.875f); plugin.parameterDump(); plugin.idleUpdate();
			QCoreApplication::processEvents(); QCOMPARE(performed.count(), 2);
			QVERIFY(GetModuleHandleW(L"Vst2Shell.dll") == nullptr);
		}
		void shellProxy_data() { legacyProxy_data(); }
		void shellProxy()
		{
			QFETCH(QString, architecture);
			const auto path = QDir(qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT"))
				.absoluteFilePath(architecture + "/fixtures/Release/Vst2Shell.dll");
			const auto scan = lmms::VstPlugin::scanModule(path);
			QVERIFY2(scan.error.isEmpty(), qPrintable(scan.error));
			QVERIFY(scan.shell); QCOMPARE(scan.entries.size(), std::size_t(2));
			QCOMPARE(scan.entries[0].id, UINT32_C(0xf1020304));
			QCOMPARE(scan.entries[1].id, UINT32_C(0x01000200));
			for (const auto id : {UINT32_C(0xf1020304), UINT32_C(0x01000200)})
			{
				QDomDocument saved; auto element = saved.createElement("vestige"); saved.appendChild(element);
				{
					lmms::VstPlugin plugin(path, id);
					QVERIFY(!plugin.failed()); QCOMPARE(plugin.shellId(), id);
					QCOMPARE(plugin.name(), id == UINT32_C(0xf1020304) ? QString("Shell Alpha") : QString("Shell Beta"));
					const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
					std::vector<lmms::SampleFrame> input(frames, lmms::SampleFrame(1.0f, 1.0f)), output(frames);
					QVERIFY(plugin.process(input.data(), output.data()));
					const float expected = id == UINT32_C(0xf1020304) ? 0.25f : 0.75f;
					for (const auto& sample : output) { QCOMPARE(sample.left(), expected); QCOMPARE(sample.right(), expected); }
					plugin.setParam(0, 0.375f); plugin.saveSettings(saved, element);
					QCOMPARE(element.attribute("shellid"), QString::number(id));
				}
				lmms::VstPlugin restored(path, element.attribute("shellid").toUInt());
				QVERIFY(!restored.failed()); restored.loadSettings(element);
				QCOMPARE(restored.parameterDump().value("param0").section(':', 2).toFloat(), 0.375f);
			}
			QVERIFY(GetModuleHandleW(L"Vst2Shell.dll") == nullptr);
		}
		void malformedPe()
		{
			for (const auto kind : {0, 1, 2, 3})
			{
				QTemporaryFile file; QVERIFY(file.open());
				QByteArray bytes(kind == 0 ? 1 : 70, '\0');
				if (kind >= 2) { bytes[0] = 'M'; bytes[1] = 'Z'; }
				if (kind == 2) { bytes[0x3f] = static_cast<char>(0xff); }
				QCOMPARE(file.write(bytes), bytes.size()); file.flush(); file.close();
				lmms::VstPlugin plugin(file.fileName());
				QVERIFY(plugin.failed());
			}
		}
	void nativeEditor()
	{
		QFETCH(QString, architecture);
		const auto path = QDir(qEnvironmentVariable("LMMS_VST_FIXTURE_ROOT"))
			.absoluteFilePath(architecture + "/fixtures/Release/Vst2Baseline.dll");
		EditorProxy proxy;
		proxy.init(architecture == "x86" ? "32/RemoteVstPlugin32" : "RemoteVstPlugin64", false, {"none"});
		proxy.waitForHostInfoGotten();
		QVERIFY(!proxy.failed());
		proxy.sendMessage(lmms::RemotePluginBase::message(lmms::IdVstLoadPlugin).addString(path.toStdString()));
		proxy.waitForInitDone();
		QVERIFY(!proxy.failed());
		QVERIFY(proxy.window && IsWindow(proxy.window));
		proxy.showUI();
		QCOMPARE(proxy.isUIVisible(), 1);
		proxy.hideUI();
		QCOMPARE(proxy.isUIVisible(), 0);
		RECT bounds{};
		QVERIFY(GetWindowRect(proxy.window, &bounds));
		QVERIFY(bounds.right > bounds.left && bounds.bottom > bounds.top);
	}
};
QTEST_MAIN(Vst2CompatibilityTest)
#include "Vst2CompatibilityTest.moc"
