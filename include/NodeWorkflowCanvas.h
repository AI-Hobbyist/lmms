#ifndef LMMS_NODE_WORKFLOW_CANVAS_H
#define LMMS_NODE_WORKFLOW_CANVAS_H

#include <QColor>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QTransform>
#include <QWidget>

#include <functional>
#include <optional>

namespace lmms::gui
{

class NodeWorkflowCanvas : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QColor canvasColor MEMBER m_canvasColor)
	Q_PROPERTY(QColor gridColor MEMBER m_gridColor)
	Q_PROPERTY(QColor nodeColor MEMBER m_nodeColor)
	Q_PROPERTY(QColor nodeBorderColor MEMBER m_nodeBorderColor)
	Q_PROPERTY(QColor navigatorNodeColor MEMBER m_navigatorNodeColor)
	Q_PROPERTY(QColor navigatorViewportColor MEMBER m_navigatorViewportColor)
	Q_PROPERTY(QColor audioPortColor MEMBER m_audioPortColor)
	Q_PROPERTY(QColor midiPortColor MEMBER m_midiPortColor)
	Q_PROPERTY(QColor voicePortColor MEMBER m_voicePortColor)
	Q_PROPERTY(QColor stepPortColor MEMBER m_stepPortColor)
	Q_PROPERTY(QColor portLabelBackground MEMBER m_portLabelBackground)

public:
	enum class ClipboardScope { None, Root, Song, Pattern, Source, Custom };
	struct Node
	{
		QRectF bounds;
		QString title;
		std::function<void()> open;
		std::function<void(const QPoint&)> edit = {};
		QColor trackColor;
		std::function<void()> openPianoRoll;
		std::function<std::function<void(const QPointF&)>()> copy;
		std::function<void()> remove;
		std::function<void()> inspect;
		QString layoutKey;
		QList<qsizetype> portIndices;
		QList<qsizetype> sourceIndices;
		QList<qsizetype> destinationIndices;
		std::function<void()> rename;
	};

	struct Connection
	{
		QPointF source;
		QPointF destination;
		QColor color;
		std::function<bool()> active;
		std::function<void()> disconnect;
		QString destinationNode;
		QString sourceNode;
		std::function<void()> select;
	};

	struct Port
	{
		QPointF position;
		QColor color;
		QString label;
		bool output;
		std::function<void(const QPoint&)> edit;
		QString connectionGroup;
		unsigned channel = 0;
		std::function<void(unsigned)> connectTo;
		QColor labelBackground;
		qreal labelWidth = 0;
		QString graphEndpoint;
		unsigned graphChannel = 0;
		QString graphSignal;
		std::function<void(unsigned)> graphConnectTo;
	};

	explicit NodeWorkflowCanvas(QWidget* parent = nullptr);
	~NodeWorkflowCanvas() override;
	void setGraph(QList<Node> nodes, QList<Connection> connections, QList<Port> ports = {});
	void setLayoutHandlers(std::function<std::optional<QPointF>(const QString&)> load,
		std::function<void(const QString&, const QPointF&)> save);
	void fitGraph();
	void setZoom(qreal scale);
	void setSignalFlowVisible(bool visible);
	void setConnectionAggregation(bool enabled, int threshold);
	void setPcbBusRouting(bool enabled);
	void setPluginDropHandler(std::function<bool(const QString&, const QString&, const QPointF&)> handler);
	void setClipboardScope(ClipboardScope scope) { m_clipboardScope = scope; }
	QColor audioPortColor() const { return m_audioPortColor; }
	QColor midiPortColor() const { return m_midiPortColor; }
	QColor voicePortColor() const { return m_voicePortColor; }
	QColor stepPortColor() const { return m_stepPortColor; }

signals:
	void zoomChanged(qreal scale);
	void graphChanged();
	void connectionSelectionCleared();

protected:
	void paintEvent(QPaintEvent*) override;
	void wheelEvent(QWheelEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	QRectF graphBounds() const;
	QRectF navigatorRect() const;
	QTransform navigatorTransform() const;
	QPointF worldPosition(const QPointF& position) const;
	struct ConnectionBundle
	{
		QList<qsizetype> indices;
		QPointF endpoint;
		QString node;
		bool output;
		qreal pcbOffset = 0;
	};
	QList<ConnectionBundle> connectionBundles() const;
	QPointF bundleJunction(const ConnectionBundle& bundle) const;
	QPointF bundleTap(const ConnectionBundle& bundle, const QPointF& junction, qsizetype index) const;
	QPainterPath bundleTrunk(const ConnectionBundle& bundle) const;
	void showConnectionTable(const ConnectionBundle& bundle);

	QList<Node> m_nodes;
	qsizetype m_selectedNode = -1;
	qsizetype m_selectedConnection = -1;
	std::function<std::optional<QPointF>(const QString&)> m_loadNodePosition;
	std::function<void(const QString&, const QPointF&)> m_saveNodePosition;
	QPointF m_nodeDragStart;
	QList<Connection> m_connections;
	QList<Port> m_ports;
	std::function<bool(const QString&, const QString&, const QPointF&)> m_pluginDrop;
	std::function<void(const QPointF&)> m_pasteNode;
	ClipboardScope m_clipboardScope = ClipboardScope::None;
	ClipboardScope m_pasteScope = ClipboardScope::None;
	QPointF m_center;
	QPointF m_lastMousePosition;
	QPointF m_wireEnd;
	QPointF m_wireDragStart;
	qsizetype m_sourcePort = -1;
	qsizetype m_draggedConnection = -1;
	QTransform m_dragNavigator;
	qreal m_scale = 1;
	QTimer m_signalTimer;
	bool m_aggregateConnections = false;
	int m_aggregationThreshold = 4;
	bool m_pcbBusRouting = false;
	QElapsedTimer m_signalClock;
	enum class Drag { None, Canvas, Navigator, Wire, Node };
	Drag m_drag = Drag::None;
	QColor m_canvasColor{"#171d24"};
	QColor m_gridColor{"#29323b"};
	QColor m_nodeColor{"#20262d"};
	QColor m_nodeBorderColor{"#40505d"};
	QColor m_navigatorNodeColor{"#415663"};
	QColor m_navigatorViewportColor{"#77c795"};
	QColor m_audioPortColor{"#77c795"};
	QColor m_midiPortColor{"#82bfff"};
	QColor m_voicePortColor{"#cbabfa"};
	QColor m_stepPortColor{"#5ed4cf"};
	QColor m_portLabelBackground{"#29323b"};
};

} // namespace lmms::gui

#endif
