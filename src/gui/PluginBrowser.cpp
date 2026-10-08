/*
 * PluginBrowser.cpp - implementation of the plugin-browser
 *
 * Copyright (c) 2005-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "lmmsconfig.h"
#include "PluginBrowser.h"
#include "SVSImageLoader.h"
#ifdef LMMS_BUILD_WIN32
#include "vsthost/CatalogJobs.h"
#include <QTimer>
#include <QApplication>
#endif

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QTreeWidget>

#include "embed.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "SVSTrack.h"
#include "SVSModel.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "PluginFactory.h"

namespace lmms::gui
{


PluginBrowser::PluginBrowser( QWidget * _parent ) :
	SideBarWidget( tr( "Instrument Plugins" ),
				embed::getIconPixmap( "plugins" ).transformed( QTransform().rotate( 90 ) ), _parent )
{
	setWindowTitle( tr( "Instrument browser" ) );
	m_view = new QWidget( contentParent() );
	//m_view->setFrameShape( QFrame::NoFrame );

	addContentWidget( m_view );

	auto view_layout = new QVBoxLayout(m_view);
	view_layout->setContentsMargins(5, 5, 5, 5);
	view_layout->setSpacing( 5 );


	auto hint = new QLabel( tr( "Drag an instrument "
					"into either the Song Editor, the "
					"Pattern Editor or an "
					"existing instrument track." ),
								m_view );
	hint->setWordWrap( true );

	auto searchBar = new QLineEdit(m_view);
	searchBar->setPlaceholderText(tr("Search"));
	searchBar->setMaxLength(64);
	searchBar->setClearButtonEnabled(true);
	searchBar->addAction(embed::getIconPixmap("zoom"), QLineEdit::LeadingPosition);

	m_descTree = new QTreeWidget( m_view );
	m_descTree->setColumnCount( 1 );
	m_descTree->header()->setVisible( false );
	m_descTree->setIndentation( 10 );
	m_descTree->setSelectionMode( QAbstractItemView::NoSelection );

	connect( searchBar, SIGNAL( textEdited( const QString& ) ),
			this, SLOT( onFilterChanged( const QString& ) ) );

	view_layout->addWidget( hint );
	view_layout->addWidget( searchBar );
	view_layout->addWidget( m_descTree );

	// Add plugins to the tree
	addPlugins();
	connect(&svs::Registry::instance(), &svs::Registry::catalogChanged, this, [this] {
		refreshSvsVoices();updateRootVisibilities();if(const auto* search=m_view->findChild<QLineEdit*>()) onFilterChanged(search->text());
	});
#ifdef LMMS_BUILD_WIN32
	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this, published = std::uint64_t{0}]() mutable {
		if (QApplication::mouseButtons() != Qt::NoButton) { return; }
		for (auto* loader : findChildren<QThread*>()) { if (loader->isRunning()) { return; } }
		if (const auto* jobs = Engine::vstCatalog(); jobs && jobs->snapshot().published != published)
		{ published = jobs->snapshot().published; addPlugins(); updateRootVisibilities();
			if (const auto* search = m_view->findChild<QLineEdit*>()) { onFilterChanged(search->text()); } }
	});
	timer->start(100);
#endif

	// Resize
	m_descTree->header()->setSectionResizeMode( QHeaderView::ResizeToContents );

	// Hide empty roots
	updateRootVisibilities();
}


void PluginBrowser::updateRootVisibility( int rootIndex )
{
	QTreeWidgetItem * root = m_descTree->topLevelItem( rootIndex );
	root->setHidden( !root->childCount() );
}


void PluginBrowser::updateRootVisibilities()
{
	int rootCount = m_descTree->topLevelItemCount();
	for (int rootIndex = 0; rootIndex < rootCount; ++rootIndex)
	{
		updateRootVisibility( rootIndex );
	}
}


void PluginBrowser::onFilterChanged( const QString & filter )
{
	const auto filterItem = [this, &filter](auto&& self, QTreeWidgetItem* item, bool parentMatches) -> bool
	{
		const auto* widget = static_cast<PluginDescWidget*>(m_descTree->itemWidget(item, 0));
		const bool matches = parentMatches || (widget ? widget->name() : item->text(0)).contains(filter, Qt::CaseInsensitive);
		bool visible = widget ? matches : matches && !item->childCount();
		for (int index = 0; index < item->childCount(); ++index)
		{
			visible = self(self, item->child(index), matches) || visible;
		}
		item->setHidden(!visible);
		return visible;
	};
	int rootCount = m_descTree->topLevelItemCount();
	for (int rootIndex = 0; rootIndex < rootCount; ++rootIndex)
	{
		QTreeWidgetItem * root = m_descTree->topLevelItem( rootIndex );

		int itemCount = root->childCount();
		for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
		{
			QTreeWidgetItem * item = root->child( itemIndex );
			filterItem(filterItem, item, false);
		}
	}
}


void PluginBrowser::addPlugins()
{
	// Add a root node to the plugin tree with the specified `label` and return it
	const auto addRoot = [this](auto label)
	{
		const auto root = new QTreeWidgetItem();
		root->setText(0, label);
		m_descTree->addTopLevelItem(root);
		return root;
	};

	// Add the plugin identified by `key` to the tree under the root node `root`
	const auto addPlugin = [this](const auto& key, auto root)
	{
		const auto item = new QTreeWidgetItem();
		root->addChild(item);
		m_descTree->setItemWidget(item, 0, new PluginDescWidget(key, m_descTree));
	};

	// Remove any existing plugins from the tree
	m_descTree->clear();

	// Fetch and sort all instrument plugin descriptors
	auto descs = getPluginFactory()->descriptors(Plugin::Type::Instrument);
	std::sort(descs.begin(), descs.end(),
		[](auto d1, auto d2)
		{
			return qstricmp(d1->displayName, d2->displayName) < 0;
		}
	);

	// Add a root node to the tree for native LMMS plugins
	const auto lmmsRoot = addRoot("LMMS");
	lmmsRoot->setExpanded(true);
	const auto svsRoot = addRoot("Singing Voice Synthesis");
	svsRoot->setExpanded(true);
	refreshSvsVoices();

	// Add all of the descriptors to the tree
	for (const auto desc : descs)
	{
		if (desc->subPluginFeatures)
		{
			// Fetch and sort all subplugins for this plugin descriptor
			auto subPluginKeys = Plugin::Descriptor::SubPluginFeatures::KeyList{};
			desc->subPluginFeatures->listSubPluginKeys(desc, subPluginKeys);
			std::sort(subPluginKeys.begin(), subPluginKeys.end(),
				[](const auto& l, const auto& r)
				{
					return QString::compare(l.displayName(), r.displayName(), Qt::CaseInsensitive) < 0;
				}
			);

			// Create a root node for this plugin and add the subplugins under it
			const bool vst = QString::fromUtf8(desc->name) == "vestige";
				if (vst) { addPlugin(Plugin::Descriptor::SubPluginFeatures::Key(desc, "VeSTige"), lmmsRoot); }
				const auto root = addRoot(vst ? tr("VST Instruments") : QString::fromUtf8(desc->displayName));
				if (vst) { root->setExpanded(true); }
			for (const auto& key : subPluginKeys) { addPlugin(key, root); }
		}
		else
		{
			addPlugin(Plugin::Descriptor::SubPluginFeatures::Key(desc, desc->name), lmmsRoot);
		}
	}
}




void PluginBrowser::refreshSvsVoices()
{
	QTreeWidgetItem* root=nullptr;for(int i=0;i<m_descTree->topLevelItemCount();++i) if(m_descTree->topLevelItem(i)->text(0)=="Singing Voice Synthesis") {root=m_descTree->topLevelItem(i);break;}if(!root) return;
	qDeleteAll(root->takeChildren());static const PixmapLoader logo("sample_track");static Plugin::Descriptor descriptor{"svs","Singing Voice Synthesis","Native singing voice synthesis","LMMS",1,Plugin::Type::SVS,&logo,"",nullptr};
	auto& registry = svs::Registry::instance();
	QMap<QString, QTreeWidgetItem*> groups;
	for (const auto& engine : registry.engines())
	{
		const auto& voices = registry.voices();
		if (std::none_of(voices.begin(), voices.end(), [&engine](const auto& voice) { return voice.pluginId == engine.id; })) { continue; }
		auto* group = new QTreeWidgetItem(root);
		group->setText(0, engine.name);
		group->setData(0, Qt::UserRole, engine.id);
		group->setExpanded(true);
		groups.insert(engine.id, group);
	}
	for (const auto& voice : registry.voices())
	{
		auto* group = groups.value(voice.pluginId);
		if (!group) { continue; }
		auto* item = new QTreeWidgetItem(group);
		PluginDescWidget::PluginKey key(&descriptor, voice.name, {{"pluginId", voice.pluginId}, {"voiceId", voice.id}, {"avatar", voice.avatar}});
		m_descTree->setItemWidget(item, 0, new PluginDescWidget(key, m_descTree));
	}
}

PluginDescWidget::PluginDescWidget(const PluginKey &_pk,
							QWidget * _parent ) :
	QWidget( _parent ),
	m_pluginKey( _pk ),
	m_logo( _pk.logo()->pixmap() ),
	m_mouseOver( false )
{
	setFixedHeight( DEFAULT_HEIGHT );
	setMouseTracking( true );
	setCursor( Qt::PointingHandCursor );
	setToolTip(_pk.desc->subPluginFeatures
		? _pk.description()
		: tr(_pk.desc->description));
	if (_pk.desc->type == Plugin::Type::SVS)
	{
		QPixmap avatar(_pk.attributes.value("avatar"));
		if (!avatar.isNull()) { m_logo = avatar; }
		else if(_pk.attributes.value("avatar").startsWith("svs-resource:")) {auto* loader=new SVSImageLoader(this);loader->changed=[this,loader]{if(!loader->image().isNull()) {m_logo=QPixmap::fromImage(loader->image());update();}};loader->request({},_pk.attributes.value("avatar"),QSize(64,64));}
	}
}




QString PluginDescWidget::name() const
{
	if (m_pluginKey.desc->type == Plugin::Type::SVS) { return m_pluginKey.name; }
	return m_pluginKey.displayName();
}




void PluginDescWidget::paintEvent( QPaintEvent * )
{

	QPainter p( this );

	// Paint everything according to the style sheet
	QStyleOption o;
	o.initFrom( this );
	style()->drawPrimitive( QStyle::PE_Widget, &o, &p, this );

	// Draw the rest
	const int s = 16 + ( 32 * ( qBound( 24, height(), 60 ) - 24 ) ) /
								( 60 - 24 );
	const QSize logo_size( s, s );
	QPixmap logo = m_logo.scaled( logo_size, Qt::KeepAspectRatio,
						Qt::SmoothTransformation );
	p.drawPixmap( 4, 4, logo );

	QFont f = p.font();
	if ( m_mouseOver )
	{
		f.setBold( true );
	}

	p.setFont( f );
	p.drawText( 10 + logo_size.width(), 15, name());
}


#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
void PluginDescWidget::enterEvent(QEnterEvent* event)
#else
void PluginDescWidget::enterEvent(QEvent* event)
#endif
{
	m_mouseOver = true;

	QWidget::enterEvent(event);
}




void PluginDescWidget::leaveEvent( QEvent * _e )
{
	m_mouseOver = false;

	QWidget::leaveEvent( _e );
}




void PluginDescWidget::mousePressEvent( QMouseEvent * _me )
{
	if (m_pluginKey.desc->type == Plugin::Type::SVS)
	{
		if (_me->button() == Qt::LeftButton) { new StringPairDrag("svsvoice", m_pluginKey.attributes.value("pluginId") + "/" + m_pluginKey.attributes.value("voiceId"), m_logo, this); }
		return;
	}
	Engine::setDndPluginKey(&m_pluginKey);
	if ( _me->button() == Qt::LeftButton )
	{
		new StringPairDrag("instrument",
			QString::fromUtf8(m_pluginKey.desc->name), m_logo, this);
		leaveEvent( _me );
	}
}


void PluginDescWidget::contextMenuEvent(QContextMenuEvent* e)
{
	QMenu contextMenu(this);
	if (m_pluginKey.desc->type == Plugin::Type::SVS)
	{
		contextMenu.addAction(tr("Send to new SVS track"), [this] {
			auto* track = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, Engine::getSong()));
			track->bindVoice(m_pluginKey.attributes.value("pluginId"), m_pluginKey.attributes.value("voiceId"));
		});
		contextMenu.exec(e->globalPos());
		return;
	}
	contextMenu.addAction(
		tr("Send to new instrument track"),
		[=, this]{ openInNewInstrumentTrack(m_pluginKey.desc->name); }
	);
	contextMenu.exec(e->globalPos());
}


void PluginDescWidget::openInNewInstrumentTrack(QString value)
{
	Engine::setDndPluginKey(&m_pluginKey);
	TrackContainer* tc = Engine::getSong();
	auto it = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, tc));
	auto ilt = new InstrumentLoaderThread(this, it, value);
	ilt->start();
}


} // namespace lmms::gui
