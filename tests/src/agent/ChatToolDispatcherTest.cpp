#include "agent/chat/ChatToolDispatcher.h"

#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QWidget>
#include <QtTest>

#include "Engine.h"
#include "ProjectJournal.h"
#include "SVSClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"
#include "agent/CommandBus.h"

using namespace lmms::agent;
using namespace lmms::agent::chat;

namespace {
int Executions = 0;
QThread* ExecutionThread = nullptr;
} // namespace

class ChatToolDispatcherTest : public QObject
{
	Q_OBJECT

private:
	QJsonObject call(const QString& command, const QJsonObject& arguments, const QString& id = "call-1") const
	{ return {{"id", id}, {"name", m_catalog.nameForCommand(command)}, {"arguments", arguments}}; }

	static bool decide(ChatToolDispatcher& dispatcher, const QJsonObject& approval, bool accepted, QString& error)
	{
		return dispatcher.decide(approval.value("token").toString(), approval.value("sessionId").toString(),
								 approval.value("turnId").toString(), approval.value("id").toString(),
								 approval.value("arguments").toObject(), accepted, error);
	}

	static QString resultCode(const QSignalSpy& finished)
	{
		return finished.last()[0]
			.toJsonObject()
			.value("result")
			.toObject()
			.value("error")
			.toObject()
			.value("code")
			.toString();
	}

private slots:
	void initTestCase()
	{
		lmms::Engine::init(true);
		m_window.setWindowTitle(QStringLiteral("LMMS tool approval validation"));
		m_window.resize(480, 160);
		m_window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&m_window));
		CommandDescriptor probe;
		probe.name = "chat.approvalProbe";
		probe.summary = "Read-only approval probe.";
		probe.argsSchema = {{"type", "object"},
							{"properties", QJsonObject{{"value", QJsonObject{{"type", "integer"}}}}},
							{"required", QJsonArray{"value"}}};
		probe.handler = [](const QJsonObject& arguments) {
			++Executions;
			ExecutionThread = QThread::currentThread();
			return CommandResult::success(arguments);
		};
		QVERIFY(CommandBus::instance().registerCommand(probe));
		QString error;
		QVERIFY2(m_catalog.load(error), qPrintable(error));
	}

	void cleanupTestCase()
	{
		m_window.close();
		lmms::Engine::destroy();
	}

	void init()
	{
		lmms::Engine::getSong()->clearProject();
		lmms::Engine::projectJournal()->clearJournal();
		Executions = 0;
		ExecutionThread = nullptr;
	}

	void approvalModesCannotPermitProjectReset()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(m_catalog.nameForCommand("song.clearProject").isEmpty());
		QVERIFY(m_catalog.nameForCommand("song.load").isEmpty());
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Original"}}).ok);
		QPointer<lmms::Track> original(lmms::Engine::getSong()->tracks()[0]);
		QSignalSpy reset(lmms::Engine::getSong(), &lmms::Song::projectAboutToReset);
		for (const bool automatic : {false, true})
		{
			ChatToolDispatcher dispatcher;
			QString error;
			QVERIFY(dispatcher.beginTurn("reset-session", automatic ? "automatic" : "approved", error));
			QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
			QSignalSpy invalidated(&dispatcher, &ChatToolDispatcher::invalidated);
			const QJsonArray steps{
				QJsonObject{{"cmd", "track.setName"}, {"args", QJsonObject{{"track", 0}, {"value", "Partial"}}}},
				QJsonObject{{"cmd", "song.clearProject"}}};
			const auto request = call("agent.runScript", {{"script", QJsonObject{{"steps", steps}}}});
			if (automatic)
			{
				QVERIFY(dispatcher.submit(request, error, ChatApprovalMode::None));
			}
			else
			{
				QVERIFY(dispatcher.submit(request, error));
				QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
			}
			QTRY_COMPARE(finished.count(), 1);
			QCOMPARE(resultCode(finished), QStringLiteral("agent_project_reset_forbidden"));
			QCOMPARE(invalidated.count(), 0);
			QCOMPARE(reset.count(), 0);
			QVERIFY(original);
			QCOMPARE(original->name(), QStringLiteral("Original"));
			QCOMPARE(bus.batchDepth(), 0);
		}
	}

	void readOnlyRequiresApprovalExactlyOnce()
	{
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy requested(&dispatcher, &ChatToolDispatcher::approvalRequested);
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(call("chat.approvalProbe", {{"value", 7}}), error));
		QCOMPARE(requested.count(), 1);
		QCOMPARE(Executions, 0);
		QCOMPARE(CommandBus::instance().batchDepth(), 0);
		QVERIFY(!lmms::Engine::projectJournal()->hasActiveTransaction());
		const auto approval = dispatcher.pendingApproval();
		QVERIFY(approval.value("annotations").toObject().value("readOnlyHint").toBool());
		QVERIFY(decide(dispatcher, approval, true, error));
		QVERIFY(!decide(dispatcher, approval, true, error));
		QTRY_COMPARE(finished.count(), 1);
		QCOMPARE(Executions, 1);
		QCOMPARE(ExecutionThread, QCoreApplication::instance()->thread());
		QVERIFY(finished[0][0].toJsonObject().value("result").toObject().value("ok").toBool());
		QVERIFY(!decide(dispatcher, approval, true, error));
		QVERIFY(!dispatcher.submit(call("chat.approvalProbe", {{"value", 7}}), error));
		QCOMPARE(Executions, 1);
	}

	void denialAndBindingMismatchNeverExecute()
	{
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(call("chat.approvalProbe", {{"value", 7}}), error));
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), false, error));
		QCOMPARE(resultCode(finished), QStringLiteral("permission_denied"));
		for (const auto& field : {"sessionId", "turnId", "id", "arguments"})
		{
			QVERIFY(dispatcher.submit(call("chat.approvalProbe", {{"value", 7}}, QString::fromLatin1(field)), error));
			auto approval = dispatcher.pendingApproval();
			if (QString::fromLatin1(field) == "arguments")
			{
				approval.insert(field, QJsonObject{{"value", 8}});
			}
			else
			{
				approval.insert(field, "changed");
			}
			QVERIFY(!decide(dispatcher, approval, true, error));
			QCOMPARE(resultCode(finished), QStringLiteral("approval_mismatch"));
			QVERIFY(dispatcher.pendingApproval().isEmpty());
		}
		QCOMPARE(Executions, 0);
	}

	void noApprovalIsQueuedAndCancelable()
	{
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy requested(&dispatcher, &ChatToolDispatcher::approvalRequested);
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(call("chat.approvalProbe", {{"value", 1}}), error, ChatApprovalMode::None));
		QCOMPARE(requested.count(), 0);
		QCOMPARE(Executions, 0);
		dispatcher.cancel();
		QVERIFY(dispatcher.definitions().isEmpty());
		QVERIFY(dispatcher.beginTurn("session-b", "turn-b", error));
		QVERIFY(dispatcher.submit(call("chat.approvalProbe", {{"value", 2}}), error, ChatApprovalMode::None));
		QTRY_COMPARE(finished.count(), 1);
		QCOMPARE(Executions, 1);
		QCOMPARE(finished[0][0].toJsonObject().value("sessionId").toString(), QStringLiteral("session-b"));
		QCOMPARE(
			finished[0][0].toJsonObject().value("result").toObject().value("data").toObject().value("value").toInt(),
			2);
	}

	void samePathProjectResetInvalidates()
	{
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy invalidated(&dispatcher, &ChatToolDispatcher::invalidated);
		QVERIFY(dispatcher.submit(call("chat.approvalProbe", {{"value", 1}}), error));
		const auto approval = dispatcher.pendingApproval();
		const auto path = lmms::Engine::getSong()->projectFileName();
		lmms::Engine::getSong()->clearProject();
		QCOMPARE(lmms::Engine::getSong()->projectFileName(), path);
		QCOMPARE(invalidated.count(), 1);
		QVERIFY(!decide(dispatcher, approval, true, error));
		QVERIFY(dispatcher.beginTurn("session-a", "turn-b", error));
		QVERIFY(dispatcher.submit(call("chat.approvalProbe", {{"value", 1}}), error, ChatApprovalMode::None));
		QVERIFY(dispatcher.pendingApproval().value("generation") != approval.value("generation"));
		lmms::Engine::getSong()->clearProject();
		QCoreApplication::processEvents();
		QCOMPARE(Executions, 0);
	}

	void targetReorderRequiresNewApproval()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "A"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "B"}}).ok);
		QPointer<lmms::Track> original(lmms::Engine::getSong()->tracks()[0]);
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(call("track.setName", {{"track", 0}, {"value", "Approved"}}), error));
		const auto approval = dispatcher.pendingApproval();
		QVERIFY(bus.execute("track.move", {{"track", 0}, {"newIndex", 1}}).ok);
		QVERIFY(decide(dispatcher, approval, true, error));
		QTRY_COMPARE(finished.count(), 1);
		QCOMPARE(resultCode(finished), QStringLiteral("target_changed"));
		QCOMPARE(original->name(), QStringLiteral("A"));
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("B"));
		QVERIFY(dispatcher.submit(call("track.setName", {{"track", 1}, {"value", "Approved"}}, "call-2"), error));
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
		QTRY_COMPARE(finished.count(), 2);
		QVERIFY(finished.last()[0].toJsonObject().value("result").toObject().value("ok").toBool());
		QCOMPARE(original->name(), QStringLiteral("Approved"));
	}

	void unrelatedRenameKeepsApprovedTarget()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "A"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "B"}}).ok);
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(call("track.setName", {{"track", 0}, {"value", "Approved"}}), error));
		QVERIFY(bus.execute("track.setName", {{"track", 1}, {"value", "Unrelated"}}).ok);
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
		QTRY_COMPARE(finished.count(), 1);
		QVERIFY(finished[0][0].toJsonObject().value("result").toObject().value("ok").toBool());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Approved"));
	}

	void routingChannelReorderRequiresNewApproval()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Routing"}}).ok);
		QVERIFY(bus.execute("mixer.addChannel", {{"name", "A"}}).ok);
		QVERIFY(bus.execute("mixer.addChannel", {{"name", "B"}}).ok);
		auto* track = qobject_cast<lmms::SampleTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QVERIFY(track);
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(
			dispatcher.submit(call("model.setValue", {{"path", "song/track:0/mixerChannel"}, {"value", 1}}), error));
		const auto approval = dispatcher.pendingApproval();
		QVERIFY(bus.execute("mixer.moveChannel", {{"channel", 1}, {"direction", "right"}}).ok);
		QVERIFY(decide(dispatcher, approval, true, error));
		QTRY_COMPARE(finished.count(), 1);
		QCOMPARE(track->mixerChannelModel()->value(), 0);
		QCOMPARE(resultCode(finished), QStringLiteral("target_changed"));
		QVERIFY(dispatcher.submit(
			call("model.setValue", {{"path", "song/track:0/mixerChannel"}, {"value", 2}}, "call-2"), error));
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
		QTRY_COMPARE(finished.count(), 2);
		QVERIFY(finished.last()[0].toJsonObject().value("result").toObject().value("ok").toBool());
		QCOMPARE(track->mixerChannelModel()->value(), 2);
	}

	void midiNoteReorderRequiresNewApproval()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}, {"name", "Notes"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}, {"length", 768}}).ok);
		const QJsonArray notes{
			QJsonObject{{"position", 0}, {"length", 48}, {"key", 60}, {"volume", 100}, {"panning", 0}},
			QJsonObject{{"position", 192}, {"length", 48}, {"key", 64}, {"volume", 100}, {"panning", 0}}};
		QVERIFY(bus.execute("midi.addNotes", {{"track", 0}, {"clip", 0}, {"notes", notes}}).ok);
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(
			call("midi.updateNote", {{"track", 0}, {"clip", 0}, {"note", 0}, {"fields", QJsonObject{{"key", 72}}}}),
			error));
		const auto approval = dispatcher.pendingApproval();
		QVERIFY(bus.execute("midi.updateNote",
							{{"track", 0}, {"clip", 0}, {"note", 0}, {"fields", QJsonObject{{"position", 384}}}})
					.ok);
		const auto reordered = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		QCOMPARE(reordered[0].toObject().value("key").toInt(), 64);
		QVERIFY(decide(dispatcher, approval, true, error));
		QTRY_COMPARE(finished.count(), 1);
		const auto after = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		QCOMPARE(after, reordered);
		QCOMPARE(resultCode(finished), QStringLiteral("target_changed"));
		QVERIFY(dispatcher.submit(call("midi.updateNote",
									   {{"track", 0}, {"clip", 0}, {"note", 1}, {"fields", QJsonObject{{"key", 72}}}},
									   "call-2"),
								  error));
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
		QTRY_COMPARE(finished.count(), 2);
		QVERIFY(finished.last()[0].toJsonObject().value("result").toObject().value("ok").toBool());
		const auto approved = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		QCOMPARE(approved[0].toObject().value("key").toInt(), 64);
		QCOMPARE(approved[1].toObject().value("key").toInt(), 72);
	}

	void svsRangeAndStableIdTargets()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("svs.createTrack", {{"name", "SVS targets"}}).ok);
		QVERIFY(bus.execute("svs.createClip", {{"track", 0}, {"position", 0}, {"length", 768}}).ok);
		const QJsonArray notes{
			QJsonObject{{"id", "a"}, {"tick", 0}, {"duration", 48}, {"pitch", 60}, {"lyric", "la"}},
			QJsonObject{{"id", "b"}, {"tick", 192}, {"duration", 48}, {"pitch", 64}, {"lyric", "la"}}};
		QVERIFY(bus.execute("svs.addNotes", {{"track", 0}, {"clip", 0}, {"notes", notes}}).ok);
		auto* clip = qobject_cast<lmms::SVSClip*>(lmms::Engine::getSong()->tracks()[0]->getClips()[0]);
		QVERIFY(clip);
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(
			call("svs.removeNotes", {{"track", 0}, {"clip", 0}, {"range", QJsonObject{{"start", 0}, {"end", 96}}}}),
			error));
		const auto approval = dispatcher.pendingApproval();
		auto edited = clip->notes();
		edited[0].tick = 384;
		edited[1].tick = 0;
		clip->setNotes(edited);
		const auto before = clip->notes();
		QVERIFY(decide(dispatcher, approval, true, error));
		QTRY_COMPARE(finished.count(), 1);
		QCOMPARE(resultCode(finished), QStringLiteral("target_changed"));
		QCOMPARE(clip->notes(), before);
		QVERIFY(dispatcher.submit(
			call("svs.removeNotes", {{"track", 0}, {"clip", 0}, {"ids", QJsonArray{"a"}}}, "call-2"), error));
		edited = clip->notes();
		for (auto& note : edited)
		{
			if (note.id == "b")
			{
				note.tick = 96;
			}
		}
		clip->setNotes(edited);
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
		QTRY_COMPARE(finished.count(), 2);
		QVERIFY2(finished.last()[0].toJsonObject().value("result").toObject().value("ok").toBool(),
				 qPrintable(QString::fromUtf8(QJsonDocument(finished.last()[0].toJsonObject()).toJson())));
		QCOMPARE(clip->notes().size(), 1);
		QCOMPARE(clip->notes()[0].id, QStringLiteral("b"));
		QVERIFY(bus.execute("svs.addNotes",
							{{"track", 0},
							 {"clip", 0},
							 {"notes",
							  QJsonArray{QJsonObject{
								  {"id", "c"}, {"tick", 192}, {"duration", 48}, {"pitch", 67}, {"lyric", "la"}}}}})
					.ok);
		QVERIFY(dispatcher.submit(call("svs.updateNote",
									   {{"track", 0}, {"clip", 0}, {"id", "b"}, {"fields", QJsonObject{{"pitch", 72}}}},
									   "call-3"),
								  error));
		edited = clip->notes();
		for (auto& note : edited)
		{
			if (note.id == "c")
			{
				note.tick = 384;
			}
		}
		clip->setNotes(edited);
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
		QTRY_COMPARE(finished.count(), 3);
		QVERIFY(finished.last()[0].toJsonObject().value("result").toObject().value("ok").toBool());
		for (const auto& note : clip->notes())
		{
			QCOMPARE(note.pitch, note.id == "b" ? 72 : 67);
		}
	}

	void scriptsShowExpandedBodyAndKeepTransactionSemantics()
	{
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-a", "turn-a", error));
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY(dispatcher.submit(
			call("agent.runScript",
				 {{"script", "four_on_floor_drums"}, {"vars", QJsonObject{{"bars", 1}}}, {"seed", 42}}),
			error));
		const auto arguments = dispatcher.pendingApproval().value("arguments").toObject();
		QVERIFY(arguments.value("script").isObject());
		QVERIFY(!arguments.value("script").toObject().value("steps").toArray().isEmpty());
		QCOMPARE(arguments.value("seed").toInt(), 42);
		QCOMPARE(arguments.value("vars").toObject().value("bars").toInt(), 1);
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), false, error));
		QVERIFY(lmms::Engine::getSong()->tracks().empty());
		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "sample"}, {"name", "A"}}).ok);
		QPointer<lmms::Track> original(lmms::Engine::getSong()->tracks()[0]);
		QSignalSpy reset(lmms::Engine::getSong(), &lmms::Song::projectAboutToReset);
		const QJsonObject script{{"name", "Approved rename"},
								 {"steps",
								  QJsonArray{QJsonObject{{"cmd", "track.setName"},
														 {"args", QJsonObject{{"track", 0}, {"value", "Scripted"}}}}}}};
		QVERIFY2(dispatcher.submit(call("agent.runScript", {{"script", script}, {"seed", 17}}, "call-2"), error),
				 qPrintable(error));
		QVERIFY(decide(dispatcher, dispatcher.pendingApproval(), true, error));
		QTRY_COMPARE(finished.count(), 2);
		QVERIFY2(finished.last()[0].toJsonObject().value("result").toObject().value("ok").toBool(),
				 qPrintable(QString::fromUtf8(QJsonDocument(finished.last()[0].toJsonObject()).toJson())));
		QCOMPARE(reset.count(), 0);
		QVERIFY(original);
		QCOMPARE(original->name(), QStringLiteral("Scripted"));
		QCOMPARE(CommandBus::instance().batchDepth(), 0);
		QVERIFY(!lmms::Engine::projectJournal()->hasActiveTransaction());
	}

	void instrumentPresetLoadKeepsTurnValid_data()
	{
		QTest::addColumn<bool>("loads");
		QTest::newRow("AnalogBell.xpf") << true;
		// A failing command is rolled back by CommandBus, which restores the project
		// snapshot; that rollback must not cancel the rest of the turn.
		QTest::newRow("Missing.xpf") << false;
	}

	void instrumentPresetLoadKeepsTurnValid()
	{
		auto& bus = CommandBus::instance();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}, {"name", "Lead"}}).ok);
		const auto preset = QDir(qEnvironmentVariable("LMMS_DATA_DIR"))
								.absoluteFilePath("presets/TripleOscillator/" + QString(QTest::currentDataTag()));
		QFETCH(bool, loads);
		QVERIFY2(QFileInfo(preset).isFile() == loads, qPrintable(preset));
		ChatToolDispatcher dispatcher;
		QString error;
		QVERIFY(dispatcher.beginTurn("session-p", "turn-p", error));
		QSignalSpy invalidated(&dispatcher, &ChatToolDispatcher::invalidated);
		QSignalSpy finished(&dispatcher, &ChatToolDispatcher::toolFinished);
		QVERIFY2(dispatcher.submit(
					 call("instrument.loadPreset", {{"track", 0}, {"path", preset}}), error, ChatApprovalMode::None),
			qPrintable(error));
		QTRY_COMPARE(finished.count(), 1);
		const auto outcome = finished[0][0].toJsonObject();
		QVERIFY2(outcome.value("result").toObject().value("ok").toBool() == loads,
			qPrintable(QString::fromUtf8(QJsonDocument(outcome).toJson())));
		// Editing one track, or rolling a failed edit back, is not a project replacement.
		QCOMPARE(invalidated.count(), 0);
		// The turn stays usable for the next call.
		QVERIFY2(dispatcher.submit(call("track.list", {}, "call-2"), error, ChatApprovalMode::None), qPrintable(error));
		QTRY_COMPARE(finished.count(), 2);
		QVERIFY(finished[1][0].toJsonObject().value("result").toObject().value("ok").toBool());
	}

private:
	QWidget m_window;
	ChatToolCatalog m_catalog;
};

QTEST_MAIN(ChatToolDispatcherTest)
#include "ChatToolDispatcherTest.moc"
