#pragma once

#include "SideBarWidget.h"

class QLineEdit;
class QTreeWidget;

namespace lmms::gui {
class SVSBrowser : public SideBarWidget
{
	Q_OBJECT
public:
	explicit SVSBrowser(QWidget* parent);

private:
	void refresh();
	void filter(const QString& text);
	QLineEdit* m_search;
	QTreeWidget* m_tree;
};
} // namespace lmms::gui
