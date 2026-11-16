#include "agent/chat/ChatAdvancedDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "ToolButton.h"

namespace lmms::agent::chat {
namespace {

QLabel* label(QWidget* parent, const QString& text, QRect rectangle, int pixels = 13)
{
	auto* result = new QLabel(text, parent);
	result->setGeometry(rectangle);
	auto font = result->font();
	font.setPixelSize(pixels);
	result->setFont(font);
	return result;
}

QFrame* card(QWidget* parent, QRect rectangle)
{
	auto* result = new QFrame(parent);
	result->setProperty("agentSettingsCard", true);
	result->setGeometry(rectangle);
	return result;
}

} // namespace

ChatAdvancedDialog::ChatAdvancedDialog(ChatConnection connection, QWidget* parent)
	: QDialog(parent)
	, m_connection(std::move(connection))
{
	setObjectName("agentAdvancedDialog");
	setWindowTitle(tr("Request format"));
	resize(1600, 1000);
	setMinimumSize(1000, 700);
	auto* scroll = new QScrollArea(this);
	scroll->setObjectName("agentAdvancedScroll");
	scroll->setFrameShape(QFrame::NoFrame);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(scroll);
	auto* page = new QWidget;
	page->setFixedSize(1600, 1000);
	scroll->setWidget(page);
	label(page, tr("Request format"), {40, 25, 700, 41}, 27);
	label(page,
		  tr("Provider presets are recommendations. Request formats remain editable; authentication uses API Keys."),
		  {40, 73, 1500, 21});
	card(page, {32, 128, 570, 814});
	card(page, {634, 128, 934, 814});
	const QStringList names{"OpenAI Chat Completions", "OpenAI Responses",	  "Anthropic Messages",
							"Google Gemini",		   "Azure OpenAI · Chat", "Azure OpenAI · Responses",
							tr("OpenAI compatible"),   tr("Custom HTTP JSON")};
	const QStringList descriptions{tr("Chat messages and tool calls"),
								   tr("Responses messages and tool calls"),
								   tr("Native Messages with an API Key header"),
								   tr("Native GenerateContent with an API Key"),
								   tr("Resource URL, deployment and API version"),
								   tr("Azure Responses with an API Key"),
								   tr("Gateways and self-hosted compatible services"),
								   tr("Custom paths, authentication and JSON mapping")};
	const auto protocols = ChatConnection::protocols();
	for (int index = 0; index < protocols.size(); ++index)
	{
		auto* choice = new gui::ToolButton(page);
		choice->setObjectName("agentAdvancedProtocol_" + protocols[index]);
		choice->setGeometry(45, 151 + index * 94, 544, 79);
		choice->setProperty("agentChatControl", true);
		choice->setCheckable(true);
		choice->setAutoExclusive(true);
		choice->setChecked(m_connection.protocol == protocols[index]);
		choice->setAccessibleName(names[index]);
		label(choice, names[index], {51, 12, 455, 24}, 16)->setAttribute(Qt::WA_TransparentForMouseEvents);
		label(choice, descriptions[index], {51, 44, 455, 18}, 11)->setAttribute(Qt::WA_TransparentForMouseEvents);
		connect(choice, &QToolButton::clicked, this, [this, protocol = protocols[index]] { selectProtocol(protocol); });
	}
	label(page, tr("Custom request configuration"), {660, 152, 878, 30}, 20);
	label(page, tr("Use advanced configuration only when needed. Standard presets need only an address and Key."),
		  {660, 191, 878, 40});
	label(page, tr("Authentication"), {660, 253, 878, 19}, 12);
	m_auth = new QComboBox(page);
	m_auth->setObjectName("agentKeyLocation");
	m_auth->setGeometry(660, 279, 878, 43);
	m_auth->addItem(tr("API Key / request header"), "header");
	m_auth->addItem(tr("API Key / query parameter"), "query");
	m_auth->addItem(tr("API Key / JSON body"), "body");
	m_auth->setCurrentIndex(qMax(0, m_auth->findData(m_connection.mapping.value("keyLocation").toString("header"))));
	label(page, tr("Key header or field name"), {660, 352, 419, 19}, 12);
	label(page, tr("Key prefix"), {1100, 352, 438, 19}, 12);
	m_header = new QLineEdit(m_connection.keyHeader, page);
	m_header->setObjectName("agentKeyHeader");
	m_header->setGeometry(660, 378, 419, 43);
	m_prefix = new QLineEdit(m_connection.keyPrefix, page);
	m_prefix->setObjectName("agentKeyPrefix");
	m_prefix->setGeometry(1100, 378, 438, 43);
	label(page, tr("Request path"), {660, 451, 878, 19}, 12);
	m_path = new QLineEdit(m_connection.mapping.value("path").toString(), page);
	m_path->setObjectName("agentRequestPath");
	m_path->setPlaceholderText(tr("Leave empty to use the native protocol path"));
	m_path->setGeometry(660, 477, 878, 43);
	card(page, {660, 558, 878, 163});
	label(page, tr("Request / response mapping and model capabilities"), {678, 574, 832, 22}, 14);
	const QJsonObject options{{"apiVersion", m_connection.apiVersion},
							  {"deployment", m_connection.deployment},
							  {"capabilities", m_connection.capabilities},
							  {"mapping", m_connection.mapping}};
	m_options = new QPlainTextEdit(QString::fromUtf8(QJsonDocument(options).toJson()), page);
	m_options->setObjectName("agentRequestMapping");
	m_options->setGeometry(678, 607, 832, 100);
	card(page, {660, 758, 878, 130});
	label(page, tr("Project tools always use LMMS internal interfaces"), {711, 775, 800, 22}, 14);
	label(page, tr("Configure model requests here, not MCP services. Do not put API Keys in mapping JSON."),
		  {679, 817, 833, 40}, 12)
		->setWordWrap(true);
	// Board 13 is static. Reuse the accepted settings-page confirmation rectangles
	// in its empty bottom margin so edits can be accepted or discarded explicitly.
	auto* buttons = new QDialogButtonBox(page);
	buttons->setGeometry(1328, 952, 248, 35);
	buttons->setStandardButtons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	buttons->layout()->setEnabled(false);
	buttons->button(QDialogButtonBox::Cancel)->setGeometry(0, 0, 104, 35);
	buttons->button(QDialogButtonBox::Save)->setGeometry(118, 0, 130, 35);
	buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
	buttons->button(QDialogButtonBox::Save)->setText(tr("Apply to draft"));
	connect(buttons, &QDialogButtonBox::accepted, this, &ChatAdvancedDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ChatConnection ChatAdvancedDialog::connection() const
{ return m_connection; }

void ChatAdvancedDialog::selectProtocol(const QString& protocol)
{
	m_connection.protocol = protocol;
	QString header = "Authorization";
	QString prefix = "Bearer ";
	if (protocol == "anthropic-messages")
	{
		header = "x-api-key";
		prefix.clear();
	}
	else if (protocol == "gemini" || protocol.startsWith("azure-"))
	{
		header = protocol == "gemini" ? "x-goog-api-key" : "api-key";
		prefix.clear();
	}
	m_header->setText(header);
	m_prefix->setText(prefix);
}

void ChatAdvancedDialog::accept()
{
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(m_options->toPlainText().toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
	{
		QMessageBox::warning(this, tr("Invalid JSON"), parseError.errorString());
		return;
	}
	const auto options = document.object();
	for (const auto& name : {"capabilities", "mapping"})
	{
		if (options.contains(name) && !options.value(name).isObject())
		{
			QMessageBox::warning(this, tr("Invalid JSON"), tr("%1 must be a JSON object.").arg(name));
			return;
		}
	}
	for (const auto& name : {"apiVersion", "deployment"})
	{
		if (options.contains(name) && !options.value(name).isString())
		{
			QMessageBox::warning(this, tr("Invalid JSON"), tr("%1 must be a string.").arg(name));
			return;
		}
	}
	auto draft = m_connection;
	draft.apiVersion = options.value("apiVersion").toString();
	draft.deployment = options.value("deployment").toString();
	draft.capabilities = options.value("capabilities").toObject();
	draft.mapping = options.value("mapping").toObject();
	draft.mapping.insert("path", m_path->text().trimmed());
	draft.mapping.insert("keyLocation", m_auth->currentData().toString());
	draft.keyHeader = m_header->text().trimmed();
	draft.keyPrefix = m_prefix->text();
	QString error;
	if (!draft.validate(error))
	{
		QMessageBox::warning(this, tr("Invalid connection"), error);
		return;
	}
	m_connection = std::move(draft);
	QDialog::accept();
}

} // namespace lmms::agent::chat
