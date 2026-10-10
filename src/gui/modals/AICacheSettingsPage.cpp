#include "AICacheSettingsPage.h"

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

#include "ConfigManager.h"
#include "AICacheBudget.h"
#include "SVCCache.h"
#include "SVSCache.h"

namespace lmms::gui {
namespace {
constexpr qint64 MiB = 1024 * 1024;
constexpr qint64 GiB = 1024 * MiB;
} // namespace

AICacheUsageBar::AICacheUsageBar(QWidget* parent)
	: QWidget(parent)
{
	setObjectName("aiCacheUsageBar");
	setFixedHeight(12);
}

void AICacheUsageBar::setUsage(qint64 svcBytes, qint64 svsBytes, qint64 capacity)
{
	m_svcBytes = std::max(qint64(0), svcBytes);
	m_svsBytes = std::max(qint64(0), svsBytes);
	m_capacity = std::max(qint64(1), capacity);
	update();
}

void AICacheUsageBar::setSvcColor(const QColor& color)
{
	if (color.isValid() && color != m_svcColor)
	{
		m_svcColor = color;
		update();
		emit colorsChanged();
	}
}

void AICacheUsageBar::setSvsColor(const QColor& color)
{
	if (color.isValid() && color != m_svsColor)
	{
		m_svsColor = color;
		update();
		emit colorsChanged();
	}
}

void AICacheUsageBar::setFreeColor(const QColor& color)
{
	if (color.isValid() && color != m_freeColor)
	{
		m_freeColor = color;
		update();
		emit colorsChanged();
	}
}

void AICacheUsageBar::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QPainterPath shape;
	shape.addRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
	painter.setClipPath(shape);
	painter.fillRect(rect(), m_freeColor);
	const auto denominator = double(std::max(m_capacity, m_svcBytes + m_svsBytes));
	const auto svcWidth = width() * double(m_svcBytes) / denominator;
	const auto svsWidth = width() * double(m_svsBytes) / denominator;
	painter.fillRect(QRectF(0, 0, svcWidth, height()), m_svcColor);
	painter.fillRect(QRectF(svcWidth, 0, svsWidth, height()), m_svsColor);
}

AICacheSettingsPage::AICacheSettingsPage(QWidget* parent)
	: QWidget(parent)
{
	setObjectName("aiCacheSettingsPage");
	auto* layout = new QVBoxLayout(this);
	auto* heading = new QLabel(QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "AI cache"), this);
	auto font = heading->font();
	font.setBold(true);
	heading->setFont(font);
	layout->addWidget(heading);
	m_totalUsage = new QLabel(this);
	m_totalUsage->setObjectName("aiCacheTotalUsage");
	auto usageFont = m_totalUsage->font();
	usageFont.setBold(true);
	usageFont.setPointSize(16);
	m_totalUsage->setFont(usageFont);
	m_totalCapacity = new QLabel(this);
	m_totalCapacity->setObjectName("aiCacheTotalCapacity");
	auto* totals = new QHBoxLayout;
	totals->addWidget(m_totalUsage);
	totals->addStretch();
	totals->addWidget(m_totalCapacity);
	layout->addLayout(totals);
	m_usageBar = new AICacheUsageBar(this);
	layout->addWidget(m_usageBar);
	m_svcLegend = new QLabel(this);
	m_svsLegend = new QLabel(this);
	auto* legend = new QHBoxLayout;
	legend->addWidget(m_svcLegend);
	legend->addStretch();
	legend->addWidget(m_svsLegend);
	layout->addLayout(legend);
	auto* clearAll = new QPushButton(
		QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Clear all caches"), this);
	clearAll->setObjectName("aiCacheClearAll");
	layout->addWidget(clearAll);
	connect(clearAll, &QPushButton::clicked, this, [this] { clearCaches(true, true); });

	auto* config = ConfigManager::inst();
	const auto limit = std::clamp(config->value("aiCache", "limitMiB", "2048").toLongLong(), 1LL, 1048576LL);
	aiCache::setLimit(limit * MiB);
	// Initialize the SVS legacy cache registration before enforcing the shared limit.
	svs::Cache::instance();
	aiCache::trim(config->aiCacheDir());
	auto* limitForm = new QFormLayout;
	m_limit = new QDoubleSpinBox(this);
	m_limit->setObjectName("aiCacheLimit");
	m_limit->setRange(0.01, 1024);
	m_limit->setDecimals(2);
	m_limit->setSingleStep(0.25);
	m_limit->setSuffix(" GiB");
	m_limit->setValue(double(limit * MiB) / GiB);
	limitForm->addRow(QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Maximum total cache size"), m_limit);
	layout->addLayout(limitForm);
	const auto addGroup = [this, layout](const QString& name, QLabel*& usage,
		const QString& action, bool svcCache) {
		auto* group = new QGroupBox(name, this);
		auto* form = new QFormLayout(group);
		usage = new QLabel(group);
		usage->setObjectName(svcCache ? "svcCacheUsage" : "svsCacheUsage");
		form->addRow(QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Current cache size"), usage);
		auto* clear = new QPushButton(action, group);
		clear->setObjectName(svcCache ? "svcCacheClear" : "svsCacheClear");
		form->addRow(clear);
		connect(clear, &QPushButton::clicked, this, [this, svcCache] { clearCaches(svcCache, !svcCache); });
		layout->addWidget(group);
	};
	addGroup("SVC", m_svcUsage,
		QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Clear SVC cache"), true);
	addGroup("SVS", m_svsUsage,
		QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Clear SVS cache"), false);
	auto* help = new QLabel(
		QCoreApplication::translate("lmms::gui::AICacheSettingsPage",
			"SVC and SVS share a total cache limit of 2 GiB by default. When the limit is exceeded, the oldest files "
			"across both caches are removed first. "
			"SVS usage includes synthesis audio and AI intermediate caches. Files used by running SVC jobs are kept. "
			"Cleared or evicted results may need to be rendered again when reopening a saved project."),
		this);
	help->setWordWrap(true);
	layout->addWidget(help);
	m_status = new QLabel(this);
	m_status->setObjectName("aiCacheStatus");
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	layout->addStretch();
	connect(m_limit, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
		[this] { refreshUsage(); });
	connect(m_usageBar, &AICacheUsageBar::colorsChanged, this, [this] { refreshUsage(); });
	refreshUsage();
	auto* timer = new QTimer(this);
	timer->setInterval(2000);
	connect(timer, &QTimer::timeout, this, [this] {
		if (isVisible())
		{
			refreshUsage();
		}
	});
	timer->start();
}

void AICacheSettingsPage::refreshUsage()
{
	const auto svcBytes = svc::cacheBytes(ConfigManager::inst()->aiCacheDir());
	const auto svsBytes = svs::Cache::instance().diskBytes();
	const auto capacity = qRound64(m_limit->value() * 1024) * MiB;
	const QLocale locale;
	m_totalUsage->setText(locale.formattedDataSize(svcBytes + svsBytes, 2));
	m_totalUsage->setAccessibleName(
		QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Total cache size: %1").arg(m_totalUsage->text()));
	m_totalCapacity->setText(
		QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Limit: %1")
			.arg(locale.formattedDataSize(capacity, 2)));
	m_svcUsage->setText(locale.formattedDataSize(svcBytes, 2));
	m_svsUsage->setText(locale.formattedDataSize(svsBytes, 2));
	const auto legend = [](const QColor& color, const QString& name, const QString& size) {
		return QString("<span style=\"color:%1\">●</span> %2 · %3").arg(color.name(), name, size);
	};
	m_svcLegend->setText(legend(m_usageBar->svcColor(), "SVC", m_svcUsage->text()));
	m_svsLegend->setText(legend(m_usageBar->svsColor(), "SVS", m_svsUsage->text()));
	m_usageBar->setUsage(svcBytes, svsBytes, capacity);
	m_usageBar->setToolTip("SVC: " + m_svcUsage->text() + "\nSVS: " + m_svsUsage->text() + '\n'
		+ QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Available: %1")
			.arg(locale.formattedDataSize(std::max(qint64(0), capacity - svcBytes - svsBytes), 2)));
}

void AICacheSettingsPage::clearCaches(bool svcCache, bool svsCache)
{
	bool cleared = true;
	if (svcCache)
	{
		cleared &= svc::clearCache(ConfigManager::inst()->aiCacheDir());
	}
	if (svsCache)
	{
		cleared &= svs::Cache::instance().clear();
	}
	m_status->setText(cleared ? QCoreApplication::translate("lmms::gui::AICacheSettingsPage", "Cache cleared.")
		: QCoreApplication::translate("lmms::gui::AICacheSettingsPage",
			"Some cache files are in use or could not be removed. Remaining usage is shown above."));
	refreshUsage();
}

void AICacheSettingsPage::save()
{
	auto* config = ConfigManager::inst();
	const auto limit = qRound64(m_limit->value() * 1024);
	config->setValue("aiCache", "limitMiB", QString::number(limit));
	aiCache::setLimit(limit * MiB);
	aiCache::trim(config->aiCacheDir());
	refreshUsage();
}
} // namespace lmms::gui
