#ifndef LMMS_AI_CACHE_SETTINGS_PAGE_H
#define LMMS_AI_CACHE_SETTINGS_PAGE_H

#include <QColor>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;

namespace lmms::gui {
class AICacheUsageBar : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QColor svcColor READ svcColor WRITE setSvcColor)
	Q_PROPERTY(QColor svsColor READ svsColor WRITE setSvsColor)
	Q_PROPERTY(QColor freeColor READ freeColor WRITE setFreeColor)

public:
	explicit AICacheUsageBar(QWidget* parent = nullptr);
	void setUsage(qint64 svcBytes, qint64 svsBytes, qint64 capacity);
	QColor svcColor() const { return m_svcColor; }
	QColor svsColor() const { return m_svsColor; }
	QColor freeColor() const { return m_freeColor; }
	void setSvcColor(const QColor& color);
	void setSvsColor(const QColor& color);
	void setFreeColor(const QColor& color);

signals:
	void colorsChanged();

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QColor m_svcColor{QString("#409b87")};
	QColor m_svsColor{QString("#6e91c3")};
	QColor m_freeColor{QString("#39434c")};
	qint64 m_svcBytes = 0;
	qint64 m_svsBytes = 0;
	qint64 m_capacity = 1;
};

class AICacheSettingsPage : public QWidget
{
public:
	explicit AICacheSettingsPage(QWidget* parent = nullptr);
	void save();

private:
	void refreshUsage();
	void clearCaches(bool svc, bool svs);
	QDoubleSpinBox* m_limit;
	QLabel* m_totalUsage;
	QLabel* m_totalCapacity;
	QLabel* m_svcLegend;
	QLabel* m_svsLegend;
	AICacheUsageBar* m_usageBar;
	QLabel* m_svcUsage;
	QLabel* m_svsUsage;
	QLabel* m_status;
};
} // namespace lmms::gui

#endif
