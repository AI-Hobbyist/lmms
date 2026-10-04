#include "vsthost/ScanRootsWidget.h"
#include <QtTest>
#include <QLineEdit>

using namespace lmms::gui;
using namespace lmms::vsthost;

class ScanRootsWidgetTest : public QObject
{
	Q_OBJECT
private slots:
	void keyboardPathEditing()
	{
		ScanRootsWidget widget({}); widget.show();
		widget.findChild<QPushButton*>("vstRootAdd")->click();
		auto* table = widget.findChild<QTableWidget*>("vstScanRoots");
		auto* editor = table->findChild<QLineEdit*>(); QVERIFY(editor);
		QTest::keyClicks(editor, "C:/Unavailable/VST3"); QTest::keyClick(editor, Qt::Key_Return);
		QTRY_COMPARE(table->item(0, 0)->text(), QString("C:/Unavailable/VST3"));
		std::vector<ScanRoot> roots; QString error;
		QVERIFY(widget.roots(roots, error)); QCOMPARE(roots.front().path, QString("C:/Unavailable/VST3"));
		QVERIFY(widget.changed());
	}
	void draftOrderAndFlags()
	{
		const std::vector<ScanRoot> initial{{QString::fromUtf16(u"C:/missing/音楽"), {"vst3"}, false, true},
			{"D:/second", {"vst2"}, true, false}};
		ScanRootsWidget widget(initial);
		QVERIFY(!widget.changed());
		std::vector<ScanRoot> roots; QString error;
		QVERIFY(widget.roots(roots, error)); QVERIFY(roots == initial);
		auto* table = widget.findChild<QTableWidget*>("vstScanRoots"); QVERIFY(table);
		table->setCurrentCell(1, 0);
		widget.findChild<QPushButton*>("vstRootUp")->click();
		QVERIFY(widget.changed()); QVERIFY(widget.roots(roots, error));
		QVERIFY(roots[0] == initial[1] && roots[1] == initial[0]);
		QVERIFY(!widget.findChild<QPushButton*>("vstRootUp")->isEnabled());
		widget.findChild<QPushButton*>("vstRootDown")->click();
		QVERIFY(widget.roots(roots, error)); QVERIFY(roots == initial);
		table->item(0, 1)->setCheckState(Qt::Unchecked);
		table->item(0, 2)->setCheckState(Qt::Checked);
		table->item(0, 3)->setCheckState(Qt::Checked);
		QVERIFY(widget.roots(roots, error));
		QVERIFY(!roots[0].enabled && roots[0].recursive && roots[0].formats == QStringList({"vst2", "vst3"}));
		QVERIFY(initial[0].enabled && !initial[0].recursive); // Source snapshot is untouched.
	}
	void addValidateDeduplicateRemove()
	{
		ScanRootsWidget widget({{"C:/first"}});
		auto* table = widget.findChild<QTableWidget*>("vstScanRoots");
		auto* add = widget.findChild<QPushButton*>("vstRootAdd");
		add->click(); QVERIFY(table->rowCount() == 2);
		std::vector<ScanRoot> roots; QString error;
		QVERIFY(!widget.roots(roots, error)); QVERIFY(!error.isEmpty());
		table->item(1, 0)->setText("relative/path"); QVERIFY(!widget.roots(roots, error));
		table->item(1, 0)->setText("C:/FIRST"); table->item(1, 1)->setCheckState(Qt::Unchecked);
		QVERIFY(widget.roots(roots, error)); QVERIFY(roots.size() == 1 && roots[0].enabled);
		table->item(1, 0)->setText(QString::fromUtf16(u"\\\\server\\share\\未接続"));
		QVERIFY(widget.roots(roots, error)); QVERIFY(roots.size() == 2);
		table->item(1, 3)->setCheckState(Qt::Unchecked); table->item(1, 4)->setCheckState(Qt::Unchecked);
		QVERIFY(!widget.roots(roots, error));
		table->item(1, 4)->setCheckState(Qt::Checked); QVERIFY(widget.roots(roots, error));
		widget.findChild<QPushButton*>("vstRootRemove")->click(); QVERIFY(table->rowCount() == 1);
		table->setCurrentCell(0, 0); widget.findChild<QPushButton*>("vstRootRemove")->click();
		QVERIFY(widget.roots(roots, error)); QVERIFY(roots.empty());
		QVERIFY(!widget.findChild<QPushButton*>("vstRootBrowse")->isEnabled());
	}
	void preserveInvalidConfigurationAndCapacity()
	{
		ScanRootsWidget invalid({}, "Invalid scan roots JSON");
		QVERIFY(!invalid.changed());
		QCOMPARE(invalid.findChild<QLabel*>("vstRootError")->text(), QString("Invalid scan roots JSON"));
		std::vector<ScanRoot> source;
		for (int i = 0; i < 256; ++i) { source.push_back({"C:/root/" + QString::number(i)}); }
		ScanRootsWidget full(source);
		QVERIFY(!full.findChild<QPushButton*>("vstRootAdd")->isEnabled());
		QVERIFY(!full.changed());
	}
};
QTEST_MAIN(ScanRootsWidgetTest)
#include "ScanRootsWidgetTest.moc"
