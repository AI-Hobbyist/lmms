#include <QtTest>
#include <QDomDocument>
#include <QTemporaryDir>
#include "ConfigManager.h"
#include "PathUtil.h"
#include "vsthost/ScanRoots.h"

using namespace lmms;
using namespace lmms::vsthost;

class VstPathsMigrationTest : public QObject
{
	Q_OBJECT
private slots:
	#ifdef Q_OS_WIN
	void windowsPathComparison()
	{
		const std::vector<std::pair<QString, QString>> pairs{
			{"C:/Plugins", "c:/PLUGINS"},
			{QString::fromUtf16(u"C:/Straße"), "C:/STRASSE"},
			{QString::fromUtf16(u"C:/ﬀ"), "C:/ff"},
			{QString::fromUtf16(u"C:/å"), QString::fromUtf16(u"C:/a\u030a")},
			{QString::fromUtf16(u"C:/σ"), QString::fromUtf16(u"C:/ς")},
			{QString::fromUtf16(u"C:/я"), QString::fromUtf16(u"C:/Я")},
			{QString::fromUtf16(u"C:/Ａ"), "C:/A"},
			{QString::fromUtf16(u"C:/a\u200db"), "C:/ab"},
			{QString::fromUtf16(u"C:/\U00010428"), QString::fromUtf16(u"C:/\U00010400")}};
		for (const auto& [a, b] : pairs)
		{
			const auto comparison = CompareStringOrdinal(reinterpret_cast<LPCWSTR>(a.utf16()), static_cast<int>(a.size()),
				reinterpret_cast<LPCWSTR>(b.utf16()), static_cast<int>(b.size()), TRUE);
			QVERIFY(comparison != 0);
			const bool same = comparison == CSTR_EQUAL;
			QVERIFY2((scanPathKey(a) == scanPathKey(b)) == same, qPrintable(a + " / " + b));
			std::vector<ScanRoot> roots; QString error;
			QVERIFY(normalizeScanRoots({{a}, {b}}, roots, error));
			QCOMPARE(roots.size(), same ? std::size_t{1} : std::size_t{2});
		}
	}
	#endif

	void roundTripAndDuplicates()
	{
		QTemporaryDir directory; QVERIFY(directory.isValid());
		const auto path = directory.path() + QString::fromUtf16(u"/音楽");
		std::vector<ScanRoot> original{{path + "/./", {"vst3"}, false, false},
			{path, {"vst2"}, true, true}, {directory.path() + "/second", {"vst2", "vst3"}, true, true}};
		QByteArray json; QString error;
		QVERIFY(encodeScanRoots(original, json, error));
		std::vector<ScanRoot> decoded;
		QVERIFY(decodeScanRoots(json, decoded, error));
		QCOMPARE(decoded.size(), std::size_t{2});
		QCOMPARE(decoded[0].path, path);
		QCOMPARE(decoded[0].formats, QStringList{"vst3"});
		QVERIFY(!decoded[0].recursive); QVERIFY(!decoded[0].enabled);
		QByteArray again; QVERIFY(encodeScanRoots(decoded, again, error)); QCOMPARE(again, json);
#ifdef Q_OS_WIN
		original.push_back({path.toUpper().replace('/', '\\'), {"vst2"}});
		QVERIFY(normalizeScanRoots(original, decoded, error)); QCOMPARE(decoded.size(), std::size_t{2});
		QVERIFY(normalizeScanRoots({{"//server/share/音楽/../VST3", {"vst3"}}}, decoded, error));
		QCOMPARE(decoded.front().path, QString("//server/share/VST3"));
#endif
		QVERIFY(encodeScanRoots({}, json, error));
		QVERIFY(decodeScanRoots(json, decoded, error)); QVERIFY(decoded.empty());
	}

	void strictSchema()
	{
		QTemporaryDir directory; QVERIFY(directory.isValid());
		QByteArray good; QString error;
		QVERIFY(encodeScanRoots({{directory.path()}}, good, error));
		std::vector<ScanRoot> roots;
		for (qsizetype size = 0; size < good.size(); ++size)
		{
			roots = {{directory.path()}};
			QVERIFY(!decodeScanRoots(good.left(size), roots, error)); QVERIFY(roots.empty()); QVERIFY(!error.isEmpty());
		}
		const std::array<QByteArray, 7> invalid{{"{}", "[]", "{\"version\":2,\"roots\":[]}",
			"{\"version\":1.5,\"roots\":[]}", "{\"version\":1,\"roots\":[null]}",
			"{\"version\":1,\"roots\":[{}]}", QByteArray(1024 * 1024 + 1, ' ')}};
		for (const auto& json : invalid) { QVERIFY(!decodeScanRoots(json, roots, error)); QVERIFY(roots.empty()); }
		for (auto root : std::vector<ScanRoot>{{"relative/path"}, {directory.path(), {}},
			{directory.path(), {"vst3", "vst3"}}, {directory.path(), {"unknown"}},
			{directory.path() + QChar(0)}})
		{
			QByteArray json = good; QVERIFY(!encodeScanRoots({root}, json, error)); QVERIFY(json.isEmpty());
		}
		QByteArray json; QVERIFY(!encodeScanRoots(std::vector<ScanRoot>(257, {directory.path()}), json, error));
	}

	void migrationUsesExistingStandards()
	{
		QTemporaryDir directory; QVERIFY(directory.isValid());
		const auto legacy = directory.path() + "/missing-legacy";
		const auto standard = directory.path() + "/VST3"; QVERIFY(QDir().mkpath(standard));
		const auto roots = migrateScanRoots(legacy, {standard, standard + "/./", directory.path() + "/missing-standard"});
		QCOMPARE(roots.size(), std::size_t{2}); QCOMPARE(roots[0].path, legacy);
		QCOMPARE(roots[0].formats, (QStringList{"vst2", "vst3"}));
		QCOMPARE(roots[1].formats, QStringList{"vst3"});
	}

	void compatibilityRootNeverFollowsScanList()
	{
		QTemporaryDir directory; QVERIFY(directory.isValid());
		const auto legacy = directory.path() + QString::fromUtf16(u"/旧根");
		const auto configPath = directory.path() + "/config.xml";
		auto* config = ConfigManager::inst();
		QDomDocument doc; auto top = doc.createElement("lmms"); doc.appendChild(top);
		top.setAttribute("version", config->defaultVersion()); top.setAttribute("configversion", "3");
		auto paths = doc.createElement("paths"); top.appendChild(paths);
		paths.setAttribute("workingdir", directory.path() + "/missing-workspace"); paths.setAttribute("vstdir", legacy);
		QFile file(configPath); QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write(doc.toByteArray()) > 0); file.close();
		config->loadConfigFile(configPath);
		QCOMPARE(config->vstDir(), legacy + "/"); QVERIFY(!QDir(legacy).exists());
		QString error; auto roots = config->vstScanRoots(&error); QVERIFY(error.isEmpty()); QVERIFY(!roots.empty());
		QCOMPARE(roots.front().path, legacy);
		QVERIFY(!config->value("vst", "scanroots").isEmpty());
		const auto oldReference = PathUtil::toAbsolute("uservst:old.dll");
		QCOMPARE(QDir::cleanPath(oldReference), legacy + "/old.dll");
		QVERIFY(config->setVstScanRoots({{directory.path() + "/new", {"vst3"}, false, false},
			{directory.path() + "/other"}}, &error));
		roots = config->vstScanRoots(); std::reverse(roots.begin(), roots.end());
		QVERIFY(config->setVstScanRoots(roots));
		QCOMPARE(config->vstDir(), legacy + "/"); QCOMPARE(PathUtil::toAbsolute("uservst:old.dll"), oldReference);
		config->saveConfigFile(); config->loadConfigFile(configPath);
		QVERIFY(config->vstScanRoots() == roots); QCOMPARE(config->vstDir(), legacy + "/");
		QVERIFY(config->setVstScanRoots({})); config->saveConfigFile(); config->loadConfigFile(configPath);
		QVERIFY(config->vstScanRoots().empty()); QCOMPARE(config->userVstDir(), legacy + "/");
		config->setValue("vst", "scanroots", "{broken");
		QVERIFY(config->vstScanRoots(&error).empty()); QVERIFY(!error.isEmpty());
		config->saveConfigFile(); config->loadConfigFile(configPath);
		QCOMPARE(config->value("vst", "scanroots"), QString("{broken"));
		QVERIFY(config->vstScanRoots(&error).empty()); QVERIFY(!error.isEmpty());
		QCOMPARE(config->vstDir(), legacy + "/");
	}
};

QTEST_GUILESS_MAIN(VstPathsMigrationTest)
#include "VstPathsMigrationTest.moc"
