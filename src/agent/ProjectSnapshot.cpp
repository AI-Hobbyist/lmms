#include "ProjectSnapshot.h"
#include <QDomElement>
#include <QJsonArray>
#include "DataFile.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms::agent {

void collectFields(const QDomElement& element, const QString& path, ProjectFields& fields)
{
	fields.insert(path, element.tagName());
	const auto attributes = element.attributes();
	for (int index = 0; index < attributes.size(); ++index)
	{
		const auto attribute = attributes.item(index).toAttr();
		fields.insert(path + "/@" + attribute.name(), attribute.value());
	}
	QMap<QString, int> counts;
	for (auto child = element.firstChild(); !child.isNull(); child = child.nextSibling())
	{
		if (child.isElement())
		{
			const auto childElement = child.toElement();
			const auto tag = childElement.tagName();
			const int index = counts[tag]++;
			collectFields(childElement, path + "/" + tag + QString("[%1]").arg(index), fields);
		}
		else if (child.isText() || child.isCDATASection())
		{
			fields[path + "/text()"] += child.nodeValue();
		}
	}
}

ProjectFields projectFields()
{
	ProjectFields fields;
	if (auto* song = Engine::getSong())
	{
		DataFile state(DataFile::Type::SongProject);
		auto* journal = Engine::projectJournal();
		struct JournallingGuard
		{
			ProjectJournal* journal;
			bool enabled;
			~JournallingGuard() { journal->setJournalling(enabled); }
		};
		const JournallingGuard guard{journal, journal->isJournalling()};
		journal->setJournalling(false);
		song->saveProjectState(state);
		collectFields(state.head(), "/head", fields);
		collectFields(state.content(), "/project", fields);
	}
	return fields;
}

QJsonObject projectDiff(const QString& command, const ProjectFields& before, const ProjectFields& after)
{
	QJsonArray changes;
	for (auto it = before.begin(); it != before.end(); ++it)
	{
		if (!after.contains(it.key()))
		{
			changes.append(QJsonObject{{"op", "remove"}, {"path", it.key()}, {"before", it.value()}});
		}
		else if (after.value(it.key()) != it.value())
		{
			changes.append(QJsonObject{
				{"op", "replace"}, {"path", it.key()}, {"before", it.value()}, {"after", after.value(it.key())}});
		}
	}
	for (auto it = after.begin(); it != after.end(); ++it)
	{
		if (!before.contains(it.key()))
		{
			changes.append(QJsonObject{{"op", "add"}, {"path", it.key()}, {"after", it.value()}});
		}
	}
	return {{"command", command}, {"changes", changes}};
}

}
