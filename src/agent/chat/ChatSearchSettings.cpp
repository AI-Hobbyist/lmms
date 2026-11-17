#include "agent/chat/ChatSearchSettings.h"

#include "ConfigManager.h"

namespace lmms::agent::chat {

ChatSearchSettings ChatSearchSettings::load()
{
	auto* config = ConfigManager::inst();
	ChatSearchSettings settings;
	settings.enabled = config->value("agentchat", "webSearch", "0") == "1";
	settings.backend = backendFromName(config->value("agentchat", "searchBackend", "searxng"));
	settings.searxngUrl = config->value("agentchat", "searxngUrl", "");
	settings.braveUrl = config->value("agentchat", "braveUrl", defaultBraveUrl());
	return settings;
}

void ChatSearchSettings::save() const
{
	auto* config = ConfigManager::inst();
	config->setValue("agentchat", "webSearch", enabled ? "1" : "0");
	config->setValue("agentchat", "searchBackend", backendName(backend));
	config->setValue("agentchat", "searxngUrl", searxngUrl);
	config->setValue("agentchat", "braveUrl", braveUrl);
}

QString ChatSearchSettings::defaultBraveUrl()
{ return QStringLiteral("https://api.search.brave.com/res/v1/web/search"); }

QString ChatSearchSettings::backendName(ChatSearchBackend backend)
{
	switch (backend)
	{
	case ChatSearchBackend::Brave:
		return QStringLiteral("brave");
	case ChatSearchBackend::Model:
		return QStringLiteral("model");
	case ChatSearchBackend::SearXNG:
		break;
	}
	return QStringLiteral("searxng");
}

ChatSearchBackend ChatSearchSettings::backendFromName(const QString& name)
{
	if (name == "brave") { return ChatSearchBackend::Brave; }
	if (name == "model") { return ChatSearchBackend::Model; }
	return ChatSearchBackend::SearXNG;
}

QString ChatSearchSettings::searxngAuthId()
{ return QStringLiteral("6f3c9a52-2d7e-4c1b-9a8e-5b1f0c3d7e01"); }

QString ChatSearchSettings::braveKeyId()
{ return QStringLiteral("6f3c9a52-2d7e-4c1b-9a8e-5b1f0c3d7e02"); }

} // namespace lmms::agent::chat
