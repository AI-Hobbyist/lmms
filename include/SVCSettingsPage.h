#pragma once
#include <QWidget>
#include <memory>
#include <vector>

#include "AutomatableModel.h"
class QLineEdit;
class QCheckBox;
class QLabel;
namespace lmms::gui {
class SVCSettingsPage : public QWidget
{
	Q_OBJECT
public:
	explicit SVCSettingsPage(QWidget* parent = nullptr);
	bool save();

private:
	struct Entry
	{
		QString id;
		QLineEdit* address;
		QLineEdit* token;
		QCheckBox* remember;
		QLabel* status;
	};
	std::vector<Entry> m_entries;
	std::vector<std::unique_ptr<FloatModel>> m_defaults;
	QLabel* m_defaultStatus;
};
} // namespace lmms::gui
