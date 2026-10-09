#ifndef LMMS_SVS_PARAMETER_PANEL_H
#define LMMS_SVS_PARAMETER_PANEL_H
#include "SVSCapabilities.h"
#include <QWidget>
#include <functional>

class QVBoxLayout;
namespace lmms::gui {
class SVSParameterPanel : public QWidget
{
public:
	using Setter = std::function<void(const QString&, const QJsonValue&)>;
	explicit SVSParameterPanel(QWidget* parent = nullptr);
	void refresh(const QVector<svs::Parameter>& schema, const QString& scope, const QVector<QJsonObject>& values,
		const QJsonObject& context, Setter setter);

private:
	struct Row
	{
		QWidget* container = nullptr;
		QWidget* editor = nullptr;
		QString type;
		svs::Parameter descriptor;
		svs::SelectedValue value;
	};
	QMap<QString, Row> m_rows;
	QVBoxLayout* m_layout;
	Setter m_setter;
	bool m_updating = false;
	void submit(const QString&, const QJsonValue&);
};
}
#endif
