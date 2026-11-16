#ifndef LMMS_AGENT_CHAT_SETTINGS_DIALOG_H
#define LMMS_AGENT_CHAT_SETTINGS_DIALOG_H

#include <QDialog>

class QAbstractButton;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;

namespace lmms::agent::chat {

class ChatSettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ChatSettingsDialog(QWidget* parent = nullptr);
	void accept() override;

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void buildChrome();
	void buildGlobalPage();
	void buildApprovalPage();
	void selectPage(int page);
	void updateApprovalLabels();
	void reloadPrompt();

	QWidget* m_header = nullptr;
	QWidget* m_navigation = nullptr;
	QWidget* m_footer = nullptr;
	QScrollArea* m_scroll = nullptr;
	QStackedWidget* m_pages = nullptr;
	QDialogButtonBox* m_buttons = nullptr;
	QLineEdit* m_promptPath = nullptr;
	QAbstractButton* m_requestApproval = nullptr;
	QAbstractButton* m_noApproval = nullptr;
	QLabel* m_globalApproval = nullptr;
	QLabel* m_currentApproval = nullptr;
	QLabel* m_approvalCheck = nullptr;
};

} // namespace lmms::agent::chat

#endif
