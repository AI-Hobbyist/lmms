#include "agent/chat/ChatGenerationPage.h"

#include <QDoubleSpinBox>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

#include "ToolButton.h"

namespace lmms::agent::chat {
namespace {

QRect pageRect(QRect rectangle)
{ return rectangle.translated(-232, -66); }

QLabel* label(QWidget* parent, const QString& text, QRect rectangle, int pixels = 13)
{
	auto* result = new QLabel(text, parent);
	result->setGeometry(pageRect(rectangle));
	auto font = result->font();
	font.setPixelSize(pixels);
	result->setFont(font);
	return result;
}

void card(QWidget* parent, QRect rectangle)
{
	auto* frame = new QFrame(parent);
	frame->setProperty("agentSettingsCard", true);
	frame->setGeometry(pageRect(rectangle));
}

const QStringList Levels{"auto", "off", "low", "medium", "high", "xhigh"};

} // namespace

ChatGenerationPage::ChatGenerationPage(QWidget* parent)
	: QWidget(parent)
{
	setObjectName("agentGenerationPage");
	label(this, tr("Generation parameters"), {280, 101, 700, 39}, 25);
	label(this, tr("Set default reasoning and temperature for the selected connection."), {280, 145, 1190, 21});
	card(this, {280, 208, 850, 623});
	label(this, tr("Reasoning level"), {306, 232, 650, 27}, 17);
	auto* badge = label(this, tr("Model capability"), {1007, 232, 100, 26}, 11);
	badge->setProperty("agentSettingsBadge", true);
	label(this, tr("Higher levels can help complex arrangement analysis and usually take longer."),
		  {306, 271, 770, 21});
	const QStringList captions{tr("Auto"), tr("Off"), tr("Low"), tr("Medium"), tr("High"), tr("Extra high")};
	for (int index = 0; index < Levels.size(); ++index)
	{
		auto* button = new gui::ToolButton(this);
		button->setObjectName("agentReasoning_" + Levels[index]);
		button->setProperty("agentChatControl", true);
		button->setText(captions[index]);
		button->setAccessibleName(captions[index]);
		button->setGeometry(pageRect({306 + index * 129, 324, 117, 43}));
		button->setCheckable(true);
		button->setAutoExclusive(true);
		m_levels.append(button);
		connect(button, &QToolButton::clicked, this, [this, index] {
			m_connection.reasoning = Levels[index];
			updatePreview();
		});
	}
	label(this, tr("Unsupported levels are disabled. Budget-based models use their configured budgets."),
		  {306, 389, 768, 18}, 11);
	card(this, {306, 441, 794, 1});
	label(this, tr("Temperature"), {306, 474, 600, 27}, 17);
	m_temperature = new QDoubleSpinBox(this);
	m_temperature->setObjectName("agentTemperature");
	m_temperature->setGeometry(pageRect({1007, 474, 66, 26}));
	m_temperature->setRange(0, 2);
	m_temperature->setDecimals(2);
	m_temperature->setSingleStep(0.05);
	m_temperature->setButtonSymbols(QAbstractSpinBox::NoButtons);
	label(this, tr("Lower values are steadier; higher values add variation."), {306, 513, 794, 21});
	m_slider = new QSlider(Qt::Horizontal, this);
	m_slider->setObjectName("agentTemperatureSlider");
	m_slider->setGeometry(pageRect({306, 565, 758, 43}));
	m_slider->setRange(0, 200);
	for (int index = 0; index < 5; ++index)
	{
		label(this, QString::number(index * 0.5), {312 + index * 183, 609, 40, 18}, 11);
	}
	connect(m_temperature, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		m_connection.temperature = value;
		const QSignalBlocker blocker(m_slider);
		m_slider->setValue(qRound(value * 100));
		updatePreview();
	});
	connect(m_slider, &QSlider::valueChanged, this, [this](int value) { m_temperature->setValue(value / 100.0); });
	card(this, {306, 667, 794, 96});
	label(this, tr("Temperature is unavailable when the model does not support it."), {353, 684, 730, 21});
	label(this, tr("Requests include only parameters supported by the model and protocol."), {353, 716, 730, 18}, 11);
	card(this, {1162, 208, 414, 380});
	label(this, tr("Current connection preview"), {1186, 232, 366, 25}, 16);
	label(this, tr("Model"), {1186, 281, 366, 18}, 11);
	m_model = label(this, {}, {1186, 309, 366, 22}, 14);
	card(this, {1186, 357, 366, 1});
	label(this, tr("Reasoning level"), {1186, 383, 230, 19}, 12);
	m_reasoningPreview = label(this, {}, {1422, 383, 130, 19}, 12);
	label(this, tr("Temperature"), {1186, 430, 230, 19}, 12);
	m_temperaturePreview = label(this, {}, {1422, 430, 130, 19}, 12);
	label(this, tr("Approval mode"), {1186, 477, 230, 19}, 12);
	m_approval = label(this, {}, {1422, 477, 130, 19}, 12);
	card(this, {1162, 614, 414, 217});
	label(this, tr("Capability adaptation"), {1225, 637, 320, 24}, 15);
	label(this,
		  tr("Reasoning levels, attachment types and temperature depend on the model. Only compatible settings are "
			 "retained."),
		  {1186, 685, 360, 65})
		->setWordWrap(true);
	auto* reset = new gui::ToolButton(this);
	reset->setObjectName("agentResetGeneration");
	reset->setProperty("agentChatControl", true);
	reset->setGeometry(pageRect({280, 868, 154, 36}));
	reset->setText(tr("Reset defaults"));
	connect(reset, &QToolButton::clicked, this, [this] {
		m_connection.reasoning = "auto";
		m_connection.temperature = 0.7;
		setConnection(m_connection, m_approval->property("requestApproval").toBool());
	});
	setConnection({}, true);
}

void ChatGenerationPage::setConnection(const ChatConnection& connection, bool requestApproval)
{
	m_connection = connection;
	const bool available = !connection.id.isEmpty();
	const auto supported = connection.capabilities.value("reasoningLevels").toArray();
	if (!Levels.contains(m_connection.reasoning)
		|| (m_connection.reasoning != "auto" && !supported.contains(m_connection.reasoning)))
	{
		m_connection.reasoning = "auto";
	}
	for (int index = 0; index < m_levels.size(); ++index)
	{
		m_levels[index]->setEnabled(available && (index == 0 || supported.contains(Levels[index])));
		m_levels[index]->setChecked(Levels[index] == m_connection.reasoning);
	}
	const bool temperature = available && connection.capabilities.value("temperature").toBool();
	m_temperature->setEnabled(temperature);
	m_slider->setEnabled(temperature);
	const QSignalBlocker blocker(m_temperature);
	m_temperature->setValue(m_connection.temperature);
	m_slider->setValue(qRound(m_connection.temperature * 100));
	m_model->setText(available ? connection.name + " / " + connection.model : tr("No connection selected"));
	m_approval->setProperty("requestApproval", requestApproval);
	m_approval->setText(requestApproval ? tr("Request approval") : tr("No approval"));
	updatePreview();
}

ChatConnection ChatGenerationPage::connection() const
{ return m_connection; }

void ChatGenerationPage::updatePreview()
{
	const int index = Levels.indexOf(m_connection.reasoning);
	m_reasoningPreview->setText(index >= 0 ? m_levels[index]->text() : tr("Auto"));
	m_temperaturePreview->setText(m_temperature->isEnabled() ? QString::number(m_connection.temperature)
															 : tr("Unavailable"));
}

} // namespace lmms::agent::chat
