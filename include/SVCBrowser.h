#pragma once
#include "SideBarWidget.h"
class QTreeWidget;
namespace lmms::gui {
class SVCBrowser : public SideBarWidget
{
	Q_OBJECT
public:
	explicit SVCBrowser(QWidget* parent);

private:
	void refresh();
	QTreeWidget* m_tree;
};
} // namespace lmms::gui
