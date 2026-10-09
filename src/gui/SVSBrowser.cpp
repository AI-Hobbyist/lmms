#include "SVSBrowser.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QMap>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "PluginBrowser.h"
#include "SVSModel.h"
#include "embed.h"

namespace lmms::gui {
SVSBrowser::SVSBrowser(QWidget* parent)
	: SideBarWidget("SVS", embed::getIconPixmap("svs_track.svg").transformed(QTransform().rotate(90)), parent)
	, m_search(new QLineEdit(contentParent()))
	, m_tree(new QTreeWidget(contentParent()))
{
	auto* view = new QWidget(contentParent());
	auto* layout = new QVBoxLayout(view);
	layout->setContentsMargins(5, 5, 5, 5);
	layout->setSpacing(5);
	m_search->setObjectName("svsBrowserSearch");
	m_search->setPlaceholderText(tr("Search"));
	m_search->setMaxLength(64);
	m_search->setClearButtonEnabled(true);
	m_search->addAction(embed::getIconPixmap("zoom"), QLineEdit::LeadingPosition);
	m_tree->setObjectName("svsBrowserTree");
	m_tree->setHeaderHidden(true);
	m_tree->setIndentation(10);
	m_tree->setSelectionMode(QAbstractItemView::NoSelection);
	m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	layout->addWidget(m_search);
	layout->addWidget(m_tree);
	addContentWidget(view);
	connect(m_search, &QLineEdit::textChanged, this, &SVSBrowser::filter);
	connect(&svs::Registry::instance(), &svs::Registry::catalogChanged, this, &SVSBrowser::refresh);
	refresh();
}

void SVSBrowser::refresh()
{
	m_tree->clear();
	// Reuse the existing voice widget, avatar/resource loader and svsvoice drag/drop contract.
	static const PixmapLoader logo("sample_track");
	static Plugin::Descriptor descriptor{"svs", "Singing Voice Synthesis", "Native singing voice synthesis", "LMMS", 1,
		Plugin::Type::SVS, &logo, "", nullptr};
	auto& registry = svs::Registry::instance();
	QMap<QString, QTreeWidgetItem*> groups;
	for (const auto& engine : registry.engines())
	{
		const auto& voices = registry.voices();
		if (std::none_of(
				voices.begin(), voices.end(), [&engine](const auto& voice) { return voice.pluginId == engine.id; }))
		{
			continue;
		}
		auto* group = new QTreeWidgetItem(m_tree, {engine.name});
		group->setData(0, Qt::UserRole, engine.id);
		group->setExpanded(true);
		groups.insert(engine.id, group);
	}
	for (const auto& voice : registry.voices())
	{
		auto* group = groups.value(voice.pluginId);
		if (!group) { continue; }
		auto* item = new QTreeWidgetItem(group);
		PluginDescWidget::PluginKey key(
			&descriptor, voice.name, {{"pluginId", voice.pluginId}, {"voiceId", voice.id}, {"avatar", voice.avatar}});
		m_tree->setItemWidget(item, 0, new PluginDescWidget(key, m_tree));
	}
	filter(m_search->text());
}

void SVSBrowser::filter(const QString& text)
{
	for (int index = 0; index < m_tree->topLevelItemCount(); ++index)
	{
		auto* group = m_tree->topLevelItem(index);
		const auto engineMatches = group->text(0).contains(text, Qt::CaseInsensitive);
		bool visible = false;
		for (int child = 0; child < group->childCount(); ++child)
		{
			auto* item = group->child(child);
			const auto* widget = qobject_cast<PluginDescWidget*>(m_tree->itemWidget(item, 0));
			const auto matches = engineMatches || (widget && widget->name().contains(text, Qt::CaseInsensitive));
			item->setHidden(!matches);
			visible = matches || visible;
		}
		group->setHidden(!visible);
	}
}
} // namespace lmms::gui
