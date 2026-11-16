#include "agent/chat/ChatApiSettingsPage.h"

#include <QComboBox>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUrlQuery>
#include <iterator>

#include "ToolButton.h"
#include "agent/chat/ChatAdvancedDialog.h"
#include "embed.h"

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

QToolButton* button(QWidget* parent, const QString& text, QRect rectangle, std::string_view icon = {})
{
	auto* result = new gui::ToolButton(parent);
	result->setGeometry(pageRect(rectangle));
	result->setText(text);
	result->setAccessibleName(text);
	result->setProperty("agentChatControl", true);
	if (!icon.empty())
	{
		result->setIcon(embed::getIconPixmap(icon, 18, 18));
		result->setIconSize({18, 18});
	}
	result->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	return result;
}

struct Preset
{
	const char* id;
	const char* name;
	const char* url;
	const char* protocol;
	const char* group;
};

void setProtocolAuthentication(ChatConnection& connection)
{
	connection.keyHeader = "Authorization";
	connection.keyPrefix = "Bearer ";
	if (connection.protocol == "anthropic-messages")
	{
		connection.keyHeader = "x-api-key";
		connection.keyPrefix.clear();
	}
	else if (connection.protocol == "gemini" || connection.protocol.startsWith("azure-"))
	{
		connection.keyHeader = connection.protocol == "gemini" ? "x-goog-api-key" : "api-key";
		connection.keyPrefix.clear();
	}
}

// Addresses/protocol facts only; source version is recorded in chat-provider-sources.md.
const Preset presets[]{
	{"deepseek", "DeepSeek", "https://api.deepseek.com", "openai-chat", "cn"},
	{"openai", "OpenAI", "https://api.openai.com/v1", "openai-responses", "international"},
	{"anthropic", "Anthropic", "https://api.anthropic.com/v1", "anthropic-messages", "international"},
	{"gemini", "Google Gemini", "https://generativelanguage.googleapis.com/v1beta", "gemini", "international"},
	{"dashscope", QT_TRANSLATE_NOOP("lmms::agent::chat::ChatApiSettingsPage", "Bailian"),
	 "https://maas.qianwenaiapi.com/compatible-mode/v1", "openai-compatible", "cn"},
	{"silicon", QT_TRANSLATE_NOOP("lmms::agent::chat::ChatApiSettingsPage", "SiliconFlow"),
	 "https://api.siliconflow.cn/v1", "openai-compatible", "cn"},
	{"zhipu", QT_TRANSLATE_NOOP("lmms::agent::chat::ChatApiSettingsPage", "ZhiPu AI"),
	 "https://open.bigmodel.cn/api/paas/v4", "openai-compatible", "cn"},
	{"moonshot", QT_TRANSLATE_NOOP("lmms::agent::chat::ChatApiSettingsPage", "Moonshot"), "https://api.moonshot.cn/v1",
	 "openai-compatible", "cn"},
	{"minimax", "MiniMax", "https://api.minimaxi.com/v1", "openai-compatible", "cn"},
	{"stepfun", QT_TRANSLATE_NOOP("lmms::agent::chat::ChatApiSettingsPage", "StepFun"), "https://api.stepfun.com/v1",
	 "openai-compatible", "cn"},
	{"doubao", QT_TRANSLATE_NOOP("lmms::agent::chat::ChatApiSettingsPage", "Volcengine"),
	 "https://ark.cn-beijing.volces.com/api/v3", "openai-compatible", "cn"},
	{"hunyuan", QT_TRANSLATE_NOOP("lmms::agent::chat::ChatApiSettingsPage", "Tencent Hunyuan"),
	 "https://api.hunyuan.cloud.tencent.com/v1", "openai-compatible", "cn"},
	{"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", "openai-compatible", "compatible"},
	{"groq", "Groq", "https://api.groq.com/openai/v1", "openai-compatible", "international"},
	{"nvidia", "NVIDIA NIM", "https://integrate.api.nvidia.com/v1", "openai-compatible", "international"},
	{"azure", "Azure OpenAI", "", "azure-chat", "international"}};

} // namespace

ChatApiSettingsPage::ChatApiSettingsPage(ChatConnectionStore& store, QWidget* parent, const QString& selectedId)
	: QWidget(parent)
	, m_store(store)
{
	setObjectName("agentApiSettingsPage");
	m_network = new QNetworkAccessManager(this);
	m_connections = m_store.connections(m_loadError);
	m_editor = new QWidget(this);
	m_editor->setGeometry(0, 0, 1368, 874);
	buildConnections();
	buildPresets();
	refreshConnections();
	if (!m_connections.isEmpty())
	{
		auto id = m_connections.first().id;
		for (const auto& connection : m_connections)
		{
			if (connection.id == selectedId)
			{
				id = selectedId;
				break;
			}
		}
		selectConnection(id);
	}
	else
	{
		m_editor->hide();
		m_presets->show();
	}
	if (!m_loadError.isEmpty())
	{
		m_status->setText(m_loadError);
	}
}

void ChatApiSettingsPage::buildConnections()
{
	auto* panel = new QFrame(m_editor);
	panel->setProperty("agentSettingsCard", true);
	panel->setGeometry(pageRect({248, 90, 244, 826}));
	label(m_editor, tr("Model connections"), {264, 113, 200, 24}, 16);
	auto* add = button(m_editor, tr("Add provider preset"), {264, 160, 212, 36}, "agent_chat_new");
	add->setObjectName("agentAddProvider");
	connect(add, &QToolButton::clicked, this, [this] {
		captureFields();
		m_editor->hide();
		m_presets->show();
	});
	m_search = new QLineEdit(m_editor);
	m_search->setGeometry(pageRect({264, 211, 212, 36}));
	m_search->setPlaceholderText(tr("Search connections"));
	m_list = new QListWidget(m_editor);
	m_list->setObjectName("agentConnectionList");
	m_list->setGeometry(pageRect({260, 301, 220, 552}));
	m_list->setFrameShape(QFrame::NoFrame);
	m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_list->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_list, &QListWidget::customContextMenuRequested, this, &ChatApiSettingsPage::connectionMenu);
	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row >= 0)
		{
			selectConnection(m_list->item(row)->data(Qt::UserRole).toString());
		}
	});
	connect(m_search, &QLineEdit::textChanged, this, &ChatApiSettingsPage::refreshConnections);
	label(m_editor, tr("Presets can be edited, duplicated and deleted"), {264, 873, 220, 30}, 10)->setWordWrap(true);
	auto* providerIcon = label(m_editor, {}, {530, 109, 28, 28});
	providerIcon->setPixmap(embed::getIconPixmap("agent_chat_key", 28, 28));
	m_title = label(m_editor, tr("Model connection"), {574, 100, 598, 42}, 27);
	m_title->setObjectName("agentProviderTitle");
	label(m_editor, tr("Connect with an API address and Key. No provider account login is needed."),
		  {530, 151, 642, 40});
	const auto field = [this](const QString& name, int y, const char* objectName, bool password = false) {
		label(m_editor, name, {530, y - 26, 642, 19}, 12);
		auto* input = new QLineEdit(m_editor);
		input->setGeometry(pageRect({530, y, 642, 43}));
		input->setObjectName(objectName);
		input->setProperty("agentApiInput", true);
		if (password)
		{
			input->setEchoMode(QLineEdit::Password);
		}
		return input;
	};
	m_name = field(tr("Connection name"), 231, "agentConnectionName");
	label(m_editor, tr("Request format"), {530, 298, 642, 19}, 12);
	m_protocol = new QComboBox(m_editor);
	m_protocol->setObjectName("agentRequestFormat");
	m_protocol->setGeometry(pageRect({530, 324, 642, 43}));
	const QStringList formats{"OpenAI Chat Completions", "OpenAI Responses",	"Anthropic Messages",
							  "Google Gemini",			 "Azure OpenAI · Chat", "Azure OpenAI · Responses",
							  tr("OpenAI compatible"),	 tr("Custom HTTP JSON")};
	const auto protocols = ChatConnection::protocols();
	for (int index = 0; index < protocols.size(); ++index)
	{
		m_protocol->addItem(formats[index], protocols[index]);
	}
	label(m_editor, tr("Choose the format supported by the provider and model."), {532, 375, 640, 18}, 11);
	m_url = field(tr("API address"), 442, "agentApiAddress");
	m_key = field(tr("API Key"), 539, "agentApiKey", true);
	label(m_editor, tr("Model ID"), {530, 610, 642, 19}, 12);
	m_model = new QComboBox(m_editor);
	m_model->setObjectName("agentModelId");
	m_model->setEditable(true);
	m_model->setGeometry(pageRect({530, 636, 506, 43}));
	auto* fetch = button(m_editor, tr("Get models"), {1050, 636, 122, 43}, "agent_chat_reload");
	fetch->setObjectName("agentGetModels");
	connect(fetch, &QToolButton::clicked, this, &ChatApiSettingsPage::fetchModels);
	label(m_editor, tr("You can also enter a model ID manually."), {532, 691, 640, 18}, 11);
	auto* advanced = button(m_editor, tr("Advanced connection parameters"), {530, 748, 642, 92}, "agent_chat_sliders");
	advanced->setObjectName("agentAdvancedConnection");
	connect(advanced, &QToolButton::clicked, this, &ChatApiSettingsPage::editAdvanced);
	auto* overview = new QFrame(m_editor);
	overview->setProperty("agentSettingsCard", true);
	overview->setGeometry(pageRect({1204, 205, 372, 635}));
	label(m_editor, tr("Connection overview"), {1226, 226, 328, 24}, 16);
	label(m_editor, tr("Authentication"), {1226, 321, 124, 19}, 12);
	label(m_editor, tr("API Key"), {1350, 321, 200, 19}, 12);
	label(m_editor, tr("Streaming output"), {1226, 367, 124, 19}, 12);
	label(m_editor, tr("Enabled"), {1350, 367, 200, 19}, 12);
	label(m_editor, tr("Project tools"), {1226, 413, 124, 19}, 12);
	label(m_editor, tr("Internal interfaces"), {1350, 413, 200, 19}, 12);
	label(m_editor, tr("Request preview"), {1226, 492, 328, 19}, 12);
	m_preview = label(m_editor, {}, {1226, 532, 328, 204}, 12);
	m_preview->setObjectName("agentApiRequestPreview");
	m_preview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_preview->setWordWrap(true);
	auto* test = button(m_editor, tr("Test connection"), {530, 869, 130, 36}, "agent_chat_play");
	test->setObjectName("agentTestConnection");
	connect(test, &QToolButton::clicked, this, &ChatApiSettingsPage::fetchModels);
	m_status = label(m_editor, tr("Not tested"), {679, 869, 875, 45}, 11);
	m_status->setObjectName("agentModelStatus");
	m_status->setWordWrap(true);
}

void ChatApiSettingsPage::buildPresets()
{
	m_presets = new QWidget(this);
	m_presets->setGeometry(0, 0, 1368, 874);
	m_presets->hide();
	label(m_presets, tr("Provider presets"), {280, 101, 600, 39}, 25);
	label(m_presets, tr("Select a preset, then enter the API address and Key. Formats and models remain editable."),
		  {280, 145, 1190, 21});
	auto* search = new QLineEdit(m_presets);
	search->setObjectName("agentProviderSearch");
	search->setGeometry(pageRect({280, 195, 750, 43}));
	search->setPlaceholderText(tr("Search providers, for example DeepSeek"));
	const QStringList groups{"", "cn", "international", "compatible"};
	const QStringList captions{tr("All"), tr("China"), tr("International"), tr("Compatible / gateways")};
	auto* groupState = new QComboBox(m_presets);
	groupState->hide();
	groupState->addItems(groups);
	for (int index = 0; index < groups.size(); ++index)
	{
		auto* filter = button(m_presets, captions[index], {1048 + index * 102, 197, index == 3 ? 188 : 92, 38});
		filter->setCheckable(true);
		filter->setAutoExclusive(true);
		filter->setChecked(index == 0);
		connect(filter, &QToolButton::clicked, groupState, [groupState, index] { groupState->setCurrentIndex(index); });
	}
	for (int index = 0; index < int(std::size(presets)); ++index)
	{
		const auto preset = presets[index];
		auto* choice = button(m_presets, {}, {280 + index % 4 * 324, 264 + index / 4 * 118, 302, 98});
		choice->setObjectName(QStringLiteral("agentPreset_%1").arg(preset.id));
		choice->setToolTip(tr(preset.name));
		choice->setProperty("agentPresetCard", true);
		auto* image = new QLabel(choice);
		image->setPixmap(embed::getIconPixmap("agent_chat_key", 24, 24));
		image->setGeometry(16, 18, 24, 24);
		image->setAttribute(Qt::WA_TransparentForMouseEvents);
		auto* name = new QLabel(tr(preset.name), choice);
		name->setGeometry(54, 13, 222, 24);
		auto* key = new QLabel(tr("API Key"), choice);
		key->setGeometry(54, 43, 222, 16);
		auto* protocol = new QLabel(QString::fromUtf8(preset.protocol), choice);
		protocol->setGeometry(16, 72, 270, 16);
		for (auto* child : {name, key, protocol})
		{
			child->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		auto titleFont = name->font();
		titleFont.setPixelSize(16);
		name->setFont(titleFont);
		connect(choice, &QToolButton::clicked, this,
				[this, preset] { addConnection(preset.id, tr(preset.name), preset.url, preset.protocol); });
		const auto update = [this, choice, search, groupState, preset] {
			choice->setVisible((groupState->currentText().isEmpty() || groupState->currentText() == preset.group)
							   && tr(preset.name).contains(search->text(), Qt::CaseInsensitive));
		};
		connect(search, &QLineEdit::textChanged, choice, update);
		connect(groupState, &QComboBox::currentIndexChanged, choice, update);
	}
	auto* customCard = new QFrame(m_presets);
	customCard->setProperty("agentSettingsCard", true);
	customCard->setGeometry(pageRect({280, 764, 1274, 99}));
	label(m_presets, tr("Custom compatible service"), {340, 782, 700, 25}, 17);
	label(m_presets, tr("Enter an address, API Key and request format to create your own connection."),
		  {340, 815, 900, 19}, 12);
	auto* custom = button(m_presets, tr("Add custom connection"), {1340, 794, 190, 38}, "agent_chat_new");
	custom->setObjectName("agentAddCustomConnection");
	connect(custom, &QToolButton::clicked, this,
			[this] { addConnection("custom", tr("Custom connection"), {}, "openai-compatible"); });
	label(m_presets, tr("Authentication: API Key, Bearer or native provider Key headers."), {282, 891, 1270, 18}, 11);
}

ChatConnection* ChatApiSettingsPage::currentConnection()
{
	for (auto& connection : m_connections)
	{
		if (connection.id == m_currentId)
		{
			return &connection;
		}
	}
	return nullptr;
}

void ChatApiSettingsPage::addConnection(const QString& provider, const QString& name, const QString& url,
										const QString& protocol)
{
	captureFields();
	auto connection = ChatConnection::create();
	connection.provider = provider;
	connection.name = name;
	connection.baseUrl = url;
	connection.protocol = protocol;
	setProtocolAuthentication(connection);
	m_connections.append(connection);
	m_presets->hide();
	m_editor->show();
	refreshConnections();
	selectConnection(connection.id);
}

void ChatApiSettingsPage::captureFields()
{
	if (auto* connection = currentConnection())
	{
		connection->name = m_name->text().trimmed();
		connection->baseUrl = m_url->text().trimmed();
		if (connection->protocol != m_protocol->currentData().toString())
		{
			connection->protocol = m_protocol->currentData().toString();
			setProtocolAuthentication(*connection);
		}
		connection->model = m_model->currentText().trimmed();
		m_preview->setText(
			QStringLiteral("%1\n%2\n%3: ••••").arg(connection->baseUrl, connection->protocol, connection->keyHeader));
		if (m_key->isModified())
		{
			m_keys.insert(connection->id, m_key->text());
		}
	}
}

void ChatApiSettingsPage::selectConnection(const QString& id)
{
	captureFields();
	if (m_reply)
	{
		m_reply->abort();
		m_reply = nullptr;
	}
	m_currentId = id;
	refreshCurrentFields();
}

void ChatApiSettingsPage::refreshCurrentFields()
{
	const auto id = m_currentId;
	const auto* connection = currentConnection();
	if (!connection)
	{
		return;
	}
	m_title->setText(connection->name);
	m_name->setText(connection->name);
	m_url->setText(connection->baseUrl);
	m_protocol->setCurrentIndex(m_protocol->findData(connection->protocol));
	m_model->clear();
	m_model->setEditText(connection->model);
	m_key->setText(m_keys.value(id));
	m_key->setModified(false);
	m_key->setPlaceholderText(tr("Enter a Key to replace it; leave unchanged to keep the stored Key."));
	m_status->setText(tr("Not tested"));
	const QSignalBlocker blocker(m_list);
	for (int row = 0; row < m_list->count(); ++row)
	{
		if (m_list->item(row)->data(Qt::UserRole).toString() == id)
		{
			m_list->setCurrentRow(row);
			break;
		}
	}
	m_preview->setText(
		QStringLiteral("%1\n%2\n%3: ••••").arg(connection->baseUrl, connection->protocol, connection->keyHeader));
}

void ChatApiSettingsPage::editAdvanced()
{
	captureFields();
	auto* connection = currentConnection();
	if (!connection)
	{
		return;
	}
	if (m_reply)
	{
		m_reply->abort();
		m_reply = nullptr;
	}
	ChatAdvancedDialog dialog(*connection, this);
	if (dialog.exec() == QDialog::Accepted)
	{
		*connection = dialog.connection();
		refreshCurrentFields();
	}
}

void ChatApiSettingsPage::refreshConnections()
{
	const QSignalBlocker blocker(m_list);
	m_list->clear();
	for (const auto& connection : m_connections)
	{
		if (!connection.name.contains(m_search->text(), Qt::CaseInsensitive))
		{
			continue;
		}
		auto* item = new QListWidgetItem(connection.name, m_list);
		item->setData(Qt::UserRole, connection.id);
		item->setSizeHint({220, 92});
		if (connection.id == m_currentId)
		{
			m_list->setCurrentItem(item);
		}
	}
}

void ChatApiSettingsPage::connectionMenu(const QPoint& position)
{
	const auto* item = m_list->itemAt(position);
	if (!item)
	{
		return;
	}
	selectConnection(item->data(Qt::UserRole).toString());
	QMenu menu(this);
	auto* duplicate = menu.addAction(tr("Duplicate"));
	auto* remove = menu.addAction(tr("Delete"));
	const auto* action = menu.exec(m_list->viewport()->mapToGlobal(position));
	if (action == duplicate)
	{
		auto copy = *currentConnection();
		copy.id = ChatConnection::create().id;
		copy.name += tr(" (copy)");
		// Copy public settings only; a new connection needs its own Key.
		m_connections.append(copy);
		refreshConnections();
		selectConnection(copy.id);
	}
	else if (action == remove)
	{
		m_removed.insert(m_currentId);
		m_keys.remove(m_currentId);
		for (auto iterator = m_connections.begin(); iterator != m_connections.end(); ++iterator)
		{
			if (iterator->id == m_currentId)
			{
				m_connections.erase(iterator);
				break;
			}
		}
		m_currentId.clear();
		refreshConnections();
		if (m_connections.isEmpty())
		{
			m_editor->hide();
			m_presets->show();
		}
		else
		{
			selectConnection(m_connections.first().id);
		}
	}
}

QString ChatApiSettingsPage::selectedConnectionId() const
{ return m_currentId; }

ChatConnection ChatApiSettingsPage::connectionDraft()
{
	captureFields();
	const auto* connection = currentConnection();
	return connection ? *connection : ChatConnection{};
}

void ChatApiSettingsPage::setGenerationDraft(const ChatConnection& draft)
{
	for (auto& connection : m_connections)
	{
		if (connection.id == draft.id)
		{
			connection.reasoning = draft.reasoning;
			connection.temperature = draft.temperature;
			return;
		}
	}
}

bool ChatApiSettingsPage::save(QString& error)
{
	captureFields();
	if (!m_loadError.isEmpty())
	{
		error = m_loadError;
		return false;
	}
	for (const auto& connection : m_connections)
	{
		if (!connection.validate(error))
		{
			return false;
		}
	}
	for (const auto& connection : m_connections)
	{
		if (m_keys.contains(connection.id))
		{
			const auto key = m_keys.value(connection.id);
			if (!(key.isEmpty() ? m_store.removeKey(connection.id, error)
								: m_store.writeKey(connection.id, key, error)))
			{
				return false;
			}
		}
		if (!m_store.saveConnection(connection, error))
		{
			return false;
		}
	}
	const auto stored = m_store.connections(error);
	if (!error.isEmpty())
	{
		return false;
	}
	for (const auto& connection : stored)
	{
		if (m_removed.contains(connection.id) && !m_store.removeConnection(connection.id, error))
		{
			return false;
		}
	}
	m_keys.clear();
	m_removed.clear();
	m_key->clear();
	m_key->setModified(false);
	return true;
}

void ChatApiSettingsPage::fetchModels()
{
	captureFields();
	const auto* connection = currentConnection();
	QString error;
	if (!connection || !connection->validate(error))
	{
		m_status->setText(error);
		return;
	}
	if (connection->protocol == "custom-json")
	{
		m_status->setText(tr("This format has no configured model-list endpoint. Enter a model ID manually."));
		return;
	}
	if (connection->mapping.value("keyLocation").toString("header") == "body")
	{
		m_status->setText(
			tr("JSON body authentication cannot be used for GET model lists. Enter a model ID manually."));
		return;
	}
	const auto key
		= m_keys.contains(connection->id) ? m_keys.value(connection->id) : m_store.readKey(connection->id, error);
	if (!error.isEmpty() || key.isEmpty())
	{
		m_status->setText(error.isEmpty() ? tr("Enter an API Key before testing.") : error);
		return;
	}
	if (m_reply)
	{
		m_reply->abort();
	}
	m_status->setText(tr("Requesting model list…"));
	QUrl url(connection->baseUrl);
	auto path = url.path();
	while (path.endsWith('/'))
	{
		path.chop(1);
	}
	if (connection->protocol.startsWith("azure-") && !path.endsWith("/openai/v1"))
	{
		path += "/openai/v1";
	}
	url.setPath(path + "/models");
	requestModels(url, *connection, key, {}, {});
}

void ChatApiSettingsPage::requestModels(const QUrl& url, const ChatConnection& connection, const QString& key,
										QStringList models, QSet<QString> cursors)
{
	QUrl authenticatedUrl(url);
	const bool queryAuthentication = connection.mapping.value("keyLocation").toString("header") == "query";
	if (queryAuthentication)
	{
		QUrlQuery query(authenticatedUrl);
		query.removeAllQueryItems(connection.keyHeader);
		query.addQueryItem(connection.keyHeader, connection.keyPrefix + key);
		authenticatedUrl.setQuery(query);
	}
	QNetworkRequest request(authenticatedUrl);
	request.setTransferTimeout(30000);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setRawHeader("Accept", "application/json");
	if (!queryAuthentication)
	{
		request.setRawHeader(connection.keyHeader.toUtf8(), (connection.keyPrefix + key).toUtf8());
	}
	if (connection.protocol == "anthropic-messages")
	{
		request.setRawHeader("anthropic-version",
							 connection.apiVersion.isEmpty() ? QByteArray("2026-10-21")
															 : connection.apiVersion.toUtf8());
	}
	auto* reply = m_network->get(request);
	m_reply = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply, connection, key, models, cursors]() mutable {
		reply->deleteLater();
		if (m_reply != reply || m_currentId != connection.id)
		{
			return;
		}
		m_reply = nullptr;
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
		{
			auto error = tr("HTTP %1: %2").arg(status).arg(reply->errorString());
			error.replace(key, "••••");
			error.replace(QString::fromLatin1(QUrl::toPercentEncoding(key)), "••••");
			m_status->setText(error);
			return;
		}
		QJsonParseError parseError;
		const auto document = QJsonDocument::fromJson(reply->readAll(), &parseError);
		const auto object = document.object();
		const bool gemini = connection.protocol == "gemini";
		const auto entries = object.value(gemini ? "models" : "data");
		if (parseError.error != QJsonParseError::NoError || !entries.isArray())
		{
			m_status->setText(tr("The endpoint did not return a valid model list. You can enter an ID manually."));
			return;
		}
		for (const auto& entry : entries.toArray())
		{
			const auto id = entry.toObject().value(gemini ? "name" : "id").toString();
			if (!id.isEmpty() && !models.contains(id))
			{
				models.append(id);
			}
		}
		QString cursor = gemini ? object.value("nextPageToken").toString() : QString{};
		if (connection.protocol == "anthropic-messages" && object.value("has_more").toBool())
		{
			cursor = object.value("last_id").toString();
		}
		if (!cursor.isEmpty())
		{
			if (cursors.contains(cursor))
			{
				m_status->setText(tr("The model-list endpoint repeated a pagination cursor."));
				return;
			}
			cursors.insert(cursor);
			QUrl next(reply->url());
			QUrlQuery query(next);
			query.removeAllQueryItems(gemini ? "pageToken" : "after_id");
			query.addQueryItem(gemini ? "pageToken" : "after_id", cursor);
			next.setQuery(query);
			requestModels(next, connection, key, models, cursors);
			return;
		}
		const auto selected = m_model->currentText();
		m_model->clear();
		m_model->addItems(models);
		m_model->setEditText(selected);
		m_status->setText(tr("Connection succeeded; %1 models returned.").arg(models.size()));
	});
}

} // namespace lmms::agent::chat
