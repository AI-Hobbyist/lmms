#ifndef LMMS_AGENT_CHAT_GENERATION_PAGE_H
#define LMMS_AGENT_CHAT_GENERATION_PAGE_H

#include <QWidget>

#include "agent/chat/ChatConnection.h"

class QLabel;
class QSlider;
class QDoubleSpinBox;
class QToolButton;

namespace lmms::agent::chat {

class ChatGenerationPage : public QWidget
{
	Q_OBJECT
public:
	explicit ChatGenerationPage(QWidget* parent = nullptr);
	void setConnection(const ChatConnection& connection, bool requestApproval);
	ChatConnection connection() const;

private:
	void updatePreview();
	ChatConnection m_connection;
	QVector<QToolButton*> m_levels;
	QDoubleSpinBox* m_temperature = nullptr;
	QSlider* m_slider = nullptr;
	QLabel* m_model = nullptr;
	QLabel* m_reasoningPreview = nullptr;
	QLabel* m_temperaturePreview = nullptr;
	QLabel* m_approval = nullptr;
};

} // namespace lmms::agent::chat
#endif
