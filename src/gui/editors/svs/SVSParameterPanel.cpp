#include "SVSParameterPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>

namespace lmms::gui {
SVSParameterPanel::SVSParameterPanel(QWidget* parent)
	: QWidget(parent)
	, m_layout(new QVBoxLayout(this))
{
	setObjectName("svsParameterPanel");
	m_layout->setContentsMargins(4, 4, 4, 4);
	m_layout->addStretch();
}
void SVSParameterPanel::submit(const QString& id, const QJsonValue& value)
{
	if (m_updating || !m_rows.contains(id))
		return;
	const auto& row = m_rows[id];
	if (row.descriptor.writable && row.descriptor.enabled && row.descriptor.accepts(value) && m_setter)
		m_setter(id, value);
}
void SVSParameterPanel::refresh(const QVector<svs::Parameter>& input, const QString& scope,
	const QVector<QJsonObject>& values, const QJsonObject& context, Setter setter)
{
	m_updating = true;
	m_setter = std::move(setter);
	QSet<QString> present;
	auto schema = input;
	std::stable_sort(schema.begin(), schema.end(),
		[](const auto& a, const auto& b) { return a.group == b.group ? a.order < b.order : a.group < b.group; });
	int order = 0;
	for (const auto& descriptor : schema)
	{
		if (descriptor.scope != scope)
			continue;
		const auto id = descriptor.id;
		present.insert(id);
		const auto editorType = descriptor.resourceIds.isEmpty() ? descriptor.type : "resource";
		auto found = m_rows.find(id);
		if (found != m_rows.end() && found->type != editorType)
		{
			delete found->container;
			m_rows.erase(found);
		}
		if (!m_rows.contains(id))
		{
			Row row;
			row.type = editorType;
			row.container = new QWidget(this);
			auto* layout = new QHBoxLayout(row.container);
			layout->setContentsMargins(0, 0, 0, 0);
			auto* label = new QLabel(row.container);
			label->setObjectName("parameterLabel");
			layout->addWidget(label);
			if (descriptor.type == "directory-list")
			{
				row.editor = new QWidget(row.container);
				auto* paths = new QVBoxLayout(row.editor);
				paths->setContentsMargins(0, 0, 0, 0);
			}
			else if (descriptor.type == "float" || descriptor.type == "int")
			{
				auto* editor = new QDoubleSpinBox(row.container);
				editor->setKeyboardTracking(false);
				row.editor = editor;
				connect(editor, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
					[this, id](double value) { submit(id, value); });
			}
			else if (descriptor.type == "bool")
			{
				auto* editor = new QCheckBox(row.container);
				row.editor = editor;
				connect(editor, &QCheckBox::clicked, this, [this, id, editor](bool value) {
					editor->setTristate(false);
					submit(id, value);
				});
			}
			else if (descriptor.type == "enum" || editorType == "resource")
			{
				auto* editor = new QComboBox(row.container);
				row.editor = editor;
				connect(editor, qOverload<int>(&QComboBox::activated), this,
					[this, id, editor](int index) { submit(id, editor->itemData(index).toString()); });
			}
			else
			{
				auto* editor = new QLineEdit(row.container);
				row.editor = editor;
				connect(editor, &QLineEdit::editingFinished, this, [this, id, editor] {
					if (editor->isModified())
					{
						submit(id, editor->text());
						editor->setModified(false);
					}
				});
			}
			row.editor->setObjectName("svsParameter." + scope + "." + id);
			layout->addWidget(row.editor);
			m_rows.insert(id, row);
		}
		auto& row = m_rows[id];
		row.descriptor = descriptor;
		row.value = svs::selectedValue(values, id);
		m_layout->removeWidget(row.container);
		m_layout->insertWidget(order++, row.container);
		row.container->findChild<QLabel*>("parameterLabel")->setText(descriptor.name.isEmpty() ? id : descriptor.name);
		row.container->setVisible(descriptor.isVisible(context));
		row.container->setToolTip(descriptor.disabledReason);
		row.editor->setEnabled(descriptor.writable && descriptor.enabled && !values.isEmpty());
		const bool editing
			= row.editor == QApplication::focusWidget() || row.editor->isAncestorOf(QApplication::focusWidget());
		// Keep the same widget and pending input across capability/context refreshes.
		if (editing)
			continue;
		const auto value = row.value.state == svs::ValueState::Common ? row.value.value : descriptor.defaultValue;
		const auto stateText = row.value.state == svs::ValueState::Mixed
			? QCoreApplication::translate("lmms::gui::SVSParameterPanel", "Mixed")
			: row.value.state == svs::ValueState::Unset
			? QCoreApplication::translate("lmms::gui::SVSParameterPanel", "Unset")
			: QString{};
		if (descriptor.type == "directory-list")
		{
			auto* paths = static_cast<QVBoxLayout*>(row.editor->layout());
			while (auto* item = paths->takeAt(0))
			{
				delete item->widget();
				delete item;
			}
			const auto values = value.toArray();
			for (int index = 0; index < values.size(); ++index)
			{
				auto* body = new QWidget(row.editor);
				auto* line = new QHBoxLayout(body);
				line->setContentsMargins(0, 0, 0, 0);
				auto* path = new QLineEdit(values[index].toString(), body);
				path->setObjectName("svsDirectoryPath");
				line->addWidget(path, 1);
				auto change = [this, id, index](const QString& path) {
					auto values = m_rows[id].value.value.toArray();
					if (index >= values.size())
						return;
					values[index] = path;
					submit(id, values);
				};
				connect(path, &QLineEdit::editingFinished, this, [path, change] {
					if (path->isModified())
					{
						change(path->text());
						path->setModified(false);
					}
				});
				auto* browse
					= new QPushButton(QCoreApplication::translate("lmms::gui::SVSParameterPanel", "Browse…"), body);
				browse->setObjectName("svsDirectoryBrowse");
				line->addWidget(browse);
				connect(browse, &QPushButton::clicked, this, [this, path, change] {
					const auto directory = QFileDialog::getExistingDirectory(this,
						QCoreApplication::translate("lmms::gui::SVSParameterPanel", "Voicebank directory"),
						path->text());
					if (!directory.isEmpty())
					{
						path->setText(directory);
						change(directory);
					}
				});
				auto* remove
					= new QPushButton(QCoreApplication::translate("lmms::gui::SVSParameterPanel", "Remove"), body);
				remove->setObjectName("svsDirectoryRemove");
				line->addWidget(remove);
				connect(remove, &QPushButton::clicked, this, [this, id, index, remove] {
					remove->clearFocus();
					auto values = m_rows[id].value.value.toArray();
					if (index < values.size())
					{
						values.removeAt(index);
						submit(id, values);
					}
				});
				paths->addWidget(body);
			}
			auto* add = new QPushButton(
				QCoreApplication::translate("lmms::gui::SVSParameterPanel", "Add directory"), row.editor);
			add->setObjectName("svsDirectoryAdd");
			add->setEnabled(values.size() < descriptor.maxItems);
			paths->addWidget(add);
			connect(add, &QPushButton::clicked, this, [this, id, add] {
				add->clearFocus();
				auto values = m_rows[id].value.value.toArray();
				values.append("");
				submit(id, values);
			});
		}
		else if (auto* editor = qobject_cast<QDoubleSpinBox*>(row.editor))
		{
			editor->setRange(descriptor.minimum, descriptor.maximum);
			editor->setSingleStep(descriptor.step);
			editor->setDecimals(descriptor.type == "int" ? 0 : 6);
			editor->setSuffix(descriptor.unit.isEmpty() ? QString{} : " " + descriptor.unit);
			editor->setValue(value.toDouble());
			editor->setToolTip(stateText
				+ (descriptor.scale == "log"
						? QCoreApplication::translate("lmms::gui::SVSParameterPanel", "; logarithmic scale")
						: QString{}));
			if (row.value.state != svs::ValueState::Common)
				if (auto* line = editor->findChild<QLineEdit*>())
				{
					line->clear();
					line->setPlaceholderText(stateText);
				}
		}
		else if (auto* editor = qobject_cast<QCheckBox*>(row.editor))
		{
			editor->setTristate(row.value.state != svs::ValueState::Common);
			editor->setCheckState(row.value.state != svs::ValueState::Common ? Qt::PartiallyChecked
					: value.toBool()										 ? Qt::Checked
																			 : Qt::Unchecked);
			editor->setText(stateText);
		}
		else if (auto* editor = qobject_cast<QComboBox*>(row.editor))
		{
			editor->clear();
			if (!descriptor.resourceIds.isEmpty())
				for (const auto& id : descriptor.resourceIds)
					editor->addItem(id, id);
			else
				for (const auto& choice : descriptor.choices)
				{
					auto item = choice.toObject();
					editor->addItem(item["name"].toString(item["id"].toString()), item["id"].toString());
				}
			editor->setCurrentIndex(
				row.value.state == svs::ValueState::Common ? editor->findData(value.toString()) : -1);
			editor->setPlaceholderText(stateText);
		}
		else if (auto* editor = qobject_cast<QLineEdit*>(row.editor))
		{
			editor->setText(row.value.state == svs::ValueState::Common ? value.toString() : QString{});
			editor->setPlaceholderText(stateText);
			editor->setModified(false);
		}
	}
	for (auto i = m_rows.begin(); i != m_rows.end(); ++i)
		if (!present.contains(i.key()))
			i->container->hide();
	m_updating = false;
}
}
