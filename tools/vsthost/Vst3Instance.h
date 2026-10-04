#ifndef LMMS_VSTHOST_VST3_INSTANCE_H
#define LMMS_VSTHOST_VST3_INSTANCE_H
#include "Vst3Module.h"
#include "Vst3ComponentHandler.h"
#include "Vst3ParameterChanges.h"
#include "Vst3Editor.h"
#include "Vst3EventList.h"
#include "vsthost/Vst3BlockEvents.h"
#include "vsthost/Vst3OutputEvents.h"
#include "vsthost/Vst3ParameterFeedback.h"
#include "vsthost/Vst3Metadata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <array>
#include <span>
#include <unordered_map>

namespace lmms::vsthost
{
// Single native instance, used only on the helper's owner thread. Destruction
// occurs before module unloading; the process supervisor bounds native calls.
class Vst3Instance
{
public:
	Vst3Instance() = default;
	~Vst3Instance();
	Vst3Instance(const Vst3Instance&) = delete;
	Vst3Instance& operator=(const Vst3Instance&) = delete;
	void open(const std::filesystem::path& path, const std::array<std::uint8_t, 16>& cid,
		Steinberg::Vst::IComponentHandler* handler = nullptr);
	void close();
	void setup(double rate, std::uint32_t frames, bool offline = false);
	bool process(std::uint32_t frames, std::span<const float> input, std::span<float> output,
		std::span<const std::uint8_t> events = {}, std::uint64_t sequence = 0);
	std::vector<std::uint8_t> midiOutput();
	std::vector<std::uint8_t> state();
	void restoreState(std::span<const std::uint8_t> bytes);
	std::vector<std::uint8_t> parameterControl(std::span<const std::uint8_t> command);
	HWND showEditor();
	void hideEditor();
	// Call between native audio blocks on the helper's owner/control thread.
	void serviceControl();
	Steinberg::Vst::IEditController* controller() const noexcept { return m_controller.get(); }
	std::uint32_t inputChannels() const noexcept { return m_inputs.channels; }
	std::uint32_t outputChannels() const noexcept { return m_outputs.channels; }
	Steinberg::int32 sampleSize() const noexcept { return m_sampleSize; }
	std::uint32_t latency() const noexcept { return m_latency; }
	const std::vector<Steinberg::Vst::ParameterInfo>& parameters() const noexcept { return m_parameters; }
private:
	struct Buffers
	{
		std::uint32_t channels = 0;
		std::vector<Steinberg::Vst::AudioBusBuffers> buses;
		std::vector<float> samples32;
		std::vector<double> samples64;
		std::vector<float*> pointers32;
		std::vector<double*> pointers64;
	};
	void prepareBuses(Steinberg::Vst::BusDirection direction, Buffers& buffers);
	bool collectEdits();
	void synchronizeController();
	void refreshMidiAssignments();
	void refreshParameters();
	std::vector<std::uint8_t> metadata();
	std::unique_ptr<Vst3Module> m_module;
	std::filesystem::path m_path;
	std::array<std::uint8_t, 16> m_cid{};
	Steinberg::IPtr<Steinberg::Vst::IComponentHandler> m_externalHandler;
	Steinberg::IPtr<Steinberg::Vst::HostApplication> m_host;
	Steinberg::IPtr<Steinberg::Vst::IComponent> m_component;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> m_processor;
	Steinberg::IPtr<Steinberg::Vst::IEditController> m_controller;
	Steinberg::IPtr<Vst3ComponentHandler> m_handler;
	Steinberg::IPtr<Vst3Editor> m_editor;
	std::unique_ptr<Vst3ParameterChanges> m_inputChanges, m_outputChanges;
	struct PendingParameter { double value = 0; bool dirty = false; };
	std::vector<PendingParameter> m_pendingParameters;
	std::vector<PendingParameter> m_controllerParameters;
	std::unordered_map<Steinberg::Vst::ParamID, std::size_t> m_parameterIndices;
	std::vector<Vst3ParameterFeedback> m_feedback;
	Vst3EventList m_inputEvents, m_outputEvents;
	std::vector<Vst3OutputEvent> m_midiFeedback;
	std::vector<Steinberg::int32> m_eventInputs;
	struct MidiAssignment { Steinberg::Vst::ParamID id = 0; bool assigned = false; };
	std::vector<MidiAssignment> m_midiAssignments;
	Steinberg::IPtr<Steinberg::Vst::ConnectionProxy> m_componentConnection, m_controllerConnection;
	std::vector<Steinberg::Vst::ParameterInfo> m_parameters;
	Buffers m_inputs, m_outputs;
	Steinberg::Vst::ProcessData m_processData;
	Steinberg::Vst::ProcessContext m_context{};
	Steinberg::int32 m_sampleSize = Steinberg::Vst::kSample32;
	std::uint32_t m_maxFrames = 0, m_latency = 0;
	std::uint32_t m_restartFlags = 0;
	bool m_componentInitialized = false, m_controllerInitialized = false;
	bool m_handlerInstalled = false, m_active = false, m_processing = false;
};
} // namespace lmms::vsthost
#endif
