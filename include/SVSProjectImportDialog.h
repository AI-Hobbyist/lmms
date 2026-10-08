#ifndef LMMS_SVS_PROJECT_IMPORT_DIALOG_H
#define LMMS_SVS_PROJECT_IMPORT_DIALOG_H
#include "SVSProjectMapper.h"
#include <QDialog>
#include <QWidget>
#include <functional>
class QComboBox;
class QLabel;
class QDialogButtonBox;
namespace lmms::gui {
class SVSProjectOptionsWidget : public QWidget {
public:
 SVSProjectOptionsWidget(const QJsonObject& defaults,const QJsonObject& schema,QWidget* parent=nullptr);
 QJsonObject options() const;
private:
 QMap<QString,std::function<QJsonValue()>> m_values;
};
class SVSProjectImportDialog : public QDialog {
public:
 explicit SVSProjectImportDialog(const QJsonObject& format,QWidget* parent=nullptr);
 svs::ProjectVoice selectedVoice() const {return m_selected;}
 QJsonObject options() const;
 void accept() override;
private:
 void refreshVoices();
 QComboBox* m_voices;
 QLabel* m_status;
 QDialogButtonBox* m_buttons;
 SVSProjectOptionsWidget* m_options;
 svs::ProjectVoice m_selected;
};
}
#endif
