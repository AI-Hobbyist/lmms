/*
 * LedCheckBox.cpp - class LedCheckBox, an improved QCheckBox
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

#include "LedCheckBox.h"

#include <QFontMetrics>
#include <QPainter>
#include <array>

#include "DeprecationHelper.h"
#include "embed.h"
#include "FontHelper.h"

namespace lmms::gui
{


static const auto names = std::array<QString, 3>
{
	"led_yellow", "led_green", "led_red"
} ;




LedCheckBox::LedCheckBox( const QString & _text, QWidget * _parent,
				const QString & _name, LedColor _color, bool legacyMode ) :
	AutomatableButton( _parent, _name ),
	m_text( _text ),
	m_legacyMode(legacyMode)
{
	setLedColor(_color);
}




LedCheckBox::LedCheckBox( QWidget * _parent,
				const QString & _name, LedColor _color, bool legacyMode ) :
	LedCheckBox( QString(), _parent, _name, _color, legacyMode )
{
}

void LedCheckBox::setText( const QString &s )
{
	m_text = s;
	onTextUpdated();
}




void LedCheckBox::paintEvent( QPaintEvent * pe )
{
	if (m_flatStyle)
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const int diameter = m_ledOffPixmap.deviceIndependentSize().height();
		const QRectF indicator(.5, (height() - diameter) / 2.0 + .5, diameter - 1, diameter - 1);
		const QColor active = m_ledColor == LedColor::Red ? m_errorColor
			: m_ledColor == LedColor::Yellow			  ? m_warningColor
														  : palette().brightText().color();
		p.setPen(
			isEnabled() && (hasFocus() || underMouse()) ? palette().brightText().color() : palette().mid().color());
		p.setBrush(model()->value() ? (isEnabled() ? active : palette().color(QPalette::Disabled, QPalette::Text))
									: palette().base().color());
		p.drawRoundedRect(indicator, 3, 3);
		p.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Text));
		const auto textRect = rect().adjusted(diameter + 5, 0, 0, 0);
		p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
			p.fontMetrics().elidedText(text(), Qt::ElideRight, textRect.width()));
		return;
	}
	if (!m_legacyMode)
	{
		paintNonLegacy(pe);
	}
	else
	{
		paintLegacy(pe);
	}
}




void LedCheckBox::setLedColor(LedColor color)
{
	m_ledColor = color;
	setCheckable( true );

	m_ledOnPixmap = embed::getIconPixmap(names[static_cast<std::size_t>(color)].toUtf8().constData());
	m_ledOffPixmap = embed::getIconPixmap("led_off");

	if (m_legacyMode){ setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE)); }

	setText( m_text );
}




void LedCheckBox::onTextUpdated()
{
	QFontMetrics const fm = fontMetrics();

	int const width = m_ledOffPixmap.width() + 5 + fm.horizontalAdvance(text());
	int const height
		= m_legacyMode && !m_flatStyle ? m_ledOffPixmap.height() : qMax(m_ledOffPixmap.height(), fm.height());

	setFixedSize(width, height);
}

void LedCheckBox::setFlatStyle(bool enabled)
{
	m_flatStyle = enabled;
	onTextUpdated();
	QWidget::update();
}

void LedCheckBox::changeEvent(QEvent* event)
{
	AutomatableButton::changeEvent(event);
	if (event->type() == QEvent::FontChange)
	{
		onTextUpdated();
	}
}

void LedCheckBox::paintLegacy(QPaintEvent * pe)
{
	QPainter p( this );
	p.setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE));

	p.drawPixmap(0, 0, model()->value() ? m_ledOnPixmap : m_ledOffPixmap);
	const auto label
		= p.fontMetrics().elidedText(text(), Qt::ElideRight, std::max(0, width() - m_ledOffPixmap.width() - 5));

	p.setPen( QColor( 64, 64, 64 ) );
	p.drawText(m_ledOffPixmap.width() + 4, 11, label);
	p.setPen( QColor( 255, 255, 255 ) );
	p.drawText(m_ledOffPixmap.width() + 3, 10, label);
}

void LedCheckBox::paintNonLegacy(QPaintEvent * pe)
{
	QPainter p(this);

	auto drawnPixmap = model()->value() ? m_ledOnPixmap : m_ledOffPixmap;

	p.drawPixmap(0, rect().height() / 2 - drawnPixmap.height() / 2, drawnPixmap);

	QRect r = rect();
	r -= QMargins(m_ledOffPixmap.width() + 5, 0, 0, 0);
	p.drawText(r, p.fontMetrics().elidedText(text(), Qt::ElideRight, r.width()));
}


} // namespace lmms::gui
