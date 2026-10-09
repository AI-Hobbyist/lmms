#ifndef LMMS_VSTHOST_SCAN_ROOTS_WIDGET_H
#define LMMS_VSTHOST_SCAN_ROOTS_WIDGET_H

#include <QCoreApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "vsthost/ScanRoots.h"

namespace lmms::gui {
// Translate fixed local validation messages only at the display boundary.
// Stored configuration and validation diagnostics retain their original values.
inline QString scanRootDisplayError(const QString& error)
{
	static const char* const messages[]{
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "Too many VST scan roots (maximum 256)"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "VST scan roots must be absolute paths without NUL characters"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "Invalid VST scan format selection"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "VST scan formats must be unique vst2/vst3 entries"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "VST scan roots exceed configuration size limit"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "Invalid VST scan roots JSON"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "Unsupported VST scan roots schema"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "Invalid VST scan root entry"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "Invalid VST scan root fields"),
		QT_TRANSLATE_NOOP("lmms::gui::ScanRootsWidget", "Invalid VST scan root format")};
	for (const auto* message : messages)
	{
		if (error == QLatin1String(message))
		{
			return QCoreApplication::translate("lmms::gui::ScanRootsWidget", message);
		}
	}
	return error;
}

inline QString catalogDisplayDiagnostic(const QString& source)
{
	static const char* const messages[]{QT_TRANSLATE_NOOP("VstHostUI", "Invalid catalog cache records"),
		QT_TRANSLATE_NOOP("VstHostUI", "Supervised catalog publication failed"),
		QT_TRANSLATE_NOOP("VstHostUI", "Invalid catalog publication reply"),
		QT_TRANSLATE_NOOP("VstHostUI", "Cannot create catalog cache directory"),
		QT_TRANSLATE_NOOP("VstHostUI", "Catalog cache writer is busy"),
		QT_TRANSLATE_NOOP("VstHostUI", "Catalog cache exceeds size limit"),
		QT_TRANSLATE_NOOP("VstHostUI", "discovery capacity"),
		QT_TRANSLATE_NOOP("VstHostUI", "catalog worker exception")};
	for (const auto* message : messages)
	{
		if (source == QLatin1String(message)) { return QCoreApplication::translate("VstHostUI", message); }
	}
	if (source.startsWith("Supervised catalog cache read failed: "))
	{
		return QCoreApplication::translate("VstHostUI", "Supervised catalog cache read failed: %1").arg(source.mid(38));
	}
	if (source.startsWith("Catalog cache schema/host mismatch or invalid JSON: "))
	{
		return QCoreApplication::translate("VstHostUI", "Catalog cache schema/host mismatch or invalid JSON: %1")
			.arg(source.mid(52));
	}
	if (source.startsWith("Cannot atomically save catalog cache: "))
	{
		return QCoreApplication::translate("VstHostUI", "Cannot atomically save catalog cache: %1").arg(source.mid(38));
	}
	return source;
}

// Edits a local draft. Configuration is committed only by the settings dialog.
// No filesystem probes are performed while displaying or validating paths.
class ScanRootsWidget : public QWidget
{
public:
	explicit ScanRootsWidget(
		const std::vector<vsthost::ScanRoot>& roots, const QString& loadError = {}, QWidget* parent = nullptr)
		: QWidget(parent)
	{
		auto* layout = new QVBoxLayout(this);
		m_table = new QTableWidget(0, 5, this);
		m_table->setObjectName("vstScanRoots");
		m_table->setHorizontalHeaderLabels({QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Directory"),
			QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Enabled"),
			QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Recursive"),
			QCoreApplication::translate("lmms::gui::ScanRootsWidget", "VST2"),
			QCoreApplication::translate("lmms::gui::ScanRootsWidget", "VST3")});
		m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		for (int column = 1; column < 5; ++column)
		{
			m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
		}
		m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
		m_table->setSelectionMode(QAbstractItemView::SingleSelection);
		m_table->setMinimumHeight(180);
		layout->addWidget(m_table);
		auto* buttons = new QHBoxLayout;
		auto button = [&](const QString& caption, const char* name) {
			auto* result = new QPushButton(caption, this);
			result->setObjectName(name);
			buttons->addWidget(result);
			return result;
		};
		m_add = button(QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Add"), "vstRootAdd");
		m_browse = button(QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Browse"), "vstRootBrowse");
		m_remove = button(QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Remove"), "vstRootRemove");
		m_up = button(QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Up"), "vstRootUp");
		m_down = button(QCoreApplication::translate("lmms::gui::ScanRootsWidget", "Down"), "vstRootDown");
		layout->addLayout(buttons);
		auto* help
			= new QLabel(QCoreApplication::translate("lmms::gui::ScanRootsWidget",
							 "Edit paths directly to include unavailable directories. Duplicate paths keep the first "
							 "row's options. This list does not change the directory used by older projects."),
				this);
		help->setWordWrap(true);
		layout->addWidget(help);
		m_error = new QLabel(scanRootDisplayError(loadError), this);
		m_error->setObjectName("vstRootError");
		m_error->setWordWrap(true);
		m_error->setVisible(!loadError.isEmpty());
		layout->addWidget(m_error);
		for (const auto& root : roots)
		{
			append(root);
		}
		connect(m_table, &QTableWidget::itemChanged, this, [this] { m_changed = true; });
		connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
		connect(m_add, &QPushButton::clicked, this, [this] {
			if (m_table->rowCount() >= 256)
			{
				return;
			}
			append({});
			m_changed = true;
			m_table->setCurrentCell(m_table->rowCount() - 1, 0);
			m_table->editItem(m_table->item(m_table->rowCount() - 1, 0));
			updateButtons();
		});
		connect(m_browse, &QPushButton::clicked, this, [this] {
			const auto row = m_table->currentRow();
			if (row < 0)
			{
				return;
			}
			const auto path = QFileDialog::getExistingDirectory(this,
				QCoreApplication::translate("lmms::gui::ScanRootsWidget", "VST scan directory"),
				m_table->item(row, 0)->text());
			if (!path.isEmpty())
			{
				m_table->item(row, 0)->setText(QDir::toNativeSeparators(path));
			}
		});
		connect(m_remove, &QPushButton::clicked, this, [this] {
			const auto row = m_table->currentRow();
			if (row < 0)
			{
				return;
			}
			m_table->removeRow(row);
			m_changed = true;
			updateButtons();
		});
		connect(m_up, &QPushButton::clicked, this, [this] { move(-1); });
		connect(m_down, &QPushButton::clicked, this, [this] { move(1); });
		updateButtons();
	}
	bool changed() const noexcept { return m_changed; }
	bool roots(std::vector<vsthost::ScanRoot>& result, QString& error) const
	{
		std::vector<vsthost::ScanRoot> draft;
		for (int row = 0; row < m_table->rowCount(); ++row)
		{
			auto checked = [&](int column) { return m_table->item(row, column)->checkState() == Qt::Checked; };
			vsthost::ScanRoot root{m_table->item(row, 0)->text(), {}, checked(2), checked(1)};
			if (checked(3))
			{
				root.formats.append("vst2");
			}
			if (checked(4))
			{
				root.formats.append("vst3");
			}
			draft.push_back(std::move(root));
		}
		return vsthost::normalizeScanRoots(draft, result, error);
	}
	void showError(const QString& error)
	{
		m_error->setText(scanRootDisplayError(error));
		m_error->setVisible(!error.isEmpty());
	}

private:
	void append(const vsthost::ScanRoot& root)
	{
		const QSignalBlocker blocker(m_table);
		const auto row = m_table->rowCount();
		m_table->insertRow(row);
		m_table->setItem(row, 0, new QTableWidgetItem(QDir::toNativeSeparators(root.path)));
		const bool values[]{root.enabled, root.recursive, root.formats.contains("vst2"), root.formats.contains("vst3")};
		for (int column = 1; column < 5; ++column)
		{
			auto* item = new QTableWidgetItem;
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
			item->setCheckState(values[column - 1] ? Qt::Checked : Qt::Unchecked);
			m_table->setItem(row, column, item);
		}
	}
	void move(int delta)
	{
		const auto row = m_table->currentRow(), target = row + delta;
		if (row < 0 || target < 0 || target >= m_table->rowCount())
		{
			return;
		}
		const QSignalBlocker blocker(m_table);
		for (int column = 0; column < 5; ++column)
		{
			auto* item = m_table->takeItem(row, column);
			m_table->setItem(row, column, m_table->takeItem(target, column));
			m_table->setItem(target, column, item);
		}
		m_table->setCurrentCell(target, 0);
		m_changed = true;
		updateButtons();
	}
	void updateButtons()
	{
		const auto row = m_table->currentRow();
		m_add->setEnabled(m_table->rowCount() < 256);
		m_browse->setEnabled(row >= 0);
		m_remove->setEnabled(row >= 0);
		m_up->setEnabled(row > 0);
		m_down->setEnabled(row >= 0 && row + 1 < m_table->rowCount());
	}
	QTableWidget* m_table;
	QLabel* m_error;
	QPushButton *m_add, *m_browse, *m_remove, *m_up, *m_down;
	bool m_changed = false;
};
} // namespace lmms::gui
#endif
