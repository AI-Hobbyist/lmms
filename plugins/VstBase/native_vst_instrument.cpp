#include <QDomElement>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "AudioEngine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "InstrumentView.h"
#include "NativeVstSelection.h"
#include "Song.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms {

extern "C" {
Plugin::Descriptor PLUGIN_EXPORT native_vst_instrument_plugin_descriptor{"native_vst_instrument", "VST Instruments",
	QT_TRANSLATE_NOOP("PluginBrowser", "Native VST instruments"), "LMMS", 0x0100, Plugin::Type::Instrument,
	new PixmapLoader("instrument_plugin"), nullptr, new vsthost::NativeVstFeatures(Plugin::Type::Instrument)};
}

namespace {

class NativeVstInstrument final : public Instrument
{
public:
	NativeVstInstrument(InstrumentTrack* track, const vsthost::NativeVstKey* key)
		: Instrument(track, &native_vst_instrument_plugin_descriptor, key, Flag::IsSingleStreamed | Flag::IsMidiBased)
	{
		if (key)
		{
			m_plugin = vsthost::openNativeVst(*key);
			if (!m_plugin)
			{
				collectErrorForUI(
					VstPlugin::tr("The VST plugin %1 could not be loaded.").arg(key->attributes.value("file")));
			}
			else
			{
				setDisplayName(m_plugin->name());
			}
		}
		Engine::audioEngine()->addPlayHandle(new InstrumentPlayHandle(this, track));
	}

	~NativeVstInstrument() override { Engine::audioEngine()->removeInstrumentPlayHandles(this); }

	void play(SampleFrame* buffer) override
	{
		if (m_plugin) { m_plugin->process(nullptr, buffer); }
		else
		{
			std::fill_n(buffer, Engine::audioEngine()->framesPerPeriod(), SampleFrame{});
		}
	}

	bool handleMidiEvent(const MidiEvent& event, const TimePos&, f_cnt_t offset) override
	{
		if (m_plugin) { m_plugin->processMidiEvent(event, offset); }
		return true;
	}

	void loadSettings(const QDomElement& state) override
	{
		if (m_plugin) { m_plugin->loadSettings(state); }
	}

	void saveSettings(QDomDocument& document, QDomElement& state) override
	{
		if (m_plugin) { m_plugin->saveSettings(document, state); }
	}

	QString nodeName() const override { return "native_vst_instrument"; }

	gui::PluginView* instantiateView(QWidget* parent) override
	{
		auto* view = new gui::InstrumentViewFixedSize(this, parent);
		auto* layout = new QVBoxLayout(view);
		auto* open = new QPushButton(VstPlugin::tr("Show/hide GUI"), view);
		open->setEnabled(m_plugin && m_plugin->hasEditor());
		if (m_plugin) { QObject::connect(open, &QPushButton::clicked, m_plugin.get(), &VstPlugin::toggleUI); }
		layout->addWidget(open);
		layout->addStretch();
		return view;
	}

private:
	std::unique_ptr<VstPlugin> m_plugin;
};

} // namespace

extern "C" PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{ return new NativeVstInstrument(static_cast<InstrumentTrack*>(parent), static_cast<vsthost::NativeVstKey*>(data)); }

} // namespace lmms
