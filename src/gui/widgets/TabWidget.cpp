/*
	* TabWidget.cpp - tabwidget for LMMS
	*
	* Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
	*
	* This file is part of LMMS - https://lmms.io
	*
	* This program is free software; you can redistribute it and/or
	* modify it under the terms of the GNU General Public
	* License as published by the Free Software Foundation; either
	* version 2 of the License, or (at your option) any later version.
	*
	* This program is distributed in the hope that it will be useful,
	* but WITHOUT ANY WARRANTY; without even the implied warranty of
	* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	* General Public License for more details.
	*
	* You should have received a copy of the GNU General Public
	* License along with this program (see COPYING); if not, write to the
	* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
	* Boston, MA 02110-1301 USA.
	*
	*/


#include "TabWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QToolTip>
#include <QWheelEvent>

#include "DeprecationHelper.h"
#include "embed.h"
#include "FontHelper.h"

namespace lmms::gui
{

TabWidget::TabWidget(const QString& caption, QWidget* parent, bool usePixmap,
					 bool resizable) :
	QWidget(parent),
	m_resizable(resizable),
	m_activeTab(0),
	m_caption(caption),
	m_usePixmap(usePixmap),
	m_tabText(0, 0, 0),
	m_tabTitleText(0, 0, 0),
	m_tabSelected(0, 0, 0),
	m_tabTextSelected(0, 0, 0),
	m_tabBackground(0, 0, 0),
	m_tabBorder(0, 0, 0)
{

	// Create taller tabbar when it's to display artwork tabs
	m_tabbarHeight = usePixmap ? GRAPHIC_TAB_HEIGHT : TEXT_TAB_HEIGHT;

	m_tabheight = caption.isEmpty() ? m_tabbarHeight - 3 : m_tabbarHeight - 4;

	setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE));

	setAutoFillBackground(true);
	QColor bg_color = QApplication::palette().color(QPalette::Active, QPalette::Window).darker(132);
	QPalette pal = palette();
	pal.setColor(QPalette::Window, bg_color);
	setPalette(pal);

}

void TabWidget::setFlatStyle(bool enabled)
{
	if (m_flatStyle == enabled)
	{
		return;
	}
	m_flatStyle = enabled;
	m_tabbarHeight = m_usePixmap ? GRAPHIC_TAB_HEIGHT : TEXT_TAB_HEIGHT;
	if (enabled)
	{
		m_tabbarHeight = std::max<int>(m_tabbarHeight, fontMetrics().height() + 4);
	}
	m_tabheight = m_tabbarHeight - (m_caption.isEmpty() ? 3 : 4);
	for (const auto& widget : m_widgets)
	{
		widget.w->move(2, m_tabbarHeight - 1);
		if (!m_resizable)
		{
			widget.w->setFixedSize(width() - 4, height() - m_tabbarHeight);
		}
	}
	updateGeometry();
	update();
}

void TabWidget::addTab(QWidget* w, const QString& name, const char* pixmap, int idx)
{
	// Append tab when position is not given
	if (idx < 0/* || m_widgets.contains(idx) == true*/)
	{
		while(m_widgets.contains(++idx) == true)
		{
		}
	}

	// Tab's width when it is a text tab. This isn't correct for artwork tabs, but it's fixed later during the PaintEvent
	int tab_width = fontMetrics().horizontalAdvance(name) + 10;

	// Register new tab
	widgetDesc d = {w, pixmap, name, tab_width};
	m_widgets[idx] = d;

	// Position tab's window
	if (!m_resizable)
	{
		w->setFixedSize(width() - 4, height() - m_tabbarHeight);
	}
	w->move(2, m_tabbarHeight - 1);
	w->hide();

	// Show tab's window if it's active
	if (m_widgets.contains(m_activeTab))
	{
		// make sure new tab doesn't overlap current widget
		m_widgets[m_activeTab].w->show();
		m_widgets[m_activeTab].w->raise();
	}
}




void TabWidget::setActiveTab(int idx)
{
	if (m_widgets.contains(idx))
	{
		int old_active = m_activeTab;
		m_activeTab = idx;
		m_widgets[m_activeTab].w->raise();
		m_widgets[m_activeTab].w->show();
		if (old_active != idx && m_widgets.contains(old_active))
		{
			m_widgets[old_active].w->hide();
		}
		update();
	}
}


// Return the index of the tab at position "pos"
QMap<int, QRect> TabWidget::tabRects() const
{
	QMap<int, QRect> result;
	int x = m_caption.isEmpty() ? 4 : 14 + fontMetrics().horizontalAdvance(m_caption);
	const int iconWidth = m_widgets.isEmpty() ? 0 : std::max(0, (width() - x) / static_cast<int>(m_widgets.size()));
	for (auto it = m_widgets.cbegin(); it != m_widgets.cend(); ++it)
	{
		const int tabWidth = m_usePixmap ? iconWidth : fontMetrics().horizontalAdvance(it->name) + 10;
		result.insert(it.key(), QRect(x, 2, tabWidth, m_tabbarHeight - 4));
		x += tabWidth;
	}
	return result;
}

int TabWidget::findTabAtPos(const QPoint& pos)
{
	const auto rectangles = tabRects();
	for (auto it = rectangles.cbegin(); it != rectangles.cend(); ++it)
		if (it.value().contains(pos))
		{
			return it.key();
		}
	return -1;
}

bool TabWidget::event(QEvent* event)
{

	if (event->type() == QEvent::ToolTip)
	{
		auto helpEvent = static_cast<QHelpEvent*>(event);

		int idx = findTabAtPos(helpEvent->pos());

		if (idx != -1)
		{
			// Display tab's tooltip
			QToolTip::showText(helpEvent->globalPos(), m_widgets[idx].name);
		}
		else
		{
			// The tooltip event doesn't relate to any tab, let's ignore it
			QToolTip::hideText();
			event->ignore();
		}

		return true;
	}

	// not a Tooltip event, let's propagate it to the other event handlers
	return QWidget::event(event);
}


// Activate tab when clicked
void TabWidget::mousePressEvent(QMouseEvent* me)
{
	// Find index of tab that has been clicked
	const int idx = findTabAtPos(position(me));

	// When found, activate tab that has been clicked
	if (idx != -1)
	{
		setActiveTab(idx);
		update();
		return;
	}
}




void TabWidget::resizeEvent(QResizeEvent*)
{
	if (!m_resizable)
	{
		for (const auto& widget : m_widgets)
		{
			widget.w->setFixedSize(width() - 4, height() - m_tabbarHeight);
		}
	}
}




void TabWidget::paintEvent(QPaintEvent* pe)
{
	QPainter p(this);
	p.setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE));

	if (m_flatStyle)
	{
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(tabBorder());
		p.setBrush(palette().window());
		p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), m_cornerRadius, m_cornerRadius);
		p.fillRect(1, 1, width() - 2, m_tabbarHeight - 2, tabBackground());
		if (!m_caption.isEmpty())
		{
			p.setPen(tabTitleText());
			p.drawText(QRect(5, 1, fontMetrics().horizontalAdvance(m_caption) + 4, m_tabbarHeight - 2),
				Qt::AlignVCenter, m_caption);
		}
		const auto rectangles = tabRects();
		for (auto it = m_widgets.cbegin(); it != m_widgets.cend(); ++it)
		{
			const auto tabRect = rectangles.value(it.key());
			if (it.key() == m_activeTab)
			{
				p.setPen(Qt::NoPen);
				p.setBrush(tabSelected());
				p.drawRoundedRect(QRectF(tabRect).adjusted(.5, 0, -.5, 0),
					std::min(m_cornerRadius, tabRect.height() / 2.0), std::min(m_cornerRadius, tabRect.height() / 2.0));
			}
			if (m_usePixmap)
			{
				const auto artwork = embed::getIconPixmap(it->pixmap);
				p.drawPixmap(
					tabRect.center().x() - artwork.width() / 2, (m_tabbarHeight - artwork.height()) / 2, artwork);
			}
			else
			{
				p.setPen(it.key() == m_activeTab ? tabTextSelected() : tabText());
				p.drawText(tabRect.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, it->name);
			}
		}
		return;
	}

	// Draw background
	QBrush bg_color = p.background();
	p.fillRect(0, 0, width() - 1, height() - 1, bg_color);

	// Draw external borders
	p.setPen(tabBorder());
	p.drawRect(0, 0, width() - 1, height() - 1);

	// Draw tabs' bar background
	p.fillRect(1, 1, width() - 2, m_tabheight + 2, tabBackground());

	// Draw title, if any
	if (!m_caption.isEmpty())
	{
		p.setPen(tabTitleText());
		p.drawText(5, 11, m_caption);
	}

	const auto rectangles = tabRects();
	auto first = m_widgets.begin();
	auto last = m_widgets.end();
	// Draw all tabs
	p.setPen(tabText());
	for (widgetStack::iterator it = first ; it != last ; ++it)
	{
		auto & currentWidgetDesc = *it;
		const auto tabRect = rectangles.value(it.key());
		const int tab_x_offset = tabRect.x();
		const int tab_width = tabRect.width();

		// Draw a text tab or a artwork tab.
		if (m_usePixmap)
		{
			// Fixes tab's width, because original size is only correct for text tabs


			// Get artwork
			QPixmap artwork(embed::getIconPixmap(currentWidgetDesc.pixmap));

			// Highlight active tab
			if (it.key() == m_activeTab)
			{
				p.fillRect(tab_x_offset, 0, tab_width, m_tabbarHeight - 1, tabSelected());
			}

			// Draw artwork
			p.drawPixmap(tab_x_offset + (tab_width - artwork.width()) / 2, 1, artwork);
		}
		else
		{
			// Highlight tab when active
			if (it.key() == m_activeTab)
			{
				p.fillRect(tab_x_offset, 2, tab_width - 6, m_tabbarHeight - 4, tabSelected());
				p.setPen(tabTextSelected());
				p.drawText(tab_x_offset + 3, m_tabheight + 1, currentWidgetDesc.name);
			}
			else
			{
				// Draw text
				p.setPen(tabText());
				p.drawText(tab_x_offset + 3, m_tabheight + 1, currentWidgetDesc.name);
			}
		}


	}
}




// Switch between tabs with mouse wheel
void TabWidget::wheelEvent(QWheelEvent* we)
{
	if (we->position().toPoint().y() > m_tabheight)
	{
		return;
	}

	we->accept();
	const bool next = we->angleDelta().y() < 0;
	auto it = m_widgets.constFind(m_activeTab);
	if (it == m_widgets.cend())
	{
		return;
	}
	if (next)
	{
		if (++it == m_widgets.cend())
		{
			return;
		}
	}
	else
	{
		if (it == m_widgets.cbegin())
		{
			return;
		}
		--it;
	}
	setActiveTab(it.key());
}




// Let parent widgets know how much space this tab widget needs
QSize TabWidget::minimumSizeHint() const
{
	if (m_resizable || m_flatStyle)
	{
		int maxWidth = 0, maxHeight = 0;
		for (const auto& widget : m_widgets)
		{
			maxWidth = std::max(maxWidth, widget.w->minimumSizeHint().width());
			maxHeight = std::max(maxHeight, widget.w->minimumSizeHint().height());
		}
		// "-1" :
		// in "addTab", under "Position tab's window", the widget is
		// moved up by 1 pixel
		return QSize(maxWidth + 4, maxHeight + m_tabbarHeight - 1);
	}
	else { return QWidget::minimumSizeHint(); }
}




QSize TabWidget::sizeHint() const
{
	if (m_resizable || m_flatStyle)
	{
		int maxWidth = 0, maxHeight = 0;
		for (const auto& widget : m_widgets)
		{
			maxWidth = std::max(maxWidth, widget.w->sizeHint().width());
			maxHeight = std::max(maxHeight, widget.w->sizeHint().height());
		}
		// "-1" :
		// in "addTab", under "Position tab's window", the widget is
		// moved up by 1 pixel
		return QSize(maxWidth + 4, maxHeight + m_tabbarHeight - 1);
	}
	else { return QWidget::sizeHint(); }
}




// Return the color to be used to draw a TabWidget's title text (if any)
QColor TabWidget::tabTitleText() const
{
	return m_tabTitleText;
}

// Set the color to be used to draw a TabWidget's title text (if any)
void TabWidget::setTabTitleText(const QColor& c)
{
	m_tabTitleText = c;
}

// Return the color to be used to draw a TabWidget's text (if any)
QColor TabWidget::tabText() const
{
	return m_tabText;
}

// Set the color to be used to draw a TabWidget's text (if any)
void TabWidget::setTabText(const QColor& c)
{
	m_tabText = c;
}

// Return the color to be used to highlight a TabWidget'selected tab (if any)
QColor TabWidget::tabSelected() const
{
	return m_tabSelected;
}

// Set the color to be used to highlight a TabWidget'selected tab (if any)
void TabWidget::setTabSelected(const QColor& c)
{
	m_tabSelected = c;
}

// Return the text color of the selected tab
QColor TabWidget::tabTextSelected() const
{
	return m_tabTextSelected;
}

// Set the text color of the selected tab
void TabWidget::setTabTextSelected(const QColor& c)
{
	m_tabTextSelected = c;
}

// Return the color to be used for the TabWidget's background
QColor TabWidget::tabBackground() const
{
	return m_tabBackground;
}

// Set the color to be used for the TabWidget's background
void TabWidget::setTabBackground(const QColor& c)
{
	m_tabBackground = c;
}

// Return the color to be used for the TabWidget's borders
QColor TabWidget::tabBorder() const
{
	return m_tabBorder;
}

// Set the color to be used for the TabWidget's borders
void TabWidget::setTabBorder(const QColor& c)
{
	m_tabBorder = c;
}


} // namespace lmms::gui
