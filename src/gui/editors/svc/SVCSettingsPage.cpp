#include "SVCSettingsPage.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
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
	auto* reconnect = new QGroupBox(tr("Automatic reconnection (all SVC engines)"), this);
	auto* reconnectLayout = new QFormLayout(reconnect);
	const auto policy = svc::Catalog::instance().reconnectPolicy();
	m_retryInterval = new QSpinBox(reconnect);
	m_retryInterval->setObjectName("svcReconnectInterval");
	m_retryInterval->setRange(1, 86400);
	m_retryInterval->setSuffix(tr(" s"));
	m_retryInterval->setValue(policy.intervalSeconds);
	m_maximumRetries = new QSpinBox(reconnect);
	m_maximumRetries->setObjectName("svcReconnectRetries");
	m_maximumRetries->setRange(1, 1000);
	m_maximumRetries->setValue(policy.maximumRetries);
	reconnectLayout->addRow(tr("Reconnect interval"), m_retryInterval);
	reconnectLayout->addRow(tr("Maximum retries"), m_maximumRetries);
	auto* startReconnect = new QPushButton(tr("Start automatic reconnection"), reconnect);
	startReconnect->setObjectName("svcAutoReconnect");
	reconnectLayout->addRow(startReconnect);
	m_retryStatus = new QLabel(reconnect);
	m_retryStatus->setWordWrap(true);
	reconnectLayout->addRow(m_retryStatus);
	connect(startReconnect, &QPushButton::clicked, this, [this] {
		if (saveReconnectPolicy() && saveConnections()) { svc::Catalog::instance().reconnectDisconnected(); }
	});
	layout->addWidget(reconnect);
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
		if (!builtin)
		{
			auto* refresh = new QPushButton(tr("Test connection / refresh"), group);
			form->addRow(refresh);
			connect(refresh, &QPushButton::clicked, this, [entry] {
				entry.status->setText(svc::Catalog::instance().setConnection(
					entry.id, {entry.address->text(), entry.token->text(), entry.remember->isChecked()}));
			});
			const auto update = [entry] { entry.status->setText(svc::Catalog::instance().status(entry.id)); };
			connect(&svc::Catalog::instance(), &svc::Catalog::changed, this, update);
			update();
		}
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
	return saveReconnectPolicy() && saveConnections();
}

bool SVCSettingsPage::saveReconnectPolicy()
{
	m_retryStatus->setText(
		svc::Catalog::instance().setReconnectPolicy({m_retryInterval->value(), m_maximumRetries->value()}));
	return m_retryStatus->text().isEmpty();
}

bool SVCSettingsPage::saveConnections()
{
	bool saved = true;
	for (const auto& entry : m_entries)
	{
		const auto current = svc::Catalog::instance().connection(entry.id);
		if (current.address == entry.address->text() && current.token == entry.token->text()
			&& current.remembered == entry.remember->isChecked())
		{
			continue;
		}
		entry.status->setText(svc::Catalog::instance().setConnection(
			entry.id, {entry.address->text(), entry.token->text(), entry.remember->isChecked()}));
		saved &= entry.status->text().isEmpty();
	}
	return saved;
}
} // namespace lmms::gui
