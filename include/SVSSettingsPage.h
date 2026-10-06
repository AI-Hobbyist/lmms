#ifndef LMMS_SVS_SETTINGS_PAGE_H
#define LMMS_SVS_SETTINGS_PAGE_H
#include "SVSParameterPanel.h"
class QComboBox;
class QLabel;
class QTabWidget;
class QSlider;
namespace lmms::gui {
class SVSSettingsPage : public QWidget {
public:
 explicit SVSSettingsPage(QWidget* parent=nullptr);
 void save();
 static QString engineLabel(const svs::Voice& voice);
private:
 void refreshEngine();
 QComboBox* m_backend;
 QComboBox* m_device;
 QSlider* m_aiSteps;
 QTabWidget* m_engine;
 QLabel* m_status;
 SVSParameterPanel* m_parameters;
 QVector<svs::Voice> m_voices;
 QMap<QString,QJsonObject> m_values;
 QVector<svs::Parameter> m_schema;
 unsigned m_request=0;
};
}
#endif
