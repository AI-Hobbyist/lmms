#include "NodeWorkflowCanvas.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHash>
#include <QItemSelectionModel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QLabel>
#include <QVBoxLayout>
#include "Engine.h"
#include "NodeSignalActivity.h"
#include "Song.h"
#include "StringPairDrag.h"

#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QFontMetricsF>
#include <QLineF>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace lmms::gui
{

namespace
{
constexpr qreal PortLabelPadding = 5;
constexpr qreal PortLabelGap = 9;
constexpr qreal PairedLabelGap = 8;
constexpr qreal PcbBusLaneSpacing = 20;
constexpr qreal PcbBridgeRadius = 8;
int signalFlowCanvasCount = 0;

struct PcbTrunk
{
	qreal x;
	qreal top;
	qreal bottom;
	QList<qreal> taps;
};

struct PcbBridge
{
	QPainterPath path;
	QPen pen;
};

bool canConnectGraphPorts(const NodeWorkflowCanvas::Port& source, const NodeWorkflowCanvas::Port& target)
{
	return source.graphConnectTo && !target.graphEndpoint.isEmpty() && source.graphSignal == target.graphSignal;
}

bool canConnectPorts(const NodeWorkflowCanvas::Port& source, const NodeWorkflowCanvas::Port& target)
{
	return (source.connectTo && !source.connectionGroup.isEmpty() && source.connectionGroup == target.connectionGroup)
		|| canConnectGraphPorts(source, target);
}

void connectPorts(const NodeWorkflowCanvas::Port& source, const NodeWorkflowCanvas::Port& target)
{
	const bool legacy = source.connectTo && source.connectionGroup == target.connectionGroup;
	const auto connect = legacy ? source.connectTo : source.graphConnectTo;
	connect(legacy ? target.channel : target.graphChannel);
}

QPainterPath wirePath(const QPointF& source, const QPointF& destination)
{
	const qreal span = std::max(qreal{36}, std::abs(destination.x() - source.x()) / 2);
	QPainterPath path(source);
	path.cubicTo(source + QPointF(span, 0), destination - QPointF(span, 0), destination);
	return path;
}

QPainterPath pcbBranchPath(const QPointF& source, const QPointF& destination)
{
	const qreal dx = destination.x() - source.x();
	const qreal dy = destination.y() - source.y();
	const qreal diagonal = std::min(std::abs(dx), std::abs(dy));
	const qreal inset = (std::abs(dx) - diagonal) / 2;
	const qreal direction = dx < 0 ? -1 : 1;
	QPainterPath path(source);
	path.lineTo(source + QPointF(direction * inset, 0));
	path.lineTo(destination - QPointF(direction * inset, 0));
	path.lineTo(destination);
	return path;
}

QPainterPath pcbBridgePath(const QPainterPath& trace, const QList<PcbTrunk>& trunks,
	QList<PcbBridge>& bridges, const QPen& pen)
{
	QPainterPath path;
	QPointF previous;
	for (int index = 0; index < trace.elementCount(); ++index)
	{
		const auto element = trace.elementAt(index);
		const QPointF point(element.x, element.y);
		if (element.isMoveTo())
		{
			path.moveTo(point);
		}
		else if (point.y() == previous.y())
		{
			const qreal y = point.y();
			const qreal direction = point.x() > previous.x() ? 1 : -1;
			const qreal low = std::min(point.x(), previous.x()) + PcbBridgeRadius;
			const qreal high = std::max(point.x(), previous.x()) - PcbBridgeRadius;
			QList<qreal> crossings;
			for (const auto& trunk : trunks)
			{
				if (trunk.x <= low || trunk.x >= high || y <= trunk.top + PcbBridgeRadius * 1.5
					|| y >= trunk.bottom - PcbBridgeRadius)
				{
					continue;
				}
				if (std::none_of(trunk.taps.cbegin(), trunk.taps.cend(), [y](qreal tap) {
					return tap >= y - PcbBridgeRadius * 1.5 && tap <= y + PcbBridgeRadius;
				}))
				{
					crossings.append(direction * trunk.x);
				}
			}
			std::sort(crossings.begin(), crossings.end());
			qreal last = direction * previous.x() - PcbBridgeRadius;
			for (const auto crossing : crossings)
			{
				if (crossing - last < 2 * PcbBridgeRadius)
				{
					continue;
				}
				const qreal x = direction * crossing;
				const QPointF arcStart(x - direction * PcbBridgeRadius, y);
				const QPointF arcEnd(x + direction * PcbBridgeRadius, y);
				const QPointF control(x, y - 2 * PcbBridgeRadius);
				path.lineTo(arcStart);
				path.quadTo(control, arcEnd);
				QPainterPath bridge(arcStart);
				bridge.quadTo(control, arcEnd);
				bridges.append({std::move(bridge), pen});
				last = crossing;
			}
			path.lineTo(point);
		}
		else
		{
			path.lineTo(point);
		}
		previous = point;
	}
	return path;
}

qreal trunkDistance(const QPainterPath& trunk, const QPointF& point)
{
	qreal closest = std::numeric_limits<qreal>::infinity();
	QPointF previous;
	for (int index = 0; index < trunk.elementCount(); ++index)
	{
		const auto element = trunk.elementAt(index);
		const QPointF current(element.x, element.y);
		if (element.isLineTo())
		{
			const QPointF projected(
				std::clamp(point.x(), std::min(previous.x(), current.x()), std::max(previous.x(), current.x())),
				std::clamp(point.y(), std::min(previous.y(), current.y()), std::max(previous.y(), current.y())));
			closest = std::min(closest, QLineF(projected, point).length());
		}
		previous = current;
	}
	return closest;
}

QColor portLabelTextColor(const QColor& background)
{
	const auto linear = [](qreal channel) {
		return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
	};
	const auto luminance = 0.2126 * linear(background.redF()) + 0.7152 * linear(background.greenF())
		+ 0.0722 * linear(background.blueF());
	constexpr qreal Contrast = 4.5;
	const bool darkText = luminance >= 0.179;
	const auto desired = darkText ? (luminance + 0.05) / Contrast - 0.05
		: Contrast * (luminance + 0.05) - 0.05;
	const auto gray = std::clamp(desired, qreal{0}, qreal{1});
	const auto encoded = gray <= 0.0031308 ? 12.92 * gray : 1.055 * std::pow(gray, 1.0 / 2.4) - 0.055;
	const auto value = static_cast<int>(darkText ? std::floor(encoded * 255) : std::ceil(encoded * 255));
	return QColor(value, value, value);
}
}

NodeWorkflowCanvas::NodeWorkflowCanvas(QWidget* parent) : QWidget(parent)
{
	setMinimumSize(320, 220);
	m_signalTimer.setInterval(33);
	connect(&m_signalTimer, &QTimer::timeout, this, [this] { update(); });
}

NodeWorkflowCanvas::~NodeWorkflowCanvas()
{
	if (m_signalTimer.isActive())
	{
		NodeSignalActivity::setMonitoring(--signalFlowCanvasCount > 0);
	}
}

void NodeWorkflowCanvas::setPluginDropHandler(
	std::function<bool(const QString&, const QString&, const QPointF&)> handler)
{
	m_pluginDrop = std::move(handler);
	setAcceptDrops(static_cast<bool>(m_pluginDrop));
}

void NodeWorkflowCanvas::dragEnterEvent(QDragEnterEvent* event)
{
	const auto kind = StringPairDrag::decodeKey(event);
	if (m_pluginDrop && (kind == "instrument" || kind == "svsvoice" || kind == "svcselection"
		|| kind == "vstpluginfile" || kind == "nodeeffect" || kind == "presetfile"))
	{
		event->acceptProposedAction();
	}
}

void NodeWorkflowCanvas::dropEvent(QDropEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const auto position = event->position();
#else
	const auto position = event->posF();
#endif
	if (m_pluginDrop && !navigatorRect().contains(position)
		&& m_pluginDrop(StringPairDrag::decodeKey(event), StringPairDrag::decodeValue(event),
			worldPosition(position)))
	{
		event->acceptProposedAction();
	}
}

void NodeWorkflowCanvas::setSignalFlowVisible(bool visible)
{
	if (m_signalTimer.isActive() == visible)
	{
		return;
	}
	signalFlowCanvasCount += visible ? 1 : -1;
	NodeSignalActivity::setMonitoring(signalFlowCanvasCount > 0);
	if (visible)
	{
		m_signalClock.restart();
		m_signalTimer.start();
	}
	else
	{
		m_signalTimer.stop();
	}
	update();
}

void NodeWorkflowCanvas::setConnectionAggregation(bool enabled, int threshold)
{
	m_aggregateConnections = enabled;
	m_aggregationThreshold = std::max(1, threshold);
	update();
}

void NodeWorkflowCanvas::setPcbBusRouting(bool enabled)
{
	m_pcbBusRouting = enabled;
	update();
}

QList<NodeWorkflowCanvas::ConnectionBundle> NodeWorkflowCanvas::connectionBundles() const
{
	QList<ConnectionBundle> result;
	if (!m_aggregateConnections)
	{
		return result;
	}
	for (const bool output : {false, true})
	{
		const auto firstBundle = result.size();
		QMap<QString, QMap<QPair<qreal, qreal>, QList<qsizetype>>> byEndpoint;
		QHash<qreal, qreal> spans;
		for (qsizetype index = 0; index < m_connections.size(); ++index)
		{
			if (index != m_draggedConnection)
			{
				const auto& wire = m_connections.at(index);
				const auto endpoint = output ? wire.source : wire.destination;
				auto& byPoint = byEndpoint[output ? wire.sourceNode : wire.destinationNode];
				byPoint[{endpoint.x(), endpoint.y()}].append(index);
			}
		}
		for (auto node = byEndpoint.cbegin(); node != byEndpoint.cend(); ++node)
		{
			for (auto point = node->cbegin(); point != node->cend(); ++point)
			{
				if (point->size() > m_aggregationThreshold)
				{
					const auto x = point.key().first;
					result.append({point.value(), QPointF(x, point.key().second), node.key(), output});
					if (m_pcbBusRouting)
					{
						auto& span = spans[x];
						for (const auto index : point.value())
						{
							const auto& wire = m_connections.at(index);
							span = std::max(span, wire.destination.x() - wire.source.x());
						}
					}
				}
			}
		}
		if (m_pcbBusRouting)
		{
			QHash<qreal, int> lanes;
			for (auto index = firstBundle; index < result.size(); ++index)
			{
				auto& bundle = result[index];
				const auto x = bundle.endpoint.x();
				bundle.pcbOffset = std::clamp(spans.value(x) / 3, 36.0, 140.0)
					+ lanes[x]++ * PcbBusLaneSpacing;
			}
		}
	}
	return result;
}

QPointF NodeWorkflowCanvas::bundleJunction(const ConnectionBundle& bundle) const
{
	if (m_pcbBusRouting)
	{
		return bundle.endpoint + QPointF(bundle.output ? bundle.pcbOffset : -bundle.pcbOffset, 0);
	}
	qreal distance = 0;
	for (const auto index : bundle.indices)
	{
		const auto& wire = m_connections.at(index);
		distance = std::max(distance, wire.destination.x() - wire.source.x());
	}
	const qreal offset = std::clamp(distance / 3, 36.0, 140.0);
	return bundle.endpoint + QPointF(bundle.output ? offset : -offset, 0);
}

QPointF NodeWorkflowCanvas::bundleTap(const ConnectionBundle& bundle, const QPointF& junction, qsizetype index) const
{
	if (!m_pcbBusRouting)
	{
		return junction;
	}
	const auto& wire = m_connections.at(index);
	const auto farEnd = bundle.output ? wire.destination : wire.source;
	const qreal delta = junction.y() - farEnd.y();
	const qreal inset = std::min(qreal{14}, std::abs(junction.x() - farEnd.x()) / 2);
	return {junction.x(), farEnd.y() + std::clamp(delta, -inset, inset)};
}

QPainterPath NodeWorkflowCanvas::bundleTrunk(const ConnectionBundle& bundle) const
{
	const auto junction = bundleJunction(bundle);
	QPainterPath trunk(bundle.endpoint);
	trunk.lineTo(junction);
	if (m_pcbBusRouting)
	{
		qreal top = junction.y();
		qreal bottom = junction.y();
		for (const auto index : bundle.indices)
		{
			const auto y = bundleTap(bundle, junction, index).y();
			top = std::min(top, y);
			bottom = std::max(bottom, y);
		}
		trunk.moveTo(junction.x(), top);
		trunk.lineTo(junction.x(), bottom);
	}
	return trunk;
}

void NodeWorkflowCanvas::showConnectionTable(const ConnectionBundle& bundle)
{
	const auto sharedPoint = bundle.endpoint;
	const auto sharedNode = bundle.node;
	const bool output = bundle.output;
	const auto belongs = [sharedPoint, sharedNode, output](const Connection& wire) {
		return output ? wire.source == sharedPoint && wire.sourceNode == sharedNode
			: wire.destination == sharedPoint && wire.destinationNode == sharedNode;
	};
	const auto describe = [this](const QPointF& point, bool output) {
		const auto port = std::find_if(m_ports.cbegin(), m_ports.cend(), [point, output](const Port& item) {
			return item.position == point && item.output == output;
		});
		const auto node = std::find_if(m_nodes.cbegin(), m_nodes.cend(), [point](const Node& item) {
			return item.bounds.contains(point);
		});
		const auto label = port == m_ports.cend() ? QString{} : port->label;
		return node == m_nodes.cend() ? label : node->title + QStringLiteral(" / ") + label;
	};
	QDialog dialog(this);
	dialog.setObjectName("nodeWorkflowConnectionTable");
	dialog.setWindowTitle(tr("Connection Relationships"));
	dialog.resize(690, 380);
	auto* layout = new QVBoxLayout(&dialog);
	auto* table = new QTableWidget(0, 2, &dialog);
	table->setObjectName("nodeWorkflowConnectionRows");
	table->setHorizontalHeaderLabels({tr("Source Port"), tr("Destination Port")});
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(table);
	auto* actions = new QHBoxLayout;
	auto* editSource = new QComboBox(&dialog);
	auto* editDestination = new QComboBox(&dialog);
	editSource->setObjectName("nodeWorkflowConnectionEditSource");
	editDestination->setObjectName("nodeWorkflowConnectionEditDestination");
	editSource->setAccessibleName(tr("Source Port"));
	editDestination->setAccessibleName(tr("Destination Port"));
	auto* remove = new QPushButton(tr("Disconnect"), &dialog);
	remove->setObjectName("nodeWorkflowConnectionDisconnect");
	auto* reconnect = new QPushButton(tr("Reconnect"), &dialog);
	reconnect->setObjectName("nodeWorkflowConnectionReconnect");
	reconnect->setEnabled(false);
	actions->addWidget(editSource, 1);
	actions->addWidget(new QLabel(QStringLiteral("→"), &dialog));
	actions->addWidget(editDestination, 1);
	actions->addWidget(remove);
	actions->addWidget(reconnect);
	layout->addLayout(actions);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	struct Draft
	{
		Connection original;
		QPointF source;
		QPointF destination;
		bool removed = false;
	};
	QList<Draft> drafts;
	for (const auto& wire : std::as_const(m_connections))
	{
		if (belongs(wire))
		{
			drafts.append({wire, wire.source, wire.destination});
		}
	}
	const auto portAt = [this](const QPointF& point, bool output) -> const Port* {
		const auto found = std::find_if(m_ports.cbegin(), m_ports.cend(), [&](const Port& port) {
			return port.output == output && port.position == point;
		});
		return found == m_ports.cend() ? nullptr : &*found;
	};
	const auto available = [&](const Port& source, const Port& target, int editing) {
		if (!canConnectPorts(source, target))
		{
			return false;
		}
		if (std::any_of(m_connections.cbegin(), m_connections.cend(), [&](const Connection& wire) {
			return !belongs(wire) && wire.source == source.position
				&& (wire.destination == target.position || drafts.at(editing).source != source.position);
		}))
		{
			return false;
		}
		for (int row = 0; row < drafts.size(); ++row)
		{
			const auto& other = drafts.at(row);
			if (row != editing && !other.removed && other.source == source.position
				&& (other.destination == target.position || !output))
			{
				return false;
			}
		}
		return true;
	};
	const auto currentDraft = [&] {
		auto* item = table->item(table->currentRow(), 0);
		return item ? item->data(Qt::UserRole).toInt() : -1;
	};
	QList<QPointF> sourceOptions;
	QList<QPointF> destinationOptions;
	const auto updateReconnect = [&] {
		const int index = currentDraft();
		const int sourceIndex = editSource->currentIndex();
		const int targetIndex = editDestination->currentIndex();
		if (index < 0 || sourceIndex < 0 || targetIndex < 0 || (sourceIndex == 0 && targetIndex == 0))
		{
			reconnect->setEnabled(false);
			return;
		}
		const auto* source = portAt(sourceOptions.at(sourceIndex), true);
		const auto* target = portAt(destinationOptions.at(targetIndex), false);
		reconnect->setEnabled(source && target && available(*source, *target, index));
	};
	const auto updateEditors = [&] {
		const QSignalBlocker blockSource(editSource);
		const QSignalBlocker blockDestination(editDestination);
		sourceOptions.clear();
		destinationOptions.clear();
		editSource->clear();
		editDestination->clear();
		const int index = currentDraft();
		if (index >= 0)
		{
			const auto& draft = drafts.at(index);
			const auto* source = portAt(draft.source, true);
			const auto* target = portAt(draft.destination, false);
			editSource->addItem(describe(draft.source, true));
			editDestination->addItem(describe(draft.destination, false));
			sourceOptions.append(draft.source);
			destinationOptions.append(draft.destination);
			if (source && target && draft.original.disconnect)
			{
				for (const auto& port : std::as_const(m_ports))
				{
					if (port.output && port.position != draft.source && available(port, *target, index))
					{
						sourceOptions.append(port.position);
						editSource->addItem(describe(port.position, true));
					}
					else if (!port.output && port.position != draft.destination && available(*source, port, index))
					{
						destinationOptions.append(port.position);
						editDestination->addItem(describe(port.position, false));
					}
				}
			}
		}
		editSource->setEnabled(sourceOptions.size() > 1);
		editDestination->setEnabled(destinationOptions.size() > 1);
		remove->setEnabled(index >= 0 && drafts.at(index).original.disconnect);
		updateReconnect();
	};
	const auto refresh = [&](int selectedDraft) {
		const QSignalBlocker block(table);
		table->setRowCount(0);
		int selectedRow = -1;
		for (int index = 0; index < drafts.size(); ++index)
		{
			const auto& draft = drafts.at(index);
			if (!draft.removed)
			{
				const int row = table->rowCount();
				table->insertRow(row);
				auto* item = new QTableWidgetItem(describe(draft.source, true));
				item->setData(Qt::UserRole, index);
				table->setItem(row, 0, item);
				table->setItem(row, 1, new QTableWidgetItem(describe(draft.destination, false)));
				if (index == selectedDraft)
				{
					selectedRow = row;
				}
			}
		}
		if (table->rowCount() > 0)
		{
			table->setCurrentCell(std::max(0, selectedRow), 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		}
		updateEditors();
	};
	connect(table, &QTableWidget::currentCellChanged, &dialog, updateEditors);
	connect(editSource, &QComboBox::currentIndexChanged, &dialog, updateReconnect);
	connect(editDestination, &QComboBox::currentIndexChanged, &dialog, updateReconnect);
	connect(reconnect, &QPushButton::clicked, &dialog, [&] {
		const int index = currentDraft();
		if (index >= 0 && reconnect->isEnabled())
		{
			auto& draft = drafts[index];
			draft.source = sourceOptions.at(editSource->currentIndex());
			draft.destination = destinationOptions.at(editDestination->currentIndex());
			refresh(index);
		}
	});
	connect(remove, &QPushButton::clicked, &dialog, [&] {
		if (const int index = currentDraft(); index >= 0)
		{
			drafts[index].removed = true;
			refresh(-1);
		}
	});
	refresh(-1);
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	const auto findWire = [this](const QPointF& source, const QPointF& destination) {
		return std::find_if(m_connections.cbegin(), m_connections.cend(), [&](const Connection& wire) {
			return wire.source == source && wire.destination == destination;
		});
	};
	for (const auto& draft : std::as_const(drafts))
	{
		const auto& old = draft.original;
		if (!draft.removed && draft.source == old.source && draft.destination == old.destination)
		{
			continue;
		}
		if (!draft.removed && findWire(draft.source, draft.destination) == m_connections.cend())
		{
			const auto* source = portAt(draft.source, true);
			const auto* destination = portAt(draft.destination, false);
			if (!source || !destination || !canConnectPorts(*source, *destination))
			{
				continue;
			}
			connectPorts(*source, *destination);
			QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
			if (findWire(draft.source, draft.destination) == m_connections.cend())
			{
				continue;
			}
		}
		const auto previous = findWire(old.source, old.destination);
		if (old.disconnect && previous != m_connections.cend() && previous->disconnect)
		{
			const auto disconnect = previous->disconnect;
			disconnect();
			QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
		}
	}
}

void NodeWorkflowCanvas::setLayoutHandlers(std::function<std::optional<QPointF>(const QString&)> load,
	std::function<void(const QString&, const QPointF&)> save)
{
	m_loadNodePosition = std::move(load);
	m_saveNodePosition = std::move(save);
}

void NodeWorkflowCanvas::setGraph(QList<Node> nodes, QList<Connection> connections, QList<Port> ports)
{
	ensurePolished();
	const QFontMetricsF metrics(font());
	QList<QRectF> originalBounds;
	QList<qsizetype> order(nodes.size());
	std::iota(order.begin(), order.end(), qsizetype{0});
	for (auto& port : ports)
	{
		port.labelWidth = std::ceil(metrics.horizontalAdvance(port.label)) + 2 * PortLabelPadding;
	}
	for (auto& node : nodes)
	{
		originalBounds.append(node.bounds);
		qreal requiredWidth = metrics.horizontalAdvance(node.title) + 24;
		QMap<qreal, std::array<qreal, 2>> labelWidths;
		for (const auto& port : ports)
		{
			if (node.bounds.contains(port.position))
			{
				auto& width = labelWidths[port.position.y()][port.output ? 1 : 0];
				width = std::max(width, port.labelWidth);
			}
		}
		for (const auto& row : std::as_const(labelWidths))
		{
			requiredWidth = std::max(requiredWidth, row[0] + row[1] + 2 * PortLabelGap + PairedLabelGap);
		}
		const auto nodeWidth = std::max(node.bounds.width(), std::ceil(requiredWidth));
		for (auto& port : ports)
		{
			if (node.bounds.contains(port.position))
			{
				const auto& row = labelWidths.value(port.position.y());
				if (row[0] > 0 && row[1] > 0)
				{
					const auto remaining = nodeWidth - row[0] - row[1] - 2 * PortLabelGap - PairedLabelGap;
					port.labelWidth = row[port.output ? 1 : 0] + remaining / 2;
				}
			}
		}
		node.bounds.setWidth(nodeWidth);
	}
	std::stable_sort(order.begin(), order.end(), [&originalBounds](qsizetype left, qsizetype right) {
		return originalBounds.at(left).left() < originalBounds.at(right).left();
	});
	for (const auto index : std::as_const(order))
	{
		const auto& original = originalBounds.at(index);
		qreal shift = 0;
		for (const auto previous : std::as_const(order))
		{
			if (previous == index)
			{
				break;
			}
			const auto& preceding = originalBounds.at(previous);
			if (preceding.right() <= original.left()
				&& preceding.top() < original.bottom() && preceding.bottom() > original.top())
			{
				shift = std::max(shift, nodes.at(previous).bounds.right() - preceding.right());
			}
		}
		nodes[index].bounds.translate(shift, 0);
	}
	if (m_loadNodePosition)
	{
		for (auto& node : nodes)
		{
			if (!node.layoutKey.isEmpty())
			{
				if (const auto position = m_loadNodePosition(node.layoutKey))
				{
					node.bounds.moveTopLeft(*position);
				}
			}
		}
	}
	const auto mapPoint = [&originalBounds, &nodes](const QPointF& point, qsizetype member,
		QList<qsizetype> Node::* indices) {
		for (qsizetype index = 0; index < originalBounds.size(); ++index)
		{
			const auto& original = originalBounds.at(index);
			if (original.width() > 0 && original.contains(point))
			{
				(nodes[index].*indices).append(member);
				const auto& resized = nodes.at(index).bounds;
				const auto ratio = (point.x() - original.left()) / original.width();
				return QPointF(resized.left() + ratio * resized.width(),
					resized.top() + point.y() - original.top());
			}
		}
		return point;
	};
	for (qsizetype index = 0; index < ports.size(); ++index)
	{
		auto& port = ports[index];
		port.position = mapPoint(port.position, index, &Node::portIndices);
	}
	for (qsizetype index = 0; index < connections.size(); ++index)
	{
		auto& connection = connections[index];
		connection.source = mapPoint(connection.source, index, &Node::sourceIndices);
		connection.destination = mapPoint(connection.destination, index, &Node::destinationIndices);
	}
	for (qsizetype index = 0; index < nodes.size(); ++index)
	{
		const auto& node = nodes.at(index);
		const auto id = node.layoutKey.isEmpty() ? QString::number(index) : node.layoutKey;
		for (const auto wire : node.sourceIndices)
		{
			connections[wire].sourceNode = id;
		}
		for (const auto wire : node.destinationIndices)
		{
			connections[wire].destinationNode = id;
		}
	}
	m_nodes = std::move(nodes);
	m_selectedNode = -1;
	m_selectedConnection = -1;
	emit connectionSelectionCleared();
	m_connections = std::move(connections);
	m_ports = std::move(ports);
	m_sourcePort = -1;
	m_draggedConnection = -1;
	m_drag = Drag::None;
	update();
	emit graphChanged();
}

QRectF NodeWorkflowCanvas::graphBounds() const
{
	QRectF bounds;
	for (const auto& node : m_nodes)
	{
		bounds = bounds.united(node.bounds);
	}
	return bounds;
}

void NodeWorkflowCanvas::fitGraph()
{
	const auto bounds = graphBounds();
	if (bounds.isEmpty())
	{
		m_center = {};
		setZoom(1);
	}
	else
	{
		m_center = bounds.center();
		setZoom(std::min({(width() - 80) / bounds.width(), (height() - 80) / bounds.height(), qreal{1.5}}));
	}
}

void NodeWorkflowCanvas::setZoom(qreal scale)
{
	m_scale = std::clamp(scale, qreal{0.1}, qreal{4});
	emit zoomChanged(m_scale);
	update();
}

QRectF NodeWorkflowCanvas::navigatorRect() const
{
	return QRectF(12, height() - 108, 174, 96);
}

QPointF NodeWorkflowCanvas::worldPosition(const QPointF& position) const
{
	return m_center + (position - QPointF(width(), height()) / 2) / m_scale;
}

QTransform NodeWorkflowCanvas::navigatorTransform() const
{
	const auto navigator = navigatorRect().adjusted(6, 6, -6, -6);
	const QSizeF visible(width() / m_scale, height() / m_scale);
	const QRectF viewport(m_center - QPointF(visible.width(), visible.height()) / 2, visible);
	const auto extent = graphBounds().united(viewport).adjusted(-20, -20, 20, 20);
	const qreal scale = std::min(navigator.width() / extent.width(), navigator.height() / extent.height());
	QTransform transform;
	transform.translate(navigator.center().x(), navigator.center().y());
	transform.scale(scale, scale);
	transform.translate(-extent.center().x(), -extent.center().y());
	return transform;
}

void NodeWorkflowCanvas::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.fillRect(rect(), m_canvasColor);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.save();
	painter.translate(width() / 2.0, height() / 2.0);
	painter.scale(m_scale, m_scale);
	painter.translate(-m_center);

	const auto visible = painter.transform().inverted().mapRect(QRectF(rect()));
	const qreal step = m_scale < 0.4 ? 128 : 32;
	painter.setPen(QPen(m_gridColor, 0));
	for (qreal x = std::floor(visible.left() / step) * step; x < visible.right(); x += step)
	{
		painter.drawLine(QPointF(x, visible.top()), QPointF(x, visible.bottom()));
	}
	for (qreal y = std::floor(visible.top() / step) * step; y < visible.bottom(); y += step)
	{
		painter.drawLine(QPointF(visible.left(), y), QPointF(visible.right(), y));
	}

	const bool animateSignals = m_signalTimer.isActive();
	const qreal beatPhase = animateSignals
		? std::fmod(m_signalClock.elapsed() * Engine::getSong()->getTempo() / 60000.0, 1.0)
		: 0;
	const auto bundles = connectionBundles();
	QHash<qsizetype, QPointF> starts;
	QHash<qsizetype, QPointF> ends;
	QList<PcbTrunk> pcbTrunks;
	QList<PcbBridge> pcbBridges;
	for (const auto& bundle : bundles)
	{
		const auto junction = bundleJunction(bundle);
		auto& junctions = bundle.output ? starts : ends;
		PcbTrunk trunk{junction.x(), junction.y(), junction.y(), {junction.y()}};
		for (const auto index : bundle.indices)
		{
			const auto tap = bundleTap(bundle, junction, index);
			junctions.insert(index, tap);
			if (m_pcbBusRouting)
			{
				trunk.top = std::min(trunk.top, tap.y());
				trunk.bottom = std::max(trunk.bottom, tap.y());
				trunk.taps.append(tap.y());
			}
		}
		if (m_pcbBusRouting)
		{
			pcbTrunks.append(std::move(trunk));
		}
	}
	for (qsizetype index = 0; index < m_connections.size(); ++index)
	{
		const auto& connection = m_connections.at(index);
		painter.setPen(QPen(connection.color, index == m_selectedConnection ? 3 : 2));
		painter.setBrush(Qt::NoBrush);
		if (m_draggedConnection == index && m_drag == Drag::Wire
			&& QLineF(m_wireDragStart, m_wireEnd).length() * m_scale >= QApplication::startDragDistance())
		{
			painter.drawPath(wirePath(connection.source, m_wireEnd));
			continue;
		}
		const auto start = starts.value(index, connection.source);
		const auto end = ends.value(index, connection.destination);
		const auto path = m_pcbBusRouting && (starts.contains(index) || ends.contains(index))
			? pcbBridgePath(pcbBranchPath(start, end), pcbTrunks, pcbBridges, painter.pen())
			: wirePath(start, end);
		painter.drawPath(path);
		if (animateSignals && connection.active && connection.active())
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(connection.color);
			painter.drawEllipse(path.pointAtPercent(beatPhase), 3.5, 3.5);
		}
	}
	for (qsizetype index = 0; index < bundles.size(); ++index)
	{
		const auto& bundle = bundles.at(index);
		const auto junction = bundleJunction(bundle);
		const auto& first = m_connections.at(bundle.indices.front());
		const auto port = std::find_if(m_ports.cbegin(), m_ports.cend(), [&bundle](const Port& endpoint) {
			return endpoint.output == bundle.output && endpoint.position == bundle.endpoint;
		});
		const auto color = port == m_ports.cend() ? first.color : port->color;
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(color, m_pcbBusRouting ? 7 : 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		const auto start = bundle.output ? bundle.endpoint : junction;
		const auto end = bundle.output ? junction : bundle.endpoint;
		if (m_pcbBusRouting)
		{
			const auto& trunk = pcbTrunks.at(index);
			QPainterPath header(bundle.endpoint);
			header.lineTo(junction);
			painter.drawPath(pcbBridgePath(header, pcbTrunks, pcbBridges, painter.pen()));
			if (trunk.top < trunk.bottom)
			{
				painter.drawLine(QPointF(trunk.x, trunk.top), QPointF(trunk.x, trunk.bottom));
			}
		}
		else
		{
			painter.drawPath(bundleTrunk(bundle));
		}
		painter.setPen(color);
		painter.drawText((start + end) / 2 + QPointF(-10, -9),
			QStringLiteral("×%1").arg(bundle.indices.size()));
		if (animateSignals && std::any_of(bundle.indices.cbegin(), bundle.indices.cend(), [this](qsizetype index) {
			const auto& cable = m_connections.at(index);
			return cable.active && cable.active();
		}))
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(color);
			painter.drawEllipse(start + (end - start) * beatPhase, 3.5, 3.5);
		}
	}
	for (const auto& bridge : pcbBridges)
	{
		auto shadow = bridge.pen;
		auto shadowColor = m_canvasColor.darker(170);
		shadowColor.setAlpha(160);
		shadow.setColor(shadowColor);
		shadow.setWidthF(bridge.pen.widthF() + 2);
		shadow.setCapStyle(Qt::FlatCap);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(shadow);
		painter.drawPath(QTransform::fromTranslate(0, 2).map(bridge.path));

		auto outline = bridge.pen;
		outline.setColor(m_canvasColor);
		outline.setWidthF(bridge.pen.widthF() + 3);
		outline.setCapStyle(Qt::FlatCap);
		painter.setPen(outline);
		painter.drawPath(bridge.path);
		painter.setPen(bridge.pen);
		painter.drawPath(bridge.path);
	}
	for (qsizetype index = 0; index < m_nodes.size(); ++index)
	{
		const auto& node = m_nodes.at(index);
		painter.setPen(node.trackColor.isValid() ? node.trackColor : m_nodeBorderColor);
		painter.setBrush(m_nodeColor);
		painter.drawRoundedRect(node.bounds, 4, 4);
		painter.fillRect(node.bounds.adjusted(1, 1, -1, 30 - node.bounds.height()), m_gridColor);
		if (node.trackColor.isValid())
		{
			painter.fillRect(QRectF(node.bounds.topLeft() + QPointF(1, 4), QSizeF(3, 22)), node.trackColor);
		}
		painter.setPen(palette().color(QPalette::WindowText));
		painter.drawText(node.bounds.adjusted(12, 0, -8, 30 - node.bounds.height()),
			Qt::AlignVCenter, node.title);
		if (index == m_selectedNode)
		{
			painter.setPen(QPen(m_navigatorViewportColor, 2));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(node.bounds, 4, 4);
		}
	}
	for (const auto& connection : m_connections)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(connection.color);
		painter.drawEllipse(connection.source, 4, 4);
		painter.drawEllipse(connection.destination, 4, 4);
	}
	for (const auto& port : m_ports)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(port.color);
		painter.drawEllipse(port.position, 5, 5);
		const auto background = port.labelBackground.isValid() ? port.labelBackground : m_portLabelBackground;
		const auto labelWidth = port.labelWidth;
		const QRectF label(port.position.x() + (port.output ? -labelWidth - PortLabelGap : PortLabelGap),
			port.position.y() - 9, labelWidth, 18);
		painter.setBrush(background);
		painter.drawRoundedRect(label, 3, 3);
		painter.setPen(portLabelTextColor(background));
		painter.drawText(label.adjusted(PortLabelPadding, 0, -PortLabelPadding, 0),
			Qt::AlignVCenter | (port.output ? Qt::AlignRight : Qt::AlignLeft), port.label);
	}
	if (m_drag == Drag::Wire && m_sourcePort >= 0 && m_draggedConnection < 0)
	{
		const auto& source = m_ports.at(m_sourcePort);
		painter.setPen(QPen(source.color, 2, Qt::DashLine));
		painter.drawLine(source.position, m_wireEnd);
	}
	painter.restore();

	const auto navigator = navigatorRect();
	const auto transform = m_drag == Drag::Navigator ? m_dragNavigator : navigatorTransform();
	painter.fillRect(navigator, m_nodeColor);
	painter.setPen(m_nodeBorderColor);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(navigator);
	painter.setClipRect(navigator.adjusted(1, 1, -1, -1));
	for (const auto& node : m_nodes)
	{
		painter.fillRect(transform.mapRect(node.bounds),
			node.trackColor.isValid() ? node.trackColor : m_navigatorNodeColor);
	}
	painter.setPen(m_navigatorViewportColor);
	painter.drawRect(transform.mapRect(visible));
}

void NodeWorkflowCanvas::wheelEvent(QWheelEvent* event)
{
	const qreal scale = std::clamp(m_scale * std::pow(1.15, event->angleDelta().y() / 120.0),
		qreal{0.1}, qreal{4});
	const QPointF offset = event->position() - QPointF(width(), height()) / 2;
	m_center += offset / m_scale - offset / scale;
	setZoom(scale);
	event->accept();
}

void NodeWorkflowCanvas::mousePressEvent(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const auto position = event->position();
#else
	const auto position = event->localPos();
#endif
	if (event->button() == Qt::RightButton && !navigatorRect().contains(position))
	{
		const auto world = m_center + (position - QPointF(width(), height()) / 2) / m_scale;
		const auto bundles = connectionBundles();
		const ConnectionBundle* nearest = nullptr;
		qreal distance = 8 / m_scale;
		for (const auto& bundle : bundles)
		{
			const auto candidate = trunkDistance(bundleTrunk(bundle), world);
			if (candidate < distance)
			{
				nearest = &bundle;
				distance = candidate;
			}
		}
		if (nearest)
		{
			QMenu menu(this);
			auto* relationships = menu.addAction(tr("Connection Relationships"));
			if (menu.exec(mapToGlobal(position.toPoint())) == relationships)
			{
				for (const auto& current : connectionBundles())
				{
					if (current.endpoint == nearest->endpoint && current.node == nearest->node
						&& current.output == nearest->output)
					{
						showConnectionTable(current);
						break;
					}
				}
			}
			return;
		}
		const bool canPaste = m_pasteNode && m_clipboardScope != ClipboardScope::None
			&& m_clipboardScope == m_pasteScope;
		for (const auto& port : m_ports)
		{
			if (port.edit && QLineF(port.position, world).length() * m_scale <= 9)
			{
				const auto edit = port.edit;
				edit(mapToGlobal(position.toPoint()));
				return;
			}
		}
		for (const auto& node : m_nodes)
		{
			if (!node.bounds.contains(world))
			{
				continue;
			}
			const auto actions = node;
			QMenu menu(this);
			auto* copy = menu.addAction(tr("Copy Node"));
			copy->setEnabled(bool(actions.copy));
			auto* paste = menu.addAction(tr("Paste Node"));
			paste->setEnabled(canPaste);
			if (actions.openPianoRoll)
			{
				menu.addAction(tr("Open Piano Roll"), this, actions.openPianoRoll);
			}
			if (actions.rename)
			{
				menu.addAction(tr("Rename Node"), this, actions.rename);
			}
			auto* remove = menu.addAction(tr("Delete Node"));
			remove->setEnabled(bool(actions.remove));
			const auto* selected = menu.exec(mapToGlobal(position.toPoint()));
			if (selected == copy && actions.copy)
			{
				m_pasteNode = actions.copy();
				m_pasteScope = m_clipboardScope;
			}
			else if (selected == paste && canPaste)
			{
				m_pasteNode(world);
			}
			else if (selected == remove && actions.remove)
			{
				actions.remove();
			}
			return;
		}
		if (canPaste)
		{
			QMenu menu(this);
			auto* paste = menu.addAction(tr("Paste Node"));
			if (menu.exec(mapToGlobal(position.toPoint())) == paste)
			{
				m_pasteNode(world);
			}
			return;
		}
	}
	if (event->button() == Qt::LeftButton && navigatorRect().contains(position))
	{
		m_dragNavigator = navigatorTransform();
		m_drag = Drag::Navigator;
		m_center = m_dragNavigator.inverted().map(position);
	}
	else if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
	{
		m_drag = Drag::Canvas;
		m_lastMousePosition = position;
		if (event->button() == Qt::LeftButton)
		{
			m_selectedConnection = -1;
			emit connectionSelectionCleared();
			const auto world = worldPosition(position);
			m_wireDragStart = world;
			for (qsizetype index = 0; index < m_ports.size(); ++index)
			{
				const auto& port = m_ports.at(index);
				if (port.output && (port.connectTo || port.graphConnectTo)
					&& QLineF(port.position, world).length() * m_scale <= 9)
				{
					m_sourcePort = index;
					m_wireEnd = world;
					m_drag = Drag::Wire;
					break;
				}
			}
			const bool overNode = std::any_of(m_nodes.begin(), m_nodes.end(),
				[&world](const Node& node) { return node.bounds.contains(world); });
			QPainterPathStroker hitArea;
			hitArea.setWidth(12 / m_scale);
			const auto bundles = connectionBundles();
			for (qsizetype index = m_connections.size(); index > 0; --index)
			{
				const auto& connection = m_connections.at(index - 1);
				if (!connection.disconnect)
				{
					continue;
				}
				if (m_sourcePort >= 0)
				{
					if (connection.source != m_ports.at(m_sourcePort).position)
					{
						continue;
					}
					if (m_draggedConnection >= 0)
					{
						m_draggedConnection = -1;
						break;
					}
				}
				else
				{
					const bool nearInput = QLineF(connection.destination, world).length() * m_scale <= 9;
					if (nearInput)
					{
						const auto count = std::count_if(m_connections.begin(), m_connections.end(),
							[&connection](const Connection& other) {
								return other.destination == connection.destination;
							});
						if (count != 1)
						{
							continue;
						}
					}
					else
					{
						auto start = connection.source;
						auto end = connection.destination;
						bool bundled = false;
						for (const auto& bundle : bundles)
						{
							if (bundle.indices.contains(index - 1))
							{
								(bundle.output ? start : end) = bundleTap(bundle, bundleJunction(bundle), index - 1);
								bundled = true;
							}
						}
						const auto path = m_pcbBusRouting && bundled ? pcbBranchPath(start, end) : wirePath(start, end);
						if (overNode || !hitArea.createStroke(path).contains(world))
						{
							continue;
						}
					}
				}
				m_draggedConnection = index - 1;
				if (m_sourcePort < 0 && connection.select)
				{
					m_selectedConnection = index - 1;
					m_selectedNode = -1;
					connection.select();
				}
				m_wireEnd = world;
				m_drag = Drag::Wire;
				if (m_sourcePort < 0)
				{
					break;
				}
			}
			if (m_drag == Drag::Canvas)
			{
				m_selectedNode = -1;
				for (auto index = m_nodes.size(); index > 0; --index)
				{
					const auto& node = m_nodes.at(index - 1);
					if (!node.bounds.contains(world))
					{
						continue;
					}
					m_selectedNode = index - 1;
					m_drag = Drag::Node;
					m_nodeDragStart = node.bounds.topLeft();
					if (const auto inspect = node.inspect)
					{
						inspect();
					}
					break;
				}
			}
		}
	}
	update();
}

void NodeWorkflowCanvas::mouseMoveEvent(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const auto position = event->position();
#else
	const auto position = event->localPos();
#endif
	if (m_drag == Drag::Navigator)
	{
		m_center = m_dragNavigator.inverted().map(position);
	}
	else if (m_drag == Drag::Canvas)
	{
		m_center -= (position - m_lastMousePosition) / m_scale;
		m_lastMousePosition = position;
	}
	else if (m_drag == Drag::Node && m_selectedNode >= 0)
	{
		const auto delta = (position - m_lastMousePosition) / m_scale;
		auto& node = m_nodes[m_selectedNode];
		node.bounds.translate(delta);
		for (const auto port : node.portIndices)
		{
			m_ports[port].position += delta;
		}
		for (const auto wire : node.sourceIndices)
		{
			m_connections[wire].source += delta;
		}
		for (const auto wire : node.destinationIndices)
		{
			m_connections[wire].destination += delta;
		}
		m_lastMousePosition = position;
	}
	else if (m_drag == Drag::Wire)
	{
		m_wireEnd = worldPosition(position);
	}
	update();
}

void NodeWorkflowCanvas::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_drag == Drag::Node && m_selectedNode >= 0 && m_saveNodePosition)
	{
		const auto& node = m_nodes.at(m_selectedNode);
		if (!node.layoutKey.isEmpty() && node.bounds.topLeft() != m_nodeDragStart)
		{
			m_saveNodePosition(node.layoutKey, node.bounds.topLeft());
		}
	}
	const auto sourceIndex = std::exchange(m_sourcePort, -1);
	const auto wireIndex = std::exchange(m_draggedConnection, -1);
	const bool draggingWire = m_drag == Drag::Wire;
	m_drag = Drag::None;
	update();
	if (!draggingWire)
	{
		return;
	}
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const auto position = event->position();
#else
	const auto position = event->localPos();
#endif
	if (navigatorRect().contains(position))
	{
		return;
	}
	const auto world = worldPosition(position);
	bool overPort = false;
	for (const auto& target : m_ports)
	{
		if (QLineF(target.position, world).length() * m_scale > 9)
		{
			continue;
		}
		overPort = true;
		if (sourceIndex < 0 || target.output)
		{
			continue;
		}
		const auto& source = m_ports.at(sourceIndex);
		if (canConnectPorts(source, target))
		{
			connectPorts(source, target);
			return;
		}
	}
	if (wireIndex >= 0 && !overPort
		&& QLineF(m_wireDragStart, world).length() * m_scale >= QApplication::startDragDistance()
		&& std::none_of(m_nodes.begin(), m_nodes.end(),
			[&world](const Node& node) { return node.bounds.contains(world); }))
	{
		const auto disconnect = m_connections.at(wireIndex).disconnect;
		disconnect();
	}
}

void NodeWorkflowCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const auto position = event->position();
#else
	const auto position = event->localPos();
#endif
	if (event->button() != Qt::LeftButton || navigatorRect().contains(position))
	{
		return;
	}
	const auto world = m_center + (position - QPointF(width(), height()) / 2) / m_scale;
	for (auto it = m_nodes.crbegin(); it != m_nodes.crend(); ++it)
	{
		if (it->bounds.contains(world) && it->open)
		{
			const auto open = it->open;
			m_drag = Drag::None;
			open();
			return;
		}
	}
}

} // namespace lmms::gui
