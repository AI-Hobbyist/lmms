#ifndef LMMS_AGENT_CHAT_DOWNLOAD_TASK_H
#define LMMS_AGENT_CHAT_DOWNLOAD_TASK_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>

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

	void start(const QJsonObject& plan, const QString& aria2Path);
	void abort();
	bool active() const;

signals:
	void progress(const QJsonObject& progress);
	void finished(const QJsonObject& result);

private:
	void readOutput();
	void complete(int exitCode);
	void cleanup();

	QPointer<QProcess> m_process;
	QJsonObject m_plan;
	QString m_temporary;
	QByteArray m_output;
	QJsonObject m_lastProgress;
};

} // namespace lmms::agent::chat

#endif // LMMS_AGENT_CHAT_DOWNLOAD_TASK_H
