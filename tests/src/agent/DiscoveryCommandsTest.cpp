#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include "agent/CommandBus.h"
#include "agent/ToolRegistry.h"
#include "Engine.h"

using namespace lmms;
using namespace lmms::agent;

namespace {

const QStringList M1Tools{
	"instrument.listPlugins", "preset.list", "sample.list", "svs.listVoices", "svc.listModels", "fs.listRoots",
	"fs.listDirectory"};

QJsonObject argumentsFor(const QString& name)
{
	if (name == "fs.listDirectory")
	{
		return {{"path", QDir::tempPath()}};
	}
	return {};
}

} // namespace

class DiscoveryCommandsTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void registry()
	{
		const auto tools = ToolRegistry::tools();
		QSet<QString> names;
		for (const auto& entry : tools)
		{
			names.insert(entry.toObject().value("name").toString());
		}
		for (const auto& name : M1Tools)
		{
			QVERIFY2(names.contains(name), qPrintable(name));
			const auto help = ToolRegistry::commandHelp(name);
			QVERIFY(!help.isEmpty());
			QCOMPARE(help.value("annotations").toObject().value("readOnlyHint").toBool(), true);
			QCOMPARE(help.value("inputSchema").toObject().value("type").toString(), QString("object"));
		}
	}

	void fsRoots()
	{
		const auto result = CommandBus::instance().execute("fs.listRoots");
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		const auto roots = result.data.value("roots").toArray();
		QVERIFY(!roots.isEmpty());
		bool hasHome = false;
		for (const auto& entry : roots)
		{
			const auto root = entry.toObject();
			QVERIFY(!root.value("path").toString().isEmpty());
			hasHome = hasHome || root.value("type").toString() == "home";
		}
		QVERIFY(hasHome);
		QVERIFY(!result.data.value("knownBases").toArray().isEmpty());
	}

	void fsDirectory()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QVERIFY(QDir(directory.path()).mkpath("nested"));
		QFile wav(directory.filePath("alpha.wav"));
		QVERIFY(wav.open(QIODevice::WriteOnly));
		wav.write("RIFF");
		wav.close();
		QFile text(directory.filePath("beta.txt"));
		QVERIFY(text.open(QIODevice::WriteOnly));
		text.close();

		const auto all = CommandBus::instance().execute("fs.listDirectory", {{"path", directory.path()}});
		QVERIFY2(all.ok, qPrintable(all.errorMessage));
		const auto entries = all.data.value("entries").toArray();
		QCOMPARE(entries.size(), 3);
		QCOMPARE(entries.first().toObject().value("type").toString(), QString("directory"));
		QCOMPARE(entries.first().toObject().value("name").toString(), QString("nested"));

		const auto wavOnly = CommandBus::instance().execute(
			"fs.listDirectory", {{"path", directory.path()}, {"extensions", QJsonArray{"wav"}}});
		QVERIFY2(wavOnly.ok, qPrintable(wavOnly.errorMessage));
		const auto wavEntries = wavOnly.data.value("entries").toArray();
		QCOMPARE(wavEntries.size(), 2);
		for (const auto& entry : wavEntries)
		{
			const auto item = entry.toObject();
			if (item.value("type").toString() == "file")
			{
				QCOMPARE(item.value("extension").toString(), QString("wav"));
			}
		}

		const auto foldersOnly = CommandBus::instance().execute(
			"fs.listDirectory", {{"path", directory.path()}, {"directoriesOnly", true}});
		QVERIFY2(foldersOnly.ok, qPrintable(foldersOnly.errorMessage));
		QCOMPARE(foldersOnly.data.value("entries").toArray().size(), 1);

		const auto paged = CommandBus::instance().execute(
			"fs.listDirectory", {{"path", directory.path()}, {"pageSize", 1}, {"page", 1}});
		QVERIFY2(paged.ok, qPrintable(paged.errorMessage));
		QCOMPARE(paged.data.value("total").toInt(), 3);
		QCOMPARE(paged.data.value("entries").toArray().size(), 1);

		QVERIFY(!CommandBus::instance().execute("fs.listDirectory", {{"path", "relative"}}).ok);
		QVERIFY(!CommandBus::instance().execute(
			"fs.listDirectory", {{"path", directory.filePath("missing")}}).ok);
		QVERIFY(!CommandBus::instance().execute(
			"fs.listDirectory", {{"path", directory.filePath("alpha.wav")}}).ok);
	}

	void factoryPresetsAndSamples()
	{
		const auto presets = CommandBus::instance().execute(
			"preset.list", {{"source", "factory"}, {"keyword", "Bass-Mania"}});
		QVERIFY2(presets.ok, qPrintable(presets.errorMessage));
		QCOMPARE(presets.data.value("total").toInt(), 1);
		const auto preset = presets.data.value("presets").toArray().first().toObject();
		QCOMPARE(preset.value("extension").toString(), QString("xpf"));
		QCOMPARE(preset.value("loadableBy").toArray().first().toString(), QString("instrument.loadPreset"));
		QVERIFY(QFileInfo(preset.value("path").toString()).isFile());

		const auto zyn = CommandBus::instance().execute(
			"preset.list", {{"source", "factory"}, {"keyword", "0001-Arpeggio1"}});
		QVERIFY2(zyn.ok, qPrintable(zyn.errorMessage));
		const auto zynPreset = zyn.data.value("presets").toArray().first().toObject();
		QCOMPARE(zynPreset.value("loadableBy").toArray().first().toString(), QString("pluginPreset"));

		const auto samples = CommandBus::instance().execute(
			"sample.list", {{"source", "factory"}, {"extensions", QJsonArray{"wav"}}});
		QVERIFY2(samples.ok, qPrintable(samples.errorMessage));
		QVERIFY(samples.data.value("total").toInt() > 0);
		for (const auto& entry : samples.data.value("samples").toArray())
		{
			const auto sample = entry.toObject();
			QCOMPARE(sample.value("extension").toString(), QString("wav"));
			QVERIFY(QFileInfo(sample.value("path").toString()).isFile());
		}

		const auto first = CommandBus::instance().execute("sample.list", {{"source", "factory"}, {"pageSize", 5}});
		const auto second = CommandBus::instance().execute("sample.list", {{"source", "factory"}, {"pageSize", 5}});
		QVERIFY(first.data == second.data);
	}

	void instrumentPlugins()
	{
		const auto result = CommandBus::instance().execute("instrument.listPlugins");
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QVERIFY(result.data.contains("plugins"));
		QVERIFY(result.data.contains("vstCatalog"));
		for (const auto& entry : result.data.value("plugins").toArray())
		{
			const auto plugin = entry.toObject();
			QVERIFY(!plugin.value("plugin").toString().isEmpty());
			QVERIFY(!plugin.value("name").toString().isEmpty());
			QVERIFY(!plugin.value("source").toString().isEmpty());
			QVERIFY(plugin.value("subKeys").isArray());
		}

		const auto vst = CommandBus::instance().execute("instrument.listPlugins", {{"source", "vst"}});
		QVERIFY2(vst.ok, qPrintable(vst.errorMessage));
		for (const auto& entry : vst.data.value("plugins").toArray())
		{
			QCOMPARE(entry.toObject().value("plugin").toString(), QString("vestige"));
		}
	}

	void svsAndSvcLists()
	{
		const auto voices = CommandBus::instance().execute("svs.listVoices");
		QVERIFY2(voices.ok, qPrintable(voices.errorMessage));
		QVERIFY(voices.data.value("voices").isArray());
		QVERIFY(voices.data.value("engines").isArray());
		QVERIFY(voices.data.contains("catalog"));

		const auto models = CommandBus::instance().execute("svc.listModels");
		QVERIFY2(models.ok, qPrintable(models.errorMessage));
		bool hasReference = false;
		for (const auto& entry : models.data.value("models").toArray())
		{
			const auto model = entry.toObject();
			if (model.value("engine_id").toString() == "reference" && model.value("id").toString() == "identity")
			{
				hasReference = true;
				QVERIFY(model.value("selection").toObject().contains("speaker_id"));
			}
		}
		QVERIFY(hasReference);
	}

	void readOnlyCommandsRejectDryRun()
	{
		for (const auto& name : M1Tools)
		{
			auto arguments = argumentsFor(name);
			arguments.insert("dryRun", true);
			const auto result = CommandBus::instance().execute(name, arguments);
			QVERIFY2(!result.ok, qPrintable(name));
			QCOMPARE(result.errorCode, QString("invalid_arguments"));
		}
	}
};

QTEST_MAIN(DiscoveryCommandsTest)
#include "DiscoveryCommandsTest.moc"
