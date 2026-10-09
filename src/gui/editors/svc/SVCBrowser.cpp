#include "SVCBrowser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Engine.h"
#include "SVCCatalog.h"
#include "SVCTrack.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "embed.h"
namespace lmms::gui {
namespace {
class SVCTree final : public QTreeWidget
{
public:
	using QTreeWidget::QTreeWidget;

protected:
	void startDrag(Qt::DropActions) override
	{
		const auto* item = currentItem();
		if (!item || item->isDisabled() || !item->data(0, Qt::UserRole).isValid()) { return; }
		new StringPairDrag(
			"svcselection", item->data(0, Qt::UserRole).toString(), embed::getIconPixmap("svc_track.svg"), this);
	}
};
void attach(QTreeWidgetItem* item, const QJsonObject& selection, const QJsonObject& metadata)
{
	item->setData(0, Qt::UserRole, QString::fromUtf8(QJsonDocument(selection).toJson(QJsonDocument::Compact)));
	item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
	item->setDisabled(!metadata.value("available").toBool(true));
	item->setToolTip(0, metadata.value("reason").toString());
}
} // namespace
SVCBrowser::SVCBrowser(QWidget* parent)
	: SideBarWidget("SVC", embed::getIconPixmap("svc_track.svg").transformed(QTransform().rotate(90)), parent)
	, m_tree(new SVCTree(contentParent()))
	, m_search(new QLineEdit(contentParent()))
{
	auto* view = new QWidget(contentParent());
	auto* layout = new QVBoxLayout(view);
	layout->setContentsMargins(5, 5, 5, 5);
	layout->setSpacing(5);
	m_search->setObjectName("svcBrowserSearch");
	m_search->setPlaceholderText(tr("Search"));
	m_search->setMaxLength(64);
	m_search->setClearButtonEnabled(true);
	m_search->addAction(embed::getIconPixmap("zoom"), QLineEdit::LeadingPosition);
	layout->addWidget(m_search);
	layout->addWidget(m_tree);
	connect(m_search, &QLineEdit::textChanged, this, &SVCBrowser::filter);
	m_tree->setObjectName("svcBrowserTree");
	m_tree->setHeaderHidden(true);
	m_tree->setIndentation(10);
	m_tree->setDragEnabled(true);
	addContentWidget(view);
	connect(&svc::Catalog::instance(), &svc::Catalog::changed, this, &SVCBrowser::refresh);
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem* item) {
		if (item->isDisabled() || !item->data(0, Qt::UserRole).isValid()) { return; }
		auto* track = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, Engine::getSong()));
		track->setSelection(QJsonDocument::fromJson(item->data(0, Qt::UserRole).toString().toUtf8()).object());
	});
	refresh();
}
void SVCBrowser::refresh()
{
	m_tree->clear();
	for (const auto& engine : svc::Catalog::instance().engines())
	{
		if (!engine.api) { continue; }
		auto* branch = new QTreeWidgetItem(m_tree, {engine.name});
		branch->setToolTip(0, svc::Catalog::instance().status(engine.id));
		for (const auto& value : engine.capabilities.value("models").toArray())
		{
			const auto model = value.toObject();
			auto* leaf = new QTreeWidgetItem(branch, {model.value("name").toString(model.value("id").toString())});
			leaf->setIcon(0, embed::getIconPixmap("svc_track.svg"));
			QJsonObject selection{{"engine_id", engine.id}, {"model_id", model.value("id")}};
			const auto weights = model.value("weights").toArray();
			if (!weights.isEmpty() && (weights.size() == 1 || !model.contains("parameters")))
			{
				selection.insert("weight_id", weights.first().toObject().value("id"));
			}
			const auto speakers = model.value("speakers").toArray();
			if (speakers.size() <= 1)
			{
				if (!speakers.isEmpty()) { selection.insert("speaker_id", speakers.first().toObject().value("id")); }
				attach(leaf, selection, model);
				if (!engine.api) { leaf->setDisabled(true); }
			}
			else
			{
				leaf->setDisabled(!model.value("available").toBool(true));
				for (const auto& entry : speakers)
				{
					const auto speaker = entry.toObject();
					selection.insert("speaker_id", speaker.value("id"));
					auto* child
						= new QTreeWidgetItem(leaf, {speaker.value("name").toString(speaker.value("id").toString())});
					child->setIcon(0, embed::getIconPixmap("svc_track.svg"));
					attach(child, selection, speaker);
					if (!engine.api) { child->setDisabled(true); }
				}
			}
		}
	}
	m_tree->expandAll();
	filter(m_search->text());
}

void SVCBrowser::filter(const QString& text)
{
	const auto visit = [&text](auto&& self, QTreeWidgetItem* item, bool parentMatches) -> bool {
		const auto matches = parentMatches || item->text(0).contains(text, Qt::CaseInsensitive);
		bool visible = matches && !item->childCount();
		for (int index = 0; index < item->childCount(); ++index)
		{
			visible = self(self, item->child(index), matches) || visible;
		}
		item->setHidden(!visible);
		return visible;
	};
	for (int index = 0; index < m_tree->topLevelItemCount(); ++index)
	{
		visit(visit, m_tree->topLevelItem(index), false);
	}
}
} // namespace lmms::gui
