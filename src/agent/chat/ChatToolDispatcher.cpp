#include "agent/chat/ChatToolDispatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include "../CommandTargets.h"
#include "../CoreCommands.h"
#include "../SvsCommands.h"
#include "Engine.h"
#include "Song.h"
#include "agent/CommandBus.h"
#include "agent/ScriptRunner.h"
#include "agent/ToolRegistry.h"
#include "agent/chat/ChatDownloadTask.h"
#include "agent/chat/ChatPortableTools.h"
#include "agent/chat/ChatSearchSettings.h"
#include "agent/chat/ChatWebSearch.h"

namespace lmms::agent::chat {
namespace {

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

bool onApplicationThread()
{ return QCoreApplication::instance() && QCoreApplication::instance()->thread() == QThread::currentThread(); }

bool captureTargets(const ChatToolCall& call, CommandTargetIdentities& targets, QStringList& scopes, QString& error)
{
	const auto all = commandTargetIdentities();
	if (all.isEmpty())
	{
		error = message("The current project is unavailable or being replaced.");
		return false;
	}
	scopes.clear();
	const auto arguments = call.arguments;
	if (call.command != "agent.runScript")
	{
		if (arguments.contains("track"))
		{
			CommandResult failure;
			Track* track = nullptr;
			int index = -1;
			if (!resolveTrack(Engine::getSong(), arguments, track, index, failure))
			{
				error = failure.errorMessage;
				return false;
			}
			const auto path = trackPath(index, track);
			if (arguments.contains("clip"))
			{
				ClipAddress address;
				if (!resolveClip(Engine::getSong(), arguments, address, failure))
				{
					error = failure.errorMessage;
					return false;
				}
				scopes.append(clipPath(index, address.clipIndex, track));
				// Keep the parent identity without binding unrelated sibling clips.
				targets.insert(path, all.value(path));
			}
			else
			{
				scopes.append(path);
			}
		}
		for (const auto* field : {"path", "target", "owner"})
		{
			const auto path = arguments.value(field).toString();
			if (all.contains(path))
			{
				scopes.append(path);
			}
		}
		if (call.command == "model.setValue" && arguments.value("path").toString().endsWith("/mixerChannel"))
		{
			// The value selects another object; the parameter alone does not bind
			// the destination when mixer channels are reordered while awaiting approval.
			const auto value = arguments.value("value");
			if (value.isDouble())
			{
				const auto destination = QStringLiteral("song/channel:%1").arg(value.toInt(-1));
				if (all.contains(destination))
				{
					scopes.append(destination);
				}
			}
		}
		for (const auto* field : {"channel", "from", "to", "controller"})
		{
			if ((QString::fromLatin1(field) == "from" || QString::fromLatin1(field) == "to")
				&& !call.command.startsWith("mixer."))
			{
				continue;
			}
			const auto value = arguments.value(field);
			if (value.isDouble())
			{
				const auto path = QStringLiteral("song/%1:%2")
									  .arg(QString::fromLatin1(field) == "controller" ? "controller" : "channel")
									  .arg(value.toInt());
				if (all.contains(path))
				{
					scopes.append(path);
				}
			}
		}
	}
	if (scopes.isEmpty())
	{
		scopes = {"song", "pattern"};
	}
	scopes.removeDuplicates();
	for (auto iterator = all.cbegin(); iterator != all.cend(); ++iterator)
	{
		for (const auto& scope : scopes)
		{
			if (iterator.key() == scope || iterator.key().startsWith(scope + '/'))
			{
				targets.insert(iterator.key(), iterator.value());
				break;
			}
		}
	}
	const bool scripted = call.command.startsWith("compose.") || call.command.startsWith("arrange.")
		|| call.command == "agent.runScript";
	if ((call.command.startsWith("midi.") && call.command != "midi.addNotes") || call.command == "query.notes"
		|| scripted)
	{
		if (!includeIndexedNoteTargets(targets, call.command, arguments, error))
		{
			return false;
		}
	}
	if ((call.command.startsWith("svs.") && call.command != "svs.addNotes") || scripted)
	{
		if (!includeSvsNoteTargets(targets, arguments, error))
		{
			return false;
		}
	}
	return true;
}

} // namespace

struct ChatToolDispatcher::State
{
	ChatToolCatalog catalog;
	QPointer<Song> song;
	QMetaObject::Connection resetConnection;
	QMetaObject::Connection destroyedConnection;
	QString sessionId;
	QString turnId;
	quint64 generation = 0;
	bool active = false;
	bool executing = false;
	bool resetWhileExecuting = false;
	bool authorized = false;
	QSet<QString> seenCalls;
	ChatToolCall call;
	QJsonObject approval;
	CommandTargetIdentities targets;

	// Chat-owned network tool currently waiting for its reply (never a project batch).
	ChatWebSearch* search = nullptr;
	ChatDownloadTask* download = nullptr;
	QJsonObject downloadPlan;
	QJsonObject networkOutcome;
};

ChatToolDispatcher::ChatToolDispatcher(QObject* parent)
	: QObject(parent)
	, m_state(std::make_unique<State>())
{
	m_state->search = new ChatWebSearch(this);
	m_state->download = new ChatDownloadTask(this);
	connect(m_state->download, &ChatDownloadTask::finished, this,
		[this](const QJsonObject& result) { finishNetworkTask(result); });
	connect(m_state->search, &ChatWebSearch::finished, this,
		[this](const QJsonObject& result) { finishNetworkTask(result); });
	connect(ChatSearchSettingsNotifier::instance(), &ChatSearchSettingsNotifier::changed, this, [this] {
		const auto settings = ChatSearchSettings::load();
		if (!m_state->networkOutcome.isEmpty() && (!settings.enabled || settings.backend == ChatSearchBackend::Model))
		{
			m_state->search->abort();
			finishNetworkTask(CommandResult::failure(
				"web_search_disabled", message("Web search was turned off; the running search was canceled."))
					.toJson());
		}
	});
}

ChatToolDispatcher::~ChatToolDispatcher() = default;

bool ChatToolDispatcher::beginTurn(const QString& sessionId, const QString& turnId, QString& error)
{
	error.clear();
	if (!onApplicationThread() || m_state->active || m_state->executing || sessionId.isEmpty() || turnId.isEmpty())
	{
		error = message("Start a distinct chat tool turn on the application thread.");
		return false;
	}
	auto* song = Engine::getSong();
	if (!song || song->isLoadingProject() || CommandBus::instance().isBatchActive())
	{
		error = message("The current project is unavailable or being replaced.");
		return false;
	}
	// Web search is offered only while the user has it enabled with an external source.
	const auto search = ChatSearchSettings::load();
	QJsonArray localTools;
	if (search.enabled && search.backend != ChatSearchBackend::Model)
	{
		localTools.append(ChatWebSearch::definition());
		localTools.append(ChatWebSearch::readDefinition());
	}
	// Asset downloads are their own switch; they never depend on web search.
	if (ChatDownloadSettings::load().enabled) { localTools.append(ChatDownloadTask::definition()); }
	if (!m_state->catalog.load(error, localTools)) { return false; }
	if (m_state->song != song)
	{
		disconnect(m_state->resetConnection);
		disconnect(m_state->destroyedConnection);
		m_state->song = song;
		m_state->resetConnection = connect(song, &Song::projectAboutToReset, this, [this] {
			++m_state->generation;
			if (m_state->executing)
			{
				// Decided when the command returns: a failed command's rollback restores
				// the project snapshot, which is not a project replacement.
				m_state->resetWhileExecuting = true;
				return;
			}
			invalidate(message("The project changed. Request a new tool call."));
		});
		m_state->destroyedConnection = connect(song, &QObject::destroyed, this, [this] {
			++m_state->generation;
			invalidate(message("The project changed. Request a new tool call."));
		});
	}
	m_state->sessionId = sessionId;
	m_state->turnId = turnId;
	m_state->seenCalls.clear();
	m_state->active = true;
	return true;
}

QJsonArray ChatToolDispatcher::definitions() const
{ return m_state->active ? m_state->catalog.definitions() : QJsonArray{}; }

QJsonObject ChatToolDispatcher::pendingApproval() const
{ return m_state->approval; }

bool ChatToolDispatcher::executing() const
{ return m_state->executing; }

bool ChatToolDispatcher::submit(const QJsonObject& call, QString& error, ChatApprovalMode mode)
{
	error.clear();
	if (!onApplicationThread() || !m_state->active || m_state->executing || !m_state->approval.isEmpty())
	{
		error = message("No chat tool turn is ready for another call.");
		return false;
	}
	if (!m_state->song || m_state->song != Engine::getSong() || m_state->song->isLoadingProject()
		|| CommandBus::instance().isBatchActive())
	{
		error = message("The current project is unavailable or being replaced.");
		return false;
	}
	ChatToolCall resolved;
	if (!m_state->catalog.resolve(call, resolved, error))
	{
		return false;
	}
	if (m_state->seenCalls.contains(resolved.id))
	{
		error = message("This tool call ID has already been handled in the current turn.");
		return false;
	}
	if (resolved.command == "agent.runScript")
	{
		const auto input = resolved.arguments.value("script");
		const auto script = input.isString() ? ScriptRunner::loadBuiltIn(input.toString()) : input.toObject();
		if (script.isEmpty())
		{
			error = message("The requested script is empty or unknown.");
			return false;
		}
		auto expanded = call;
		auto arguments = resolved.arguments;
		arguments.insert("script", script);
		arguments.insert("vars", arguments.value("vars").toObject());
		if (!arguments.contains("seed"))
		{
			const auto seed = script.value("seed");
			arguments.insert("seed", seed.isUndefined() ? QJsonValue(0) : seed);
		}
		expanded.insert("arguments", arguments);
		if (!m_state->catalog.resolve(expanded, resolved, error))
		{
			return false;
		}
	}
	CommandTargetIdentities targets;
	QStringList scopes;
	QJsonArray labels;
	QJsonObject help;
	if (m_state->catalog.isLocal(resolved.command))
	{
		// Network tools touch no project object; the approval shows what is sent.
		QString target;
		if (resolved.command == ChatDownloadTask::command())
		{
			// The card shows the source, the resolved target and the overwrite choice.
			QJsonObject plan;
			if (!ChatDownloadTask::plan(resolved.arguments, plan, error)) { return false; }
			target = plan.value("url").toString() + " → " + QDir::toNativeSeparators(plan.value("target").toString())
				+ " · "
				+ (plan.value("overwrite").toBool() ? message("replaces a file with the same name")
													: message("keeps an existing file with the same name"));
			if (plan.value("extract").toBool())
			{
				target += " · "
					+ message("extract to %1").arg(QDir::toNativeSeparators(plan.value("extractTo").toString()));
			}
			m_state->downloadPlan = plan;
		}
		else if (resolved.command == ChatWebSearch::readCommand())
		{
			target = resolved.arguments.value("url").toString();
		}
		else
		{
			const auto backend = ChatSearchSettings::backendName(ChatSearchSettings::load().backend);
			target = backend + " · " + resolved.arguments.value("query").toString();
		}
		scopes.append(QStringLiteral("web"));
		labels.append(QJsonObject{{"path", "web"}, {"name", target}});
		help = {{"description", m_state->catalog.description(resolved.command)}};
	}
	else
	{
		if (!captureTargets(resolved, targets, scopes, error)) { return false; }
		help = ToolRegistry::commandHelp(resolved.command);
		for (const auto& scope : scopes)
		{
			labels.append(QJsonObject{{"path", scope}, {"name", targets.value(scope).name}});
		}
	}
	const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_state->call = resolved;
	m_state->targets = targets;
	m_state->authorized = mode == ChatApprovalMode::None;
	m_state->seenCalls.insert(resolved.id);
	m_state->approval = {{"token", token},
						 {"sessionId", m_state->sessionId},
						 {"turnId", m_state->turnId},
						 {"id", resolved.id},
						 {"wireName", resolved.wireName},
						 {"command", resolved.command},
						 {"arguments", resolved.arguments},
						 {"approvalRequired", !m_state->authorized},
						 {"generation", QString::number(m_state->generation)},
						 {"scope", QJsonArray::fromStringList(scopes)},
						 {"purpose", help.value("description")},
						 {"annotations", help.value("annotations")},
						 {"targets", labels}};
	if (m_state->authorized)
	{
		QTimer::singleShot(0, this, [this, token] { execute(token); });
	}
	else
	{
		const auto approval = m_state->approval;
		emit approvalRequested(approval);
	}
	return true;
}

bool ChatToolDispatcher::decide(const QString& token, const QString& sessionId, const QString& turnId,
								const QString& callId, const QJsonObject& arguments, bool approved, QString& error)
{
	error.clear();
	if (!onApplicationThread() || !m_state->active || m_state->approval.isEmpty() || m_state->authorized
		|| token != m_state->approval.value("token").toString())
	{
		error = message("This tool approval is no longer pending.");
		return false;
	}
	if (sessionId != m_state->sessionId || turnId != m_state->turnId || callId != m_state->call.id
		|| arguments != m_state->call.arguments)
	{
		error = message("The approval no longer matches the original tool call.");
		finishPending(CommandResult::failure("approval_mismatch", error).toJson());
		return false;
	}
	if (!approved)
	{
		finishPending(CommandResult::failure("permission_denied", message("The user denied this tool call.")).toJson());
		return true;
	}
	m_state->authorized = true;
	QTimer::singleShot(0, this, [this, token] { execute(token); });
	return true;
}

void ChatToolDispatcher::finishPending(const QJsonObject& result)
{
	const QJsonObject outcome{{"sessionId", m_state->sessionId},
							  {"turnId", m_state->turnId},
							  {"id", m_state->call.id},
							  {"wireName", m_state->call.wireName},
							  {"command", m_state->call.command},
							  {"arguments", m_state->call.arguments},
							  {"approvalRequired", m_state->approval.value("approvalRequired")},
							  {"executed", false},
							  {"result", result}};
	m_state->approval = {};
	m_state->targets.clear();
	m_state->authorized = false;
	emit toolFinished(outcome);
}

void ChatToolDispatcher::execute(const QString& token)
{
	if (!m_state->active || !m_state->authorized || m_state->approval.value("token") != token)
	{
		return;
	}
	if (!m_state->song || m_state->song != Engine::getSong() || m_state->song->isLoadingProject()
		|| CommandBus::instance().isBatchActive())
	{
		finishPending(
			CommandResult::failure("project_busy", message("The current project is unavailable or being replaced."))
				.toJson());
		return;
	}
	if (m_state->catalog.isLocal(m_state->call.command))
	{
		executeNetworkTask();
		return;
	}
	CommandTargetIdentities targets;
	QStringList scopes;
	QString error;
	if (!captureTargets(m_state->call, targets, scopes, error) || targets != m_state->targets)
	{
		finishPending(
			CommandResult::failure("target_changed", message("The tool target changed. Request approval again."))
				.toJson());
		return;
	}
	// Consume authorization before entering a handler, which may dispatch Qt
	// events or replace the project. Reentrant decisions cannot replay this call.
	const auto call = m_state->call;
	const auto sessionId = m_state->sessionId;
	const auto turnId = m_state->turnId;
	const auto approvalRequired = m_state->approval.value("approvalRequired");
	m_state->approval = {};
	m_state->targets.clear();
	m_state->authorized = false;
	m_state->executing = true;
	m_state->resetWhileExecuting = false;
	const QPointer<ChatToolDispatcher> alive(this);
	const auto result = CommandBus::instance().execute(call.command, call.arguments).toJson();
	if (!alive)
	{
		return;
	}
	if (m_state->resetWhileExecuting && result.value("ok").toBool())
	{
		// The command itself replaced the project (for example song.clearProject):
		// its result is kept and the remaining calls of this turn are canceled.
		invalidate(message("The project changed. Request a new tool call."));
	}
	m_state->resetWhileExecuting = false;
	m_state->executing = false;
	emit toolFinished(QJsonObject{{"sessionId", sessionId},
								  {"turnId", turnId},
								  {"id", call.id},
								  {"wireName", call.wireName},
								  {"command", call.command},
								  {"arguments", call.arguments},
								  {"approvalRequired", approvalRequired},
								  {"executed", true},
								  {"result", result}});
}

void ChatToolDispatcher::executeDownload()
{
	const auto settings = ChatDownloadSettings::load();
	if (!settings.enabled)
	{
		finishPending(CommandResult::failure(
			"downloads_disabled", message("Asset downloads are turned off in the Agent settings."))
				.toJson());
		return;
	}
	// The target is resolved again: a moved project or folder needs a new approval.
	QJsonObject plan;
	QString error;
	if (!ChatDownloadTask::plan(m_state->call.arguments, plan, error) || plan != m_state->downloadPlan)
	{
		finishPending(CommandResult::failure(
			"target_changed", error.isEmpty() ? message("The tool target changed. Request approval again.") : error)
				.toJson());
		return;
	}
	const auto aria2 = ChatPortableTools::resolve(settings.aria2Path);
	if (!QFileInfo(aria2).isFile())
	{
		finishPending(CommandResult::failure("tool_missing",
			message("aria2 was not found at %1. Check the tools in the Agent settings.")
				.arg(QDir::toNativeSeparators(aria2)))
				.toJson());
		return;
	}
	const auto sevenZip = ChatPortableTools::resolve(settings.sevenZipPath);
	if (plan.value("extract").toBool() && !QFileInfo(sevenZip).isFile())
	{
		finishPending(CommandResult::failure("tool_missing",
			message("7-Zip was not found at %1. Check the tools in the Agent settings.")
				.arg(QDir::toNativeSeparators(sevenZip)))
				.toJson());
		return;
	}
	const auto& call = m_state->call;
	m_state->networkOutcome = {{"sessionId", m_state->sessionId}, {"turnId", m_state->turnId}, {"id", call.id},
		{"wireName", call.wireName}, {"command", call.command}, {"arguments", call.arguments},
		{"approvalRequired", m_state->approval.value("approvalRequired")}, {"executed", true}};
	m_state->approval = {};
	m_state->targets.clear();
	m_state->authorized = false;
	m_state->download->start(plan, aria2, sevenZip);
}

void ChatToolDispatcher::executeNetworkTask()
{
	if (m_state->call.command == ChatDownloadTask::command())
	{
		executeDownload();
		return;
	}
	// Re-check at run time: approval does not keep a disabled search alive.
	const auto settings = ChatSearchSettings::load();
	if (!settings.enabled || settings.backend == ChatSearchBackend::Model)
	{
		finishPending(
			CommandResult::failure("web_search_disabled", message("Web search is turned off in the Agent settings."))
				.toJson());
		return;
	}
	const auto& call = m_state->call;
	const bool reading = call.command == ChatWebSearch::readCommand();
	QString credential;
	if (!reading)
	{
		const ChatConnectionStore store(
			QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("agent"));
		QString error;
		const bool brave = settings.backend == ChatSearchBackend::Brave;
		credential
			= store.readKey(brave ? ChatSearchSettings::braveKeyId() : ChatSearchSettings::searxngAuthId(), error);
		if (!error.isEmpty() || (brave && credential.isEmpty()))
		{
			finishPending(CommandResult::failure("search_not_configured",
				error.isEmpty() ? message("Brave search needs an API key in the Agent settings.") : error)
					.toJson());
			return;
		}
	}

	// Consume the authorization before the asynchronous request starts.
	m_state->networkOutcome = {{"sessionId", m_state->sessionId}, {"turnId", m_state->turnId}, {"id", call.id},
		{"wireName", call.wireName}, {"command", call.command}, {"arguments", call.arguments},
		{"approvalRequired", m_state->approval.value("approvalRequired")}, {"executed", true}};
	m_state->approval = {};
	m_state->targets.clear();
	m_state->authorized = false;
	if (reading)
	{
		m_state->search->startRead(call.arguments.value("url").toString());
		return;
	}
	const int count = call.arguments.contains("count") ? call.arguments.value("count").toInt() : 5;
	m_state->search->start(settings, credential, call.arguments.value("query").toString(), count);
}

void ChatToolDispatcher::finishNetworkTask(const QJsonObject& result)
{
	if (m_state->networkOutcome.isEmpty()) { return; }
	auto outcome = m_state->networkOutcome;
	m_state->networkOutcome = {};
	outcome.insert("result", result);
	emit toolFinished(outcome);
}

void ChatToolDispatcher::invalidate(const QString& reason)
{
	// A running search or download belongs to the invalidated turn; its result is dropped.
	m_state->search->abort();
	m_state->download->abort();
	m_state->networkOutcome = {};
	const bool wasActive = m_state->active;
	m_state->active = false;
	m_state->approval = {};
	m_state->targets.clear();
	m_state->authorized = false;
	if (wasActive)
	{
		const auto sessionId = m_state->sessionId;
		const auto turnId = m_state->turnId;
		emit invalidated(sessionId, turnId, reason);
	}
}

void ChatToolDispatcher::cancel()
{ invalidate(message("The chat tool turn was canceled.")); }

} // namespace lmms::agent::chat
