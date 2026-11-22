#include <QDomElement>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "NativeVstModels.h"
#include "NativeVstSelection.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms {

extern "C" {
Plugin::Descriptor PLUGIN_EXPORT native_vst_effect_plugin_descriptor{"native_vst_effect", "VST",
	QT_TRANSLATE_NOOP("PluginBrowser", "Native VST effects"), "LMMS", 0x0100, Plugin::Type::Effect,
	new PixmapLoader("setup_audio"), nullptr, new vsthost::NativeVstFeatures(Plugin::Type::Effect)};
}

namespace {

class NativeVstEffectControls final : public EffectControls
{
public:
	NativeVstEffectControls(Effect* effect, VstPlugin* plugin)
		: EffectControls(effect)
		, m_plugin(plugin)
	{
		if (m_plugin)
		{
			m_parameters.initialize(*m_plugin, this);
		}
	}

	QString nodeName() const override { return "native_vst_effect_controls"; }
	int controlCount() override { return m_plugin ? 1 : 0; }
	QMap<QString, AutomatableModel*> parameterModels() override { return m_parameters.models(); }

	void loadSettings(const QDomElement& state) override
	{
		if (m_plugin)
		{
			m_plugin->loadSettings(state);
			m_parameters.load(*m_plugin, state);
		}
	}

	void saveSettings(QDomDocument& document, QDomElement& state) override
	{
		if (m_plugin)
		{
			m_plugin->saveSettings(document, state);
			m_parameters.save(document, state);
		}
	}

	gui::EffectControlDialog* createView() override
	{
		auto* dialog = new gui::EffectControlDialog(this);
		auto* layout = new QVBoxLayout(dialog);
		auto* open = new QPushButton(VstPlugin::tr("Show/hide GUI"), dialog);
		open->setEnabled(m_plugin && m_plugin->hasEditor());
		if (m_plugin) { QObject::connect(open, &QPushButton::clicked, m_plugin, &VstPlugin::toggleUI); }
		layout->addWidget(open);
		m_parameters.addControls(layout);
		return dialog;
	}

private:
	VstPlugin* m_plugin;
	NativeVstModels m_parameters;
};

class NativeVstEffect final : public Effect, public vsthost::VstNodeOwner
{
public:
	NativeVstEffect(Model* parent, const vsthost::NativeVstKey* key)
		: Effect(&native_vst_effect_plugin_descriptor, parent, key)
		, m_plugin(key ? vsthost::openNativeVst(*key) : nullptr)
		, m_controls(this, m_plugin.get())
		, m_vst3(key && key->attributes.value("format") == "vst3")
	{
		setDontRun(!m_plugin);
		setOkay(static_cast<bool>(m_plugin));
		if (key && !m_plugin)
		{
			collectErrorForUI(
				VstPlugin::tr("The VST plugin %1 could not be loaded.").arg(key->attributes.value("file")));
		}
		else if (m_plugin) { setDisplayName(m_plugin->name()); }
	}

	EffectControls* controls() override { return &m_controls; }
	vsthost::VstNodeProcessor* vstNodeProcessor() noexcept override { return m_plugin.get(); }

protected:
	ProcessStatus processImpl(SampleFrame* buffer, const f_cnt_t frames) override
	{
		static thread_local std::array<SampleFrame, MAXIMUM_BUFFER_SIZE> input;
		std::copy_n(buffer, frames, input.begin());
		m_plugin->processEffect(input.data(), buffer, frames, wetLevel(), dryLevel());
		return m_vst3 || m_plugin->hasNodeInput() ? ProcessStatus::Continue : ProcessStatus::ContinueIfNotQuiet;
	}

private:
	std::unique_ptr<VstPlugin> m_plugin;
	NativeVstEffectControls m_controls;
	bool m_vst3;
};

} // namespace

extern "C" PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{ return new NativeVstEffect(parent, static_cast<vsthost::NativeVstKey*>(data)); }

} // namespace lmms
