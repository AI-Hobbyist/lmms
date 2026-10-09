#include "SVCSettingsPage.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <array>

#include "Knob.h"
#include "SVCCatalog.h"
namespace lmms::gui {
SVCSettingsPage::SVCSettingsPage(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
	auto* help
		= new QLabel(tr("Configure each SVC engine independently. Leave Bearer token empty for a local server without "
						"authentication. Tokens stay in memory unless saved in Windows Credential Manager."),
			this);
	help->setWordWrap(true);
	layout->addWidget(help);
	auto* defaults = new QGroupBox(tr("Defaults for new SVC tracks"), this);
	auto* defaultsLayout = new QFormLayout(defaults);
	const auto config = svc::chunkDefaults();
	const std::array<double, 3> values{
		config.silenceThresholdDbfs, config.lengthThresholdSeconds, config.forcedChunkSeconds};
	const QStringList names{tr("Silence threshold"), tr("Length threshold"), tr("Forced length")};
	for (int index = 0; index < 3; ++index)
	{
		auto model = std::make_unique<FloatModel>(values[index], index == 0 ? -120 : .001, index == 0 ? 0 : 86400,
			index == 0 ? 1 : .001, nullptr, names[index]);
		auto* row = new QWidget(defaults);
		auto* body = new QHBoxLayout(row);
		body->setContentsMargins(0, 0, 0, 0);
		auto* knob = new Knob(KnobType::Small17, row);
		knob->setModel(model.get());
		const auto unit = index == 0 ? QString("dBFS") : QString("s");
		knob->setUnit(unit);
		auto* value = new QLabel(row);
		const auto update = [model = model.get(), value, unit] {
			value->setText(QString::number(model->value(), 'g', 7) + " " + unit);
		};
		update();
		connect(model.get(), &FloatModel::dataChanged, this, update);
		body->addWidget(knob);
		body->addWidget(value);
		defaultsLayout->addRow(names[index], row);
		m_defaults.push_back(std::move(model));
	}
	m_defaultStatus = new QLabel(defaults);
	defaultsLayout->addRow(m_defaultStatus);
	layout->addWidget(defaults);
	for (const auto& engine : svc::Catalog::instance().engines())
	{
		const auto connection = svc::Catalog::instance().connection(engine.id);
		auto* group = new QGroupBox(engine.name, this);
		auto* form = new QFormLayout(group);
		Entry entry{engine.id, new QLineEdit(connection.address, group), new QLineEdit(connection.token, group),
			new QCheckBox(tr("Save token in Windows Credential Manager"), group), new QLabel(group)};
		entry.token->setEchoMode(QLineEdit::Password);
		entry.remember->setChecked(connection.remembered);
		entry.address->setObjectName("svcAddress_" + engine.id);
		entry.token->setObjectName("svcToken_" + engine.id);
		const auto builtin = engine.defaultAddress.startsWith("builtin:");
		entry.address->setReadOnly(builtin);
		entry.token->setEnabled(!builtin);
		entry.remember->setEnabled(!builtin);
		form->addRow(tr("API address"), entry.address);
		form->addRow(tr("Bearer token (optional)"), entry.token);
		form->addRow(entry.remember);
		form->addRow(entry.status);
		layout->addWidget(group);
		m_entries.push_back(entry);
	}
	layout->addStretch();
}
bool SVCSettingsPage::save()
{
	m_defaultStatus->setText(
		svc::setChunkDefaults({m_defaults[0]->value(), m_defaults[1]->value(), m_defaults[2]->value()}));
	if (!m_defaultStatus->text().isEmpty()) { return false; }
	bool saved = true;
	for (const auto& entry : m_entries)
	{
		entry.status->setText(svc::Catalog::instance().setConnection(
			entry.id, {entry.address->text(), entry.token->text(), entry.remember->isChecked()}));
		saved &= entry.status->text().isEmpty();
	}
	return saved;
}
} // namespace lmms::gui
