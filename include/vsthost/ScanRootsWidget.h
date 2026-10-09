#ifndef LMMS_VSTHOST_SCAN_ROOTS_WIDGET_H
#define LMMS_VSTHOST_SCAN_ROOTS_WIDGET_H

#include "vsthost/ScanRoots.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lmms::gui {
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
		m_table->setHorizontalHeaderLabels({tr("Directory"), tr("Enabled"), tr("Recursive"), tr("VST2"), tr("VST3")});
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
		m_add = button(tr("Add"), "vstRootAdd");
		m_browse = button(tr("Browse"), "vstRootBrowse");
		m_remove = button(tr("Remove"), "vstRootRemove");
		m_up = button(tr("Up"), "vstRootUp");
		m_down = button(tr("Down"), "vstRootDown");
		layout->addLayout(buttons);
		auto* help = new QLabel(
			tr("Edit paths directly to include unavailable directories. Duplicate paths keep the first row's options. This list does not change the directory used by older projects."),
			this);
		help->setWordWrap(true);
		layout->addWidget(help);
		m_error = new QLabel(loadError, this);
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
			const auto path
				= QFileDialog::getExistingDirectory(this, tr("VST scan directory"), m_table->item(row, 0)->text());
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
		m_error->setText(error);
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
