#include <QtTest>
#include <QDomDocument>
#include <QFile>
#include <QDir>
#include <windows.h>
#include <tlhelp32.h>
#include "Engine.h"
#include "AudioEngine.h"
#include "SampleFrame.h"
#include "VstPlugin.h"
#include "MidiEvent.h"
#include "communication.h"
#include <vector>

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
QTEST_GUILESS_MAIN(Vst2CompatibilityTest)
#include "Vst2CompatibilityTest.moc"
