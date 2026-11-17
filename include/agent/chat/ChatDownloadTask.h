#ifndef LMMS_AGENT_CHAT_DOWNLOAD_TASK_H
#define LMMS_AGENT_CHAT_DOWNLOAD_TASK_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>

class QProcess;

namespace lmms::agent::chat {

// Downloads one http/https asset with aria2c. The file is first written to a
// temporary folder inside the target directory and only moved into place after
// it has been checked; downloaded files are never executed.
class ChatDownloadTask : public QObject
{
	Q_OBJECT
public:
	explicit ChatDownloadTask(QObject* parent = nullptr);
	~ChatDownloadTask() override;

	static QString command();
	static QJsonObject definition();

	// Resolves the arguments into a concrete plan {url, directory, fileName,
	// target, overwrite}. A relative directory needs a saved project.
	static bool plan(const QJsonObject& arguments, QJsonObject& plan, QString& error);

	void start(const QJsonObject& plan, const QString& aria2Path, const QString& sevenZipPath);
	void abort();
	bool active() const;

signals:
	void progress(const QJsonObject& progress);
	void finished(const QJsonObject& result);

private:
	void readOutput();
	void complete(int exitCode);
	void cleanup();

	// Archive phase: list and check every member, extract to a temporary
	// folder, re-check what was written, then move files into place.
	void run7Zip(const QStringList& arguments, const std::function<void(int, QByteArray)>& done);
	void listArchive();
	void extractArchive();
	void failExtraction(const QString& code, const QString& reason);

	QPointer<QProcess> m_process;
	QString m_sevenZip;
	QJsonObject m_downloaded;
	QJsonObject m_plan;
	QString m_temporary;
	QByteArray m_output;
	QJsonObject m_lastProgress;
};

} // namespace lmms::agent::chat

#endif // LMMS_AGENT_CHAT_DOWNLOAD_TASK_H
