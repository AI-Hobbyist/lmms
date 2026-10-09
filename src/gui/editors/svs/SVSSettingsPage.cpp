#include "SVSSettingsPage.h"
#include <QPushButton>
#include <QDebug>
#include "ConfigManager.h"
#include "SVSSynthesisScheduler.h"
#include "Engine.h"
#include "Song.h"
#include "SVSTrack.h"
#include "SVSClip.h"
#include <QComboBox>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonDocument>
#include <QPointer>
#include <QRunnable>
#include <QSet>
#include <QTabWidget>
#include <QSlider>
#include <QCheckBox>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dxgi.h>
#endif
namespace lmms::gui {
QString SVSSettingsPage::engineLabel(const svs::Voice& voice)
{
	const auto type = voice.metadata["engineType"].toString();
	const auto category = type == "ai" ? tr("AI")
		: type == "concatenative"	   ? tr("Traditional concatenation")
		: type == "example"			   ? tr("Non-AI example")
									   : tr("Type not declared");
	return tr("%1 (%2)").arg(voice.metadata["pluginName"].toString(voice.pluginId), category);
}
SVSSettingsPage::SVSSettingsPage(QWidget* parent)
	: QWidget(parent)
{
	setObjectName("svsSettingsPage");
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* heading = new QLabel(tr("SVS"), this);
	auto font = heading->font();
	font.setBold(true);
	heading->setFont(font);
	layout->addWidget(heading);
	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	layout->addWidget(scroll);
	auto* body = new QWidget(scroll);
	scroll->setWidget(body);
	auto* controls = new QVBoxLayout(body);
	auto* form = new QFormLayout;
	controls->addLayout(form);
	m_backend = new QComboBox(body);
	m_backend->setObjectName("svsComputeBackend");
	m_backend->addItem("CPU", "cpu");
	for (const auto& name : QStringList{"DirectML", "LibTorch", "Vulkan"})
		m_backend->addItem(tr("%1 — Coming soon").arg(name), name.toLower());
	form->addRow(tr("AI voicebank computation engine"), m_backend);
	m_device = new QComboBox(body);
	m_device->setObjectName("svsComputeDevice");
	m_device->addItem("CPU", "cpu");
#ifdef Q_OS_WIN
	// Discover hardware without adding a GPU SDK or runtime dependency.
	const auto library = LoadLibraryW(L"dxgi.dll");
	if (library)
	{
		using FactoryFunction = HRESULT(WINAPI*)(REFIID, void**);
		const auto create = reinterpret_cast<FactoryFunction>(GetProcAddress(library, "CreateDXGIFactory1"));
		IDXGIFactory1* factory = nullptr;
		if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
		{
			for (UINT index = 0;; ++index)
			{
				IDXGIAdapter1* adapter = nullptr;
				if (factory->EnumAdapters1(index, &adapter) != S_OK)
					break;
				DXGI_ADAPTER_DESC1 description{};
				if (SUCCEEDED(adapter->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
				{
					const auto id = QString("dxgi:%1:%2")
										.arg(quint32(description.AdapterLuid.HighPart), 8, 16, QChar('0'))
										.arg(description.AdapterLuid.LowPart, 8, 16, QChar('0'));
					m_device->addItem(QString::fromWCharArray(description.Description), id);
				}
				adapter->Release();
			}
			factory->Release();
		}
		FreeLibrary(library);
	}
#endif
	form->addRow(tr("Device"), m_device);
	auto* hint = new QLabel(
		tr("Backend and device options apply only to AI voicebanks. CPU is the available backend. Other backends are placeholders for testing device selection; synthesis continues to use CPU."),
		body);
	hint->setObjectName("svsComputeHint");
	hint->setWordWrap(true);
	controls->addWidget(hint);
	auto* config = ConfigManager::inst();
	m_backend->setCurrentIndex(std::max(0, m_backend->findData(config->value("svs", "computeBackend", "cpu"))));
	m_device->setCurrentIndex(std::max(0, m_device->findData(config->value("svs", "computeDevice", "cpu"))));
	m_pitchRanges = new QCheckBox(tr("Show voicebank pitch ranges"), body);
	m_pitchRanges->setObjectName("svsShowVoicePitchRanges");
	m_pitchRanges->setChecked(config->value("svs", "showVoicePitchRanges", "1").toInt() != 0);
	m_pitchRanges->setToolTip(tr(
		"Mark available, comfortable and weak pitches on the keyboard and show their ranges in the sidebar when the voicebank declares them."));
	controls->addWidget(m_pitchRanges);
	m_backgroundWaveform = new QCheckBox(tr("Show translucent background waveform"), body);
	m_backgroundWaveform->setObjectName("svsShowBackgroundWaveform");
	m_backgroundWaveform->setChecked(config->value("svs", "showBackgroundWaveform", "0").toInt() != 0);
	m_backgroundWaveform->setToolTip(tr("Display the synthesized audio waveform behind SVS clips in the Song Editor."));
	controls->addWidget(m_backgroundWaveform);
	auto updateDevice = [this] {
		const bool cpu = m_backend->currentData().toString() == "cpu";
		if (cpu)
			m_device->setCurrentIndex(0);
		m_device->setEnabled(!cpu);
	};
	connect(m_backend, qOverload<int>(&QComboBox::currentIndexChanged), this, [updateDevice](int) { updateDevice(); });
	updateDevice();
	m_engine = new QTabWidget(body);
	m_engine->setObjectName("svsEngineTabs");
	QSet<QString> seen;
	for (const auto& installed : svs::Registry::instance().engines())
	{
		svs::Voice voice;
		voice.pluginId = installed.id;
		voice.package = installed.package;
		voice.metadata = {{"pluginName", installed.name}, {"engineType", installed.type}};
		for (const auto& candidate : svs::Registry::instance().voices())
			if (candidate.pluginId == installed.id)
			{
				voice.id = candidate.id;
				break;
			}
		seen.insert(voice.pluginId);
		m_voices.append(voice);
		auto* page = new QWidget(m_engine);
		page->setObjectName("svsEnginePage." + voice.pluginId);
		auto* pageLayout = new QVBoxLayout(page);
		auto* status = new QLabel(page);
		status->setObjectName("svsEngineStatus");
		status->setWordWrap(true);
		pageLayout->addWidget(status);
		pageLayout->addWidget(new SVSParameterPanel(page));
		pageLayout->addStretch();
		auto plugin = svs::Registry::instance().plugin(installed.id);
		if (plugin && plugin->hasCatalogQuery())
		{
			auto* rescan = new QPushButton(tr("Rescan voicebanks"), page);
			rescan->setObjectName("svsRescanVoicebanks");
			rescan->setToolTip(tr("Rescan the applied voicebank directories. Apply directory changes first."));
			pageLayout->insertWidget(pageLayout->count() - 1, rescan);
			connect(rescan, &QPushButton::clicked, this, [key = installed.id] {
				const auto settings = QJsonDocument::fromJson(
					ConfigManager::inst()
						->value("svsEngineSettings", "engine_" + QString::fromLatin1(key.toUtf8().toHex()))
						.toUtf8())
										  .object();
				svs::Registry::instance().refreshCatalogAsync(key, settings);
			});
			connect(&svs::Registry::instance(), &svs::Registry::catalogScanStarted, page,
				[this, key = installed.id, status, rescan](const QString& id) {
					if (id != key)
						return;
					status->setText(tr("Scanning voicebanks…"));
					rescan->setEnabled(false);
				});
			connect(&svs::Registry::instance(), &svs::Registry::catalogScanFinished, page,
				[this, key = installed.id, status, rescan](const QString& id, const QString& error) {
					if (id != key)
						return;
					rescan->setEnabled(true);
					int count = 0;
					for (const auto& voice : svs::Registry::instance().voices())
						if (voice.pluginId == key)
							++count;
					status->setText(error.isEmpty() ? tr("Voicebanks found: %1").arg(count) : error);
				});
			rescan->setEnabled(!svs::Registry::instance().scanning(installed.id));
		}
		m_engine->addTab(page, engineLabel(voice));
	}
	auto* aiExample = new QWidget(m_engine);
	aiExample->setObjectName("svsEnginePage.aiExample");
	auto* aiLayout = new QVBoxLayout(aiExample);
	auto* aiHint = new QLabel(
		tr("AI engine settings example — layout preview only; no synthesis engine is installed for this example."),
		aiExample);
	aiHint->setWordWrap(true);
	aiLayout->addWidget(aiHint);
	auto* stepsForm = new QFormLayout;
	auto* stepsRow = new QHBoxLayout;
	m_aiSteps = new QSlider(Qt::Horizontal, aiExample);
	m_aiSteps->setObjectName("svsAiExampleRenderSteps");
	m_aiSteps->setRange(1, 100);
	m_aiSteps->setValue(std::clamp(config->value("svs", "aiExampleRenderSteps", "20").toInt(), 1, 100));
	m_aiSteps->setToolTip(tr("Example rendering steps: 1–100 (default 20)"));
	m_aiSteps->setAccessibleName(tr("Example rendering steps"));
	auto* stepsValue = new QLabel(QString::number(m_aiSteps->value()), aiExample);
	stepsValue->setObjectName("svsAiExampleRenderStepsValue");
	stepsValue->setMinimumWidth(30);
	stepsRow->addWidget(m_aiSteps);
	stepsRow->addWidget(stepsValue);
	stepsForm->addRow(tr("Rendering steps (1–100)"), stepsRow);
	aiLayout->addLayout(stepsForm);
	connect(m_aiSteps, &QSlider::valueChanged, stepsValue,
		[stepsValue](int value) { stepsValue->setText(QString::number(value)); });
	aiLayout->addStretch();
	m_engine->addTab(aiExample, tr("AI example (AI)"));
	controls->addWidget(m_engine);
	if (m_voices.isEmpty())
		controls->addWidget(new QLabel(tr("No SVS engine is installed."), body));
	controls->addStretch();
	connect(m_engine, &QTabWidget::currentChanged, this, [this](int) { refreshEngine(); });
	refreshEngine();
	connect(m_backend, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refreshEngine(); });
	connect(m_device, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refreshEngine(); });
}
void SVSSettingsPage::refreshEngine()
{
	const auto request = ++m_request;
	m_schema.clear();
	if (m_engine->currentIndex() < 0 || m_engine->currentIndex() >= m_voices.size())
		return;
	auto* page = m_engine->currentWidget();
	m_status = page->findChild<QLabel*>("svsEngineStatus");
	m_parameters = static_cast<SVSParameterPanel*>(page->findChild<QWidget*>("svsParameterPanel"));
	m_parameters->refresh({}, "track", {}, {}, {});
	const auto voice = m_voices[m_engine->currentIndex()];
	const auto key = voice.pluginId;
	if (!m_values.contains(key))
		m_values[key] = QJsonDocument::fromJson(
			ConfigManager::inst()
				->value("svsEngineSettings", "engine_" + QString::fromLatin1(key.toUtf8().toHex()))
				.toUtf8())
							.object();
	const auto values = m_values[key];
	const QJsonObject context{{"engineSettings", values}, {"computeBackend", m_backend->currentData().toString()},
		{"computeDevice", m_device->currentData().toString()}};
	auto plugin = svs::Registry::instance().plugin(key);
	m_status->setText(tr("Loading engine options…"));
	QPointer<SVSSettingsPage> target(this);
	svs::SynthesisScheduler::instance().declarationPool().start(
		QRunnable::create([target, plugin, voice, values, context, request] {
			QString error;
			QJsonObject declaration;
			if (plugin)
				declaration = plugin->engineSettings(voice.id, context, error);
			else
				error = "Engine unavailable";
			svs::Capabilities parsed;
			if (declaration.contains("engineSettings") && !declaration["engineSettings"].isArray())
				error = "Engine settings must be an array";
			auto schema = declaration["engineSettings"].toArray();
			for (int i = 0; i < schema.size(); ++i)
			{
				auto parameter = schema[i].toObject();
				parameter["scope"] = "track";
				parameter["curve"] = false;
				schema[i] = parameter;
			}
			if (error.isEmpty())
				svs::Capabilities::parse(
					{{"schemaVersion", 1}, {"parameters", schema}, {"feedbackParameters", QJsonArray{}},
						{"languages", QJsonArray{"en"}}, {"defaultLanguage", "en"}},
					parsed, error);
			if (!target)
				return;
			QMetaObject::invokeMethod(
				target,
				[target, request, voice, parsed, error, declaration] {
					if (!target || target->m_request != request)
						return;
					if (!error.isEmpty())
					{
						target->m_status->setText(error);
						return;
					}
					const auto key = voice.pluginId;
					auto namedVoice = voice;
					if (!declaration["name"].toString().isEmpty())
						namedVoice.metadata["pluginName"] = declaration["name"];
					if (!declaration["engineType"].toString().isEmpty())
						namedVoice.metadata["engineType"] = declaration["engineType"];
					target->m_engine->setTabText(target->m_engine->currentIndex(), engineLabel(namedVoice));
					target->m_schema = parsed.parameters;
					auto& values = target->m_values[key];
					for (const auto& parameter : parsed.parameters)
						if (!parameter.accepts(values[parameter.id]))
							values[parameter.id] = parameter.defaultValue;
					int count = 0;
					for (const auto& catalogVoice : svs::Registry::instance().voices())
						if (catalogVoice.pluginId == key)
							++count;
					target->m_status->setText(svs::Registry::instance().scanning(key) ? tr("Scanning voicebanks…")
							: count == 0 ? tr("No voicebanks found. Configure directories and apply, then rescan.")
							: parsed.parameters.isEmpty() ? tr("This engine does not declare additional options.")
														  : tr("Voicebanks found: %1").arg(count));
					target->m_parameters->refresh(parsed.parameters, "track", {values}, values,
						[target, key](const QString& id, const QJsonValue& value) {
							if (!target)
								return;
							target->m_values[key][id] = value;
							// Refresh after the parameter setter has returned; refreshing synchronously
							// replaces the std::function which is currently executing this callback.
							QMetaObject::invokeMethod(
								target,
								[target] {
									if (target)
										target->refreshEngine();
								},
								Qt::QueuedConnection);
						});
				},
				Qt::QueuedConnection);
		}));
}
void SVSSettingsPage::save()
{
	auto* config = ConfigManager::inst();
	config->setValue("svs", "computeBackend", m_backend->currentData().toString());
	config->setValue("svs", "computeDevice",
		m_backend->currentData().toString() == "cpu" ? "cpu" : m_device->currentData().toString());
	config->setValue("svs", "aiExampleRenderSteps", QString::number(m_aiSteps->value()));
	config->setValue("svs", "showVoicePitchRanges", m_pitchRanges->isChecked() ? "1" : "0");
	config->setValue("svs", "showBackgroundWaveform", m_backgroundWaveform->isChecked() ? "1" : "0");
	QSet<QString> changed;
	for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
	{
		const auto key = "engine_" + QString::fromLatin1(it.key().toUtf8().toHex());
		const auto json = QString::fromUtf8(QJsonDocument(it.value()).toJson(QJsonDocument::Compact));
		if (config->value("svsEngineSettings", key) != json)
		{
			auto previous = QJsonDocument::fromJson(config->value("svsEngineSettings", key).toUtf8()).object();
			auto current = it.value();
			config->setValue("svsEngineSettings", key, json);
			auto plugin = svs::Registry::instance().plugin(it.key());
			if (plugin && plugin->hasCatalogQuery())
			{
				svs::Registry::instance().refreshCatalogAsync(it.key(), it.value(), false);
				previous.remove("diffsinger.voicebankDirectories");
				current.remove("diffsinger.voicebankDirectories");
			}
			if (it.key() == "org.lmms.svs.diffsinger")
			{
				previous.remove("diffsinger.showPhonemeLanguagePrefix");
				current.remove("diffsinger.showPhonemeLanguagePrefix");
			}
			if (previous != current)
				changed.insert(it.key());
		}
	}
	for (auto* base : Engine::getSong()->tracks())
		if (base->type() == Track::Type::SVS)
		{
			auto* track = static_cast<SVSTrack*>(base);
			if (!changed.contains(track->pluginId()))
				continue;
			for (auto* item : track->getClips())
			{
				auto* clip = static_cast<SVSClip*>(item);
				clip->invalidate();
				clip->synthesize();
			}
		}
}
}
