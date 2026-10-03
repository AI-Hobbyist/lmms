#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QThread>
#include <QElapsedTimer>
#include "agent/CommandBus.h"
#include "agent/ScriptRunner.h"
#include "agent/ToolRegistry.h"
#include "Engine.h"

using namespace lmms::agent;
int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	const auto args = application.arguments();
	QTextStream output(stdout), error(stderr);
	if (args.contains("--tools"))
	{
		output << QJsonDocument(ToolRegistry::tools()).toJson(QJsonDocument::Indented);
		return 0;
	}
	if (args.contains("--manual"))
	{
		output << ToolRegistry::commandManual();
		return 0;
	}
	const auto option = [&](const QString& name) {
		const int index = args.indexOf(name);
		return index >= 0 && index + 1 < args.size() ? args[index + 1] : QString();
	};
	QJsonObject script;
	if (args.contains("--script"))
	{
		QFile file(option("--script"));
		if (!file.open(QIODevice::ReadOnly)) { error << "Cannot open script.\n"; return 2; }
		QJsonParseError parse;
		const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
		if (parse.error != QJsonParseError::NoError || !document.isObject()) { error << "Invalid script JSON.\n"; return 2; }
		script = document.object();
	}
	else { script = ScriptRunner::loadBuiltIn(option("--builtin")); }
	if (script.isEmpty())
	{
		error << "Usage: AgentHarness --tools | --manual | (--script FILE | --builtin NAME) [--vars JSON] [--seed INT] [--dry-run]\n";
		return 2;
	}
	QJsonObject variables;
	if (args.contains("--vars"))
	{
		QJsonParseError parse;
		const auto document = QJsonDocument::fromJson(option("--vars").toUtf8(), &parse);
		if (parse.error != QJsonParseError::NoError || !document.isObject()) { error << "Invalid vars JSON.\n"; return 2; }
		variables = document.object();
	}
	bool validSeed = true;
	const int seed = args.contains("--seed") ? option("--seed").toInt(&validSeed) : script.value("seed").toInt();
	if (!validSeed) { error << "seed must be a 32-bit integer.\n"; return 2; }
	lmms::Engine::init(true);
	const auto result = ScriptRunner::run(script, variables, seed, args.contains("--dry-run"));
	output << QJsonDocument(result.toJson()).toJson(QJsonDocument::Indented);
	output.flush();
	bool ok = result.ok;
	const auto task = result.data.value("lastResult").toObject().value("task").toString();
	if (ok && !task.isEmpty() && !args.contains("--dry-run"))
	{
		QElapsedTimer timeout; timeout.start();
		for (;;)
		{
			QCoreApplication::processEvents();
			const auto status = CommandBus::instance().execute("export.status", {{"task", task}});
			const auto state = status.data.value("status").toString();
			if (!status.ok || state == "completed" || state == "failed" || state == "cancelled")
			{
				output << QJsonDocument(status.toJson()).toJson(QJsonDocument::Indented);
				ok = status.ok && state == "completed"; break;
			}
			if (timeout.elapsed() >= 60000)
			{
				CommandBus::instance().execute("export.cancel", {{"task", task}});
				error << "Export timed out.\n"; ok = false; break;
			}
			QThread::msleep(10);
		}
	}
	lmms::Engine::destroy();
	return ok ? 0 : 1;
}
