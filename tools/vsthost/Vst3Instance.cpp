#include "Vst3Instance.h"
#include "vsthost/Vst3Commands.h"
#include "Vst3StateStream.h"
#include "vsthost/AudioQueue.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <chrono>
#include <limits>

namespace lmms::vsthost
{
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
class LmmsHostApplication final : public HostApplication
{
public:
	tresult PLUGIN_API getName(String128 name) override
	{
		if (!name) { return kInvalidArgument; }
		std::fill_n(name, 128, char16{}); std::copy_n(u"LMMS", 4, name); return kResultOk;
	}
};
std::string utf8(const String128& text)
{
	const auto end = std::find(std::begin(text), std::end(text), char16{});
	if (end == std::end(text)) { throw std::runtime_error("Unterminated VST3 text"); }
	const auto length = static_cast<int>(end - std::begin(text));
	if (!length) { return std::string{}; }
	const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, reinterpret_cast<const wchar_t*>(text), length, nullptr, 0, nullptr, nullptr);
	if (size <= 0) { throw std::runtime_error("Invalid VST3 text"); }
	std::string value(size, '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, reinterpret_cast<const wchar_t*>(text), length, value.data(), size, nullptr, nullptr);
	return value;
}

void require(tresult result, const char* operation)
{
	if (result != kResultOk) { throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")"); }
}
}

Vst3Instance::~Vst3Instance()
{
	try { close(); } catch (...) { /* The owner reports explicit close failures. */ }
}

void Vst3Instance::open(const std::filesystem::path& path, const std::array<std::uint8_t, 16>& cid,
	IComponentHandler* handler)
{
	if (m_module) { throw std::logic_error("VST3 instance already open"); }
	m_path = path; m_cid = cid; m_externalHandler = handler;
	m_module = std::make_unique<Vst3Module>();
	std::string error;
	if (!m_module->open(path, error)) { throw std::runtime_error(error); }
	bool selected = false;
	for (const auto& info : m_module->classes())
	{
		if (std::memcmp(info.ID().data(), cid.data(), cid.size()) == 0 && info.category() == kVstAudioEffectClass)
		{ selected = true; break; }
	}
	if (!selected) { throw std::runtime_error("Selected VST3 audio CID is absent from this module"); }
	m_host = owned(new LmmsHostApplication);
	const auto& factory = m_module->factory();
	factory.setHostContext(m_host);
	TUID nativeCid{}; std::memcpy(nativeCid, cid.data(), cid.size());
	m_component = factory.createInstance<IComponent>(VST3::UID(nativeCid));
	if (!m_component) { throw std::runtime_error("VST3 factory could not create IComponent"); }
	require(m_component->initialize(m_host), "Component initialize"); m_componentInitialized = true;
	m_processor = U::cast<IAudioProcessor>(m_component);
	if (!m_processor) { throw std::runtime_error("VST3 component has no IAudioProcessor"); }
	m_controller = U::cast<IEditController>(m_component);
	if (!m_controller)
	{
		TUID controllerCid{};
		if (m_component->getControllerClassId(controllerCid) == kResultOk)
		{
			m_controller = factory.createInstance<IEditController>(VST3::UID(controllerCid));
			if (!m_controller) { throw std::runtime_error("VST3 controller CID could not be instantiated"); }
			require(m_controller->initialize(m_host), "Controller initialize"); m_controllerInitialized = true;
		}
	}
	if (m_controller)
	{
		if (!handler) { m_handler = owned(new Vst3ComponentHandler); handler = m_handler; }
		require(m_controller->setComponentHandler(handler), "Set component handler"); m_handlerInstalled = true;
		if (m_controllerInitialized)
		{
			auto componentPoint = U::cast<IConnectionPoint>(m_component);
			auto controllerPoint = U::cast<IConnectionPoint>(m_controller);
			if (componentPoint && controllerPoint)
			{
				m_componentConnection = owned(new ConnectionProxy(componentPoint));
				m_controllerConnection = owned(new ConnectionProxy(controllerPoint));
				require(m_componentConnection->connect(controllerPoint), "Connect component");
				require(m_controllerConnection->connect(componentPoint), "Connect controller");
			}
		}
		// A controller must see its component's initial state before parameter
		// values are exposed, including when the same controller class serves CIDs
		// whose processor defaults differ.
		Vst3ComponentHandler::HostSetter initialSetter(m_handler);
		auto initial = owned(new Vst3StateStream);
		const auto initialResult = m_component->getState(initial);
		if (initialResult == kResultOk)
		{
			if (initial->failed()) { throw std::runtime_error("Invalid VST3 initial component state"); }
			require(initial->seek(0, IBStream::kIBSeekSet, nullptr), "Rewind initial state");
			require(m_controller->setComponentState(initial), "Initialize controller state");
		}
		const auto count = m_controller->getParameterCount();
		if (count < 0 || count > 65536) { throw std::runtime_error("Invalid VST3 parameter count"); }
		m_parameters.reserve(count);
		std::unordered_set<ParamID> identities;
		for (int32 index = 0; index < count; ++index)
		{
			ParameterInfo info{}; require(m_controller->getParameterInfo(index, info), "Get parameter metadata");
			if (!identities.insert(info.id).second || !std::isfinite(info.defaultNormalizedValue) ||
				info.defaultNormalizedValue < 0 || info.defaultNormalizedValue > 1 || info.stepCount < 0)
			{ throw std::runtime_error("Invalid VST3 parameter metadata"); }
			m_parameters.push_back(info);
		}
		m_pendingParameters.resize(m_parameters.size());
		m_controllerParameters.resize(m_parameters.size());
		for (std::size_t index = 0; index < m_parameters.size(); ++index) { m_parameterIndices.emplace(m_parameters[index].id, index); }
		m_feedback.reserve(RealtimeMidiQueue::Capacity);
		// Probe availability on the supervised native owner thread, without attaching a window.
		auto view = owned(m_controller->createView(ViewType::kEditor));
		m_hasEditor = view && view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultOk;
	}
	m_midiFeedback.reserve(512); m_blockMidiFeedback.reserve(512);
	m_inputChanges = std::make_unique<Vst3ParameterChanges>();
	m_outputChanges = std::make_unique<Vst3ParameterChanges>();
}

void Vst3Instance::close()
{
	// Complete every cleanup step even if a plug-in reports a failed return code.
	tresult failure = kResultOk;
	auto record = [&](tresult result) { if (result != kResultOk && failure == kResultOk) { failure = result; } };
	if (m_editor)
	{
		try { m_editor->close(); } catch (...) { failure = kResultFalse; }
		m_editor.reset();
	}
	if (m_processing) { record(m_processor->setProcessing(false)); m_processing = false; }
	if (m_active) { record(m_component->setActive(false)); m_active = false; }
	if (m_handlerInstalled) { record(m_controller->setComponentHandler(nullptr)); m_handlerInstalled = false; }
	if (m_controllerConnection) { if (!m_controllerConnection->disconnect()) { failure = kResultFalse; } m_controllerConnection.reset(); }
	if (m_componentConnection) { if (!m_componentConnection->disconnect()) { failure = kResultFalse; } m_componentConnection.reset(); }
	if (m_controllerInitialized) { record(m_controller->terminate()); m_controllerInitialized = false; }
	m_controller.reset(); m_handler.reset();
	if (m_componentInitialized) { record(m_component->terminate()); m_componentInitialized = false; }
	m_processor.reset(); m_component.reset();
	if (m_module && m_host) { m_module->factory().setHostContext(nullptr); }
	m_host.reset(); m_module.reset();
	m_path.clear(); m_cid = {}; m_externalHandler.reset();
	m_parameters.clear(); m_pendingParameters.clear(); m_parameterIndices.clear(); m_feedback.clear();
	m_controllerParameters.clear();
	m_inputChanges.reset(); m_outputChanges.reset();
	m_midiFeedback.clear(); m_blockMidiFeedback.clear(); m_eventInputs.clear(); m_eventBuses.clear(); m_midiAssignments.clear();
	m_restartFlags = 0; m_pendingRestart = 0; m_restartEventCount = 0; m_hasEditor = false;
	m_inputs = {}; m_outputs = {}; m_processData = {}; m_maxFrames = 0; m_latency = 0;
	require(failure, "VST3 close");
}

void Vst3Instance::prepareBuses(BusDirection direction, Buffers& buffers)
{
	const auto count = m_component->getBusCount(kAudio, direction);
	if (count < 0 || count > 32) { throw std::runtime_error("Invalid VST3 audio bus count"); }
	std::vector<SpeakerArrangement> arrangements(count);
	buffers = {}; buffers.buses.resize(count);
	for (int32 index = 0; index < count; ++index)
	{
		BusInfo info{}; require(m_component->getBusInfo(kAudio, direction, index, info), "Get audio bus");
		if (info.mediaType != kAudio || info.direction != direction || info.channelCount < 0 ||
			info.channelCount > 32 || buffers.channels + info.channelCount > AudioQueue::MaxChannels)
		{ throw std::runtime_error("Invalid VST3 audio bus channels"); }
		require(m_processor->getBusArrangement(direction, index, arrangements[index]), "Get bus arrangement");
		if (SpeakerArr::getChannelCount(arrangements[index]) != info.channelCount)
		{ throw std::runtime_error("Inconsistent VST3 bus arrangement"); }
		if (info.busType < kMain || info.busType > kAux || (info.flags & ~3u))
		{ throw std::runtime_error("Invalid VST3 audio bus type/flags"); }
		buffers.descriptors.push_back({0, static_cast<std::uint32_t>(direction), static_cast<std::uint32_t>(index),
			static_cast<std::uint32_t>(info.channelCount), static_cast<std::uint32_t>(info.busType), info.flags,
			buffers.channels, true, arrangements[index], utf8(info.name)});
		buffers.buses[index].numChannels = info.channelCount;
		buffers.channels += info.channelCount;
	}
	if (m_sampleSize == kSample32)
	{
		buffers.samples32.resize(static_cast<std::size_t>(m_maxFrames) * buffers.channels);
		buffers.pointers32.resize(buffers.channels);
		for (std::uint32_t channel = 0; channel < buffers.channels; ++channel)
		{ buffers.pointers32[channel] = buffers.samples32.data() + static_cast<std::size_t>(channel) * m_maxFrames; }
	}
	else
	{
		buffers.samples64.resize(static_cast<std::size_t>(m_maxFrames) * buffers.channels);
		buffers.pointers64.resize(buffers.channels);
		for (std::uint32_t channel = 0; channel < buffers.channels; ++channel)
		{ buffers.pointers64[channel] = buffers.samples64.data() + static_cast<std::size_t>(channel) * m_maxFrames; }
	}
	std::uint32_t channelOffset = 0;
	for (auto& bus : buffers.buses)
	{
		if (m_sampleSize == kSample32) { bus.channelBuffers32 = bus.numChannels ? buffers.pointers32.data() + channelOffset : nullptr; }
		else { bus.channelBuffers64 = bus.numChannels ? buffers.pointers64.data() + channelOffset : nullptr; }
		channelOffset += bus.numChannels;
	}
}

void Vst3Instance::setup(double rate, std::uint32_t frames, bool offline)
{
	if (!m_componentInitialized || m_active || !std::isfinite(rate) || rate < 8000 || rate > 768000 ||
		!frames || frames > AudioQueue::MaxFrames) { throw std::runtime_error("Invalid VST3 processing setup"); }
	m_maxFrames = frames;
	m_eventInputs.clear(); m_eventBuses.clear();
	m_sampleSize = m_processor->canProcessSampleSize(kSample32) == kResultOk ? kSample32 : kSample64;
	require(m_processor->canProcessSampleSize(m_sampleSize), "Negotiate sample size");
	prepareBuses(kInput, m_inputs); prepareBuses(kOutput, m_outputs);
	std::vector<SpeakerArrangement> inputs(m_inputs.buses.size()), outputs(m_outputs.buses.size());
	for (int32 index = 0; index < static_cast<int32>(inputs.size()); ++index)
	{ require(m_processor->getBusArrangement(kInput, index, inputs[index]), "Input arrangement"); }
	for (int32 index = 0; index < static_cast<int32>(outputs.size()); ++index)
	{ require(m_processor->getBusArrangement(kOutput, index, outputs[index]), "Output arrangement"); }
	require(m_processor->setBusArrangements(inputs.data(), static_cast<int32>(inputs.size()), outputs.data(), static_cast<int32>(outputs.size())), "Set bus arrangements");
	// Arrangements can change channel counts even when accepted. Revalidate and
	// allocate all native planar storage before activating the component.
	prepareBuses(kInput, m_inputs); prepareBuses(kOutput, m_outputs);
	for (auto media : {kAudio, kEvent})
	{
		for (auto direction : {kInput, kOutput})
		{
			const auto count = m_component->getBusCount(media, direction);
			if (count < 0 || count > 32) { throw std::runtime_error("Invalid VST3 bus count"); }
			for (int32 index = 0; index < count; ++index)
			{
				BusInfo info{}; require(m_component->getBusInfo(media, direction, index, info), "Get bus activation metadata");
				if (info.mediaType != media || info.direction != direction || info.busType < kMain || info.busType > kAux ||
					(media == kEvent && (info.channelCount < 0 || info.channelCount > 16 || (info.flags & ~1u))))
				{ throw std::runtime_error("Invalid VST3 bus activation metadata"); }
				if (media == kEvent)
				{
					m_eventBuses.push_back({1, static_cast<std::uint32_t>(direction), static_cast<std::uint32_t>(index),
						static_cast<std::uint32_t>(info.channelCount), static_cast<std::uint32_t>(info.busType), info.flags,
						0, true, 0, utf8(info.name)});
				}
				if (media == kEvent && direction == kInput)
				{
					if (info.channelCount < 0 || info.channelCount > 16) { throw std::runtime_error("Invalid VST3 event bus channels"); }
					m_eventInputs.push_back(info.channelCount);
				}
				require(m_component->activateBus(media, direction, index, true), "Activate bus");
			}
		}
	}
	refreshMidiAssignments();
	ProcessSetup setup{}; setup.processMode = offline ? kOffline : kRealtime;
	setup.symbolicSampleSize = m_sampleSize; setup.maxSamplesPerBlock = static_cast<int32>(frames); setup.sampleRate = rate;
	require(m_processor->setupProcessing(setup), "Setup processing");
	require(m_component->setActive(true), "Activate component"); m_active = true;
	m_latency = m_processor->getLatencySamples();
	require(m_processor->setProcessing(true), "Start processing"); m_processing = true;
	m_processData.processMode = setup.processMode; m_processData.symbolicSampleSize = m_sampleSize;
	m_processData.numInputs = static_cast<int32>(m_inputs.buses.size()); m_processData.inputs = m_inputs.buses.data();
	m_processData.numOutputs = static_cast<int32>(m_outputs.buses.size()); m_processData.outputs = m_outputs.buses.data();
	m_processData.inputParameterChanges = m_inputChanges.get(); m_processData.outputParameterChanges = m_outputChanges.get();
	m_processData.inputEvents = &m_inputEvents; m_processData.outputEvents = &m_outputEvents;
	m_context = {}; m_context.sampleRate = rate; m_context.tempo = 120; m_context.timeSigNumerator = 4; m_context.timeSigDenominator = 4;
	m_context.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid | ProcessContext::kProjectTimeMusicValid |
		ProcessContext::kBarPositionValid | ProcessContext::kContTimeValid;
	m_processData.processContext = &m_context;
}

void Vst3Instance::refreshMidiAssignments()
{
	// IMidiMapping is a control-thread interface. Reset obsolete assignments as
	// well as querying newly learned ones before the next native process call.
	m_midiAssignments.assign(m_eventInputs.size() * 16 * 130, {});
	if (auto mapping = U::cast<IMidiMapping>(m_controller))
	{
		for (std::size_t bus = 0; bus < m_eventInputs.size(); ++bus)
		{
			for (int16 channel = 0; channel < m_eventInputs[bus]; ++channel)
			{
				for (int16 number = 0; number < 130; ++number)
				{
					ParamID id = 0;
					if (mapping->getMidiControllerAssignment(static_cast<int32>(bus), channel, number, id) != kResultOk) { continue; }
					const auto found = m_parameterIndices.find(id);
					if (found == m_parameterIndices.end() || (m_parameters[found->second].flags & ParameterInfo::kIsReadOnly))
					{ throw std::runtime_error("Invalid VST3 MIDI parameter assignment"); }
					m_midiAssignments[(bus * 16 + channel) * 130 + number] = {id, true};
				}
			}
		}
	}
}

void Vst3Instance::serviceControl(bool applyRestart)
{
	if (!m_handler) { return; }
	const auto reported = m_handler->takeRestart();
	m_restartFlags |= reported; m_pendingRestart |= reported;
	if (!applyRestart) { return; }
	const auto flags = m_pendingRestart; m_pendingRestart = 0;
	if (flags & kReloadComponent)
	{
		// Fully release the view/controller/processor/factory/module before opening
		// the same exact CID. Save opaque state while the old processor still exists.
		const auto saved = state();
		const auto path = m_path; const auto cid = m_cid;
		const auto handler = m_externalHandler;
		const auto context = m_context;
		const auto frames = m_maxFrames;
		const bool offline = m_processData.processMode == kOffline;
		const bool editor = m_editor != nullptr;
		const bool visible = editor && IsWindowVisible(m_editor->window());
		const auto reported = m_restartFlags;
		auto feedback = std::move(m_feedback); auto midi = std::move(m_midiFeedback);
		const auto restartEvents = m_restartEvents; const auto restartEventCount = m_restartEventCount;
		close(); open(path, cid, handler);
		restoreState(saved); setup(context.sampleRate, frames, offline); m_context = context;
		m_restartFlags |= reported;
		m_restartEvents = restartEvents; m_restartEventCount = restartEventCount;
		m_feedback = std::move(feedback); m_midiFeedback = std::move(midi);
		if (editor) { showEditor(); if (!visible) { hideEditor(); } }
		return;
	}
	if ((flags & kIoChanged) && m_active)
	{
		const auto context = m_context; const auto frames = m_maxFrames;
		const bool offline = m_processData.processMode == kOffline;
		require(m_processor->setProcessing(false), "Stop for I/O change"); m_processing = false;
		require(m_component->setActive(false), "Deactivate for I/O change"); m_active = false;
		setup(context.sampleRate, frames, offline); m_context = context;
	}
	if (flags & kParamTitlesChanged) { refreshParameters(); }
	if ((flags & kParamValuesChanged) && m_controller)
	{
		for (std::size_t index = 0; index < m_parameters.size(); ++index)
		{
			const auto value = m_controller->getParamNormalized(m_parameters[index].id);
			if (!std::isfinite(value) || value < 0 || value > 1) { throw std::runtime_error("Invalid refreshed VST3 parameter value"); }
			m_controllerParameters[index].dirty = false;
			if (!(m_parameters[index].flags & ParameterInfo::kIsReadOnly)) { m_pendingParameters[index] = {value, true}; }
		}
	}
	if ((flags & kLatencyChanged) && !(flags & kIoChanged) && m_active)
	{
		require(m_processor->setProcessing(false), "Stop for latency change"); m_processing = false;
		require(m_component->setActive(false), "Deactivate for latency change"); m_active = false;
		require(m_component->setActive(true), "Reactivate for latency change"); m_active = true;
		m_latency = m_processor->getLatencySamples();
		require(m_processor->setProcessing(true), "Resume after latency change"); m_processing = true;
	}
	if (flags & kMidiCCAssignmentChanged) { refreshMidiAssignments(); }
}

void Vst3Instance::refreshParameters()
{
	if (!m_controller) { return; }
	const auto count = m_controller->getParameterCount();
	if (count < 0 || count > 65536) { throw std::runtime_error("Invalid refreshed VST3 parameter count"); }
	std::vector<ParameterInfo> parameters; parameters.reserve(count);
	std::unordered_map<ParamID, std::size_t> indices;
	std::vector<PendingParameter> pending(count), controller(count);
	for (int32 index = 0; index < count; ++index)
	{
		ParameterInfo info{}; require(m_controller->getParameterInfo(index, info), "Refresh parameter metadata");
		if (!indices.emplace(info.id, index).second || !std::isfinite(info.defaultNormalizedValue) ||
			info.defaultNormalizedValue < 0 || info.defaultNormalizedValue > 1 || info.stepCount < 0)
		{ throw std::runtime_error("Invalid refreshed VST3 parameter metadata"); }
		parameters.push_back(info);
		if (const auto old = m_parameterIndices.find(info.id); old != m_parameterIndices.end())
		{
			pending[index] = m_pendingParameters[old->second]; controller[index] = m_controllerParameters[old->second];
			if (info.flags & ParameterInfo::kIsReadOnly) { pending[index].dirty = false; }
		}
	}
	m_parameters = std::move(parameters); m_parameterIndices = std::move(indices);
	m_pendingParameters = std::move(pending); m_controllerParameters = std::move(controller);
	refreshMidiAssignments();
}

std::vector<std::uint8_t> Vst3Instance::metadata()
{
	if (!collectEdits()) { throw std::runtime_error("VST3 metadata callback barrier failed"); }
	synchronizeController();

	Vst3Metadata info{inputChannels(), outputChannels(), static_cast<std::uint32_t>(m_sampleSize), m_latency, {}};
	info.hasEditor = m_hasEditor;
	info.buses = m_inputs.descriptors;
	info.buses.insert(info.buses.end(), m_outputs.descriptors.begin(), m_outputs.descriptors.end());
	info.buses.insert(info.buses.end(), m_eventBuses.begin(), m_eventBuses.end());
	// Titles/type/flags may change without a channel-layout restart. Refresh
	// them on the paused owner, never by querying native code on DAW audio.
	for (auto& bus : info.buses)
	{
		BusInfo current{};
		require(m_component->getBusInfo(static_cast<MediaType>(bus.media), static_cast<BusDirection>(bus.direction),
			static_cast<int32>(bus.index), current), "Refresh bus metadata");
		if (current.mediaType != static_cast<MediaType>(bus.media) || current.direction != static_cast<BusDirection>(bus.direction) ||
			current.channelCount < 0 || static_cast<std::uint32_t>(current.channelCount) != bus.channels)
		{ throw std::runtime_error("VST3 bus changed without prepared channel layout"); }
		bus.type = static_cast<std::uint32_t>(current.busType); bus.flags = current.flags; bus.name = utf8(current.name);
		if (bus.media == 0)
		{
			SpeakerArrangement arrangement = 0;
			require(m_processor->getBusArrangement(static_cast<BusDirection>(bus.direction), static_cast<int32>(bus.index), arrangement),
				"Refresh bus arrangement");
			bus.arrangement = arrangement;
		}
	}

	for (const auto& parameter : m_parameters)
	{
		info.parameters.push_back({parameter.id, static_cast<std::uint32_t>(parameter.flags), static_cast<std::uint32_t>(parameter.stepCount),
			parameter.unitId, m_controller->getParamNormalized(parameter.id), parameter.defaultNormalizedValue,
			utf8(parameter.title), utf8(parameter.shortTitle), utf8(parameter.units)});
	}
	std::vector<std::uint8_t> bytes;
	if (!encodeVst3Metadata(info, bytes)) { throw std::runtime_error("Invalid VST3 metadata envelope"); }
	return bytes;
}

bool Vst3Instance::process(std::uint32_t frames, std::span<const float> input, std::span<float> output,
	std::span<const std::uint8_t> events, std::uint64_t sequence, bool collectOutput)
{
	m_blockMidiFeedback.clear();
	auto& midiFeedback = collectOutput ? m_blockMidiFeedback : m_midiFeedback;
	std::fill(output.begin(), output.end(), 0.0f);
	if (!m_processing || frames > m_maxFrames || input.size() != static_cast<std::size_t>(frames) * m_inputs.channels ||
		output.size() != static_cast<std::size_t>(frames) * m_outputs.channels) { return false; }
	// Validate the whole wire packet before changing queues or transport state.
	if (!decodeVst3Events(events, frames, [](const auto&) { return true; })) { return false; }
	if (!events.empty() && get(events, 0, 4) == 2)
	{
		Vst3Transport transport; if (!decodeVst3Transport(events, transport)) { return false; }
		m_context.projectTimeSamples = transport.samples; m_context.continousTimeSamples = transport.continuous;
		m_context.tempo = transport.tempo; m_context.projectTimeMusic = transport.music; m_context.barPositionMusic = transport.bar;
		m_context.cycleStartMusic = transport.cycleStart; m_context.cycleEndMusic = transport.cycleEnd;
		m_context.timeSigNumerator = static_cast<int32>(transport.numerator); m_context.timeSigDenominator = static_cast<int32>(transport.denominator);
		m_context.state &= ~(ProcessContext::kPlaying | ProcessContext::kRecording | ProcessContext::kCycleActive);
		if (transport.flags & 1) { m_context.state |= ProcessContext::kPlaying; }
		if (transport.flags & 2) { m_context.state |= ProcessContext::kRecording; }
		if (transport.flags & 4) { m_context.state |= ProcessContext::kCycleActive; }
		m_context.state |= ProcessContext::kCycleValid;
	}
	if (m_context.projectTimeSamples > std::numeric_limits<int64>::max() - frames ||
		m_context.continousTimeSamples > std::numeric_limits<int64>::max() - frames) { return false; }
	if (!collectEdits()) { return false; }
	m_inputChanges->reset(static_cast<int32>(std::max(1u, frames))); m_outputChanges->reset(static_cast<int32>(std::max(1u, frames)));
	m_inputEvents.reset(static_cast<int32>(frames)); m_outputEvents.reset(static_cast<int32>(frames));
	for (std::size_t index = 0; index < m_pendingParameters.size(); ++index)
	{
		auto& pending = m_pendingParameters[index];
		if (!pending.dirty) { continue; }
		int32 queueIndex = -1, pointIndex = -1;
		auto* queue = m_inputChanges->addParameterData(m_parameters[index].id, queueIndex);
		if (!queue || queue->addPoint(0, pending.value, pointIndex) != kResultOk) { return false; }
		pending.dirty = false;
	}
	const auto applyEvent = [&](const Vst3BlockEvent& wire)
	{
		if (wire.type == 0)
		{
			const auto found = m_parameterIndices.find(wire.id);
			if (found == m_parameterIndices.end() || (m_parameters[found->second].flags & ParameterInfo::kIsReadOnly)) { return false; }
			int32 queueIndex = -1, pointIndex = -1;
			auto* queue = m_inputChanges->addParameterData(wire.id, queueIndex);
			return queue && queue->addPoint(static_cast<int32>(wire.offset), wire.value, pointIndex) == kResultOk;
		}
		if (wire.bus >= m_eventInputs.size() || wire.channel >= static_cast<std::uint32_t>(m_eventInputs[wire.bus])) { return false; }
		if (wire.type == 4)
		{
			const auto& assignment = m_midiAssignments[(wire.bus * 16 + wire.channel) * 130 + wire.id];
			if (!assignment.assigned) { return true; }
			int32 queueIndex = -1, pointIndex = -1;
			auto* queue = m_inputChanges->addParameterData(assignment.id, queueIndex);
			return queue && queue->addPoint(static_cast<int32>(wire.offset), wire.value, pointIndex) == kResultOk;
		}
		Event event{}; event.busIndex = static_cast<int32>(wire.bus); event.sampleOffset = static_cast<int32>(wire.offset);
		event.ppqPosition = m_context.projectTimeMusic + wire.offset * m_context.tempo / (60 * m_context.sampleRate);
		const auto channel = static_cast<int16>(wire.channel), pitch = static_cast<int16>(wire.id);
		if (wire.type == 1) { event.type = Event::kNoteOnEvent; event.noteOn = {channel, pitch, 0, static_cast<float>(wire.value), 0, -1}; }
		else if (wire.type == 2) { event.type = Event::kNoteOffEvent; event.noteOff = {channel, pitch, static_cast<float>(wire.value), -1, 0}; }
		else { event.type = Event::kPolyPressureEvent; event.polyPressure = {channel, pitch, static_cast<float>(wire.value), -1}; }
		return m_inputEvents.addEvent(event) == kResultOk;
	};
	// Missed blocks cannot retain sample accuracy, so replay at the next safe
	// block boundary, in arrival order and before newly arriving events.
	for (std::size_t index = 0; index < m_restartEventCount; ++index)
	{
		const auto& event = m_restartEvents[index];
		if ((frames || event.type == 0) && !applyEvent(event)) { return false; }
	}
	if (!decodeVst3Events(events, frames, applyEvent)) { return false; }
	m_context.systemTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	m_context.state |= ProcessContext::kSystemTimeValid;
	for (std::uint32_t channel = 0; channel < m_inputs.channels; ++channel)
	{
		for (std::uint32_t frame = 0; frame < frames; ++frame)
		{
			const auto sample = input[static_cast<std::size_t>(frame) * m_inputs.channels + channel];
			if (m_sampleSize == kSample32) { m_inputs.pointers32[channel][frame] = sample; }
			else { m_inputs.pointers64[channel][frame] = sample; }
		}
	}
	for (auto& bus : m_inputs.buses) { bus.silenceFlags = 0; }
	for (auto& bus : m_outputs.buses)
	{
		bus.silenceFlags = 0;
		for (int32 channel = 0; channel < bus.numChannels; ++channel)
		{
			if (m_sampleSize == kSample32) { std::fill_n(bus.channelBuffers32[channel], frames, 0.0f); }
			else { std::fill_n(bus.channelBuffers64[channel], frames, 0.0); }
		}
	}
	m_processData.numSamples = static_cast<int32>(frames);
	if (m_processor->process(m_processData) != kResultOk || m_inputChanges->failed() || m_outputChanges->failed() || m_outputEvents.failed()) { return false; }
	for (int32 index = 0; index < m_outputEvents.getEventCount(); ++index)
	{
		Event event{}; if (m_outputEvents.getEvent(index, event) != kResultOk || midiFeedback.size() >= 512) { return false; }
		Vst3BlockEvent wire; wire.offset = event.sampleOffset; wire.bus = event.busIndex;
		if (event.type == Event::kNoteOnEvent) { wire.type = 1; wire.channel = event.noteOn.channel; wire.id = event.noteOn.pitch; wire.value = event.noteOn.velocity; }
		else if (event.type == Event::kNoteOffEvent) { wire.type = 2; wire.channel = event.noteOff.channel; wire.id = event.noteOff.pitch; wire.value = event.noteOff.velocity; }
		else { wire.type = 3; wire.channel = event.polyPressure.channel; wire.id = event.polyPressure.pitch; wire.value = event.polyPressure.pressure; }
		if (!validVst3Event(wire, frames)) { return false; }
		midiFeedback.push_back({sequence, frames, wire});
	}
	// Remember the final point of each input automation curve. Controller
	// setters belong to subsequent control work, never the native process call.
	for (int32 index = 0; index < m_inputChanges->getParameterCount(); ++index)
	{
		auto* queue = m_inputChanges->getParameterData(index);
		const auto found = m_parameterIndices.find(queue->getParameterId());
		if (found == m_parameterIndices.end()) { return false; }
		int32 offset = 0; double value = 0;
		if (queue->getPointCount() && queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
		{ m_controllerParameters[found->second] = {value, true}; }
	}
	for (int32 index = 0; index < m_outputChanges->getParameterCount(); ++index)
	{
		auto* queue = m_outputChanges->getParameterData(index);
		if (!queue || !m_parameterIndices.contains(queue->getParameterId())) { return false; }
		for (int32 point = 0; point < queue->getPointCount(); ++point)
		{
			int32 offset = 0; double value = 0;
				if (queue->getPoint(point, offset, value) != kResultOk || (frames && m_feedback.size() == RealtimeMidiQueue::Capacity)) { return false; }
				if (frames) { m_feedback.push_back({3, queue->getParameterId(), value, static_cast<std::uint32_t>(offset), frames, sequence}); }
			m_controllerParameters[m_parameterIndices.at(queue->getParameterId())] = {value, true};
		}
	}
	std::uint32_t flatChannel = 0;
	for (const auto& bus : m_outputs.buses)
	{
		for (int32 channel = 0; channel < bus.numChannels; ++channel, ++flatChannel)
		{
			if (bus.silenceFlags & (std::uint64_t{1} << channel)) { continue; }
			for (std::uint32_t frame = 0; frame < frames; ++frame)
			{
				const auto sample = m_sampleSize == kSample32 ? bus.channelBuffers32[channel][frame] : bus.channelBuffers64[channel][frame];
				if (!std::isfinite(sample) || std::abs(sample) > std::numeric_limits<float>::max())
				{ std::fill(output.begin(), output.end(), 0.0f); return false; }
				output[static_cast<std::size_t>(frame) * m_outputs.channels + flatChannel] = static_cast<float>(sample);
			}
		}
	}
	if (frames) { m_restartEventCount = 0; }
	else { discardRestartParameters(); }
	m_context.continousTimeSamples += frames;
	if (m_context.state & ProcessContext::kPlaying)
	{
		m_context.projectTimeSamples += frames;
		m_context.projectTimeMusic += frames * m_context.tempo / (60 * m_context.sampleRate);
		const auto barLength = m_context.timeSigNumerator * 4.0 / m_context.timeSigDenominator;
		m_context.barPositionMusic = std::floor(m_context.projectTimeMusic / barLength) * barLength;
	}
	return true;
}

bool Vst3Instance::deferRestartEvents(std::span<const std::uint8_t> events, std::uint32_t frames)
{
	std::size_t count = 0;
	// Validate the entire packet and capacity before changing the retained queue.
	if (frames > m_maxFrames || !decodeVst3Events(events, frames, [&](const Vst3BlockEvent& event) {
		if (++count > m_restartEvents.size() - m_restartEventCount) { return false; }
		if (event.type == 0)
		{
			const auto found = m_parameterIndices.find(event.id);
			return found != m_parameterIndices.end() && !(m_parameters[found->second].flags & ParameterInfo::kIsReadOnly);
		}
		return event.bus < m_eventInputs.size() && event.channel < static_cast<std::uint32_t>(m_eventInputs[event.bus]);
	})) { return false; }
	return decodeVst3Events(events, frames, [&](Vst3BlockEvent event) {
		event.offset = 0; m_restartEvents[m_restartEventCount++] = event; return true;
	});
}

void Vst3Instance::discardRestartParameters(std::optional<ParamID> id)
{
	std::size_t kept = 0;
	for (std::size_t index = 0; index < m_restartEventCount; ++index)
	{
		const auto& event = m_restartEvents[index];
		if (event.type != 0 || (id && event.id != *id)) { m_restartEvents[kept++] = event; }
	}
	m_restartEventCount = kept;
}

std::vector<std::uint8_t> Vst3Instance::state(std::span<const std::uint8_t> queuedParameters)
{
	if (!m_componentInitialized) { throw std::logic_error("VST3 instance is not initialized"); }
	if (!queuedParameters.empty())
	{
		if (queuedParameters.size() < 8 || get(queuedParameters, 0, 4) != 1 || !decodeVst3Events(queuedParameters, m_maxFrames, [&](const auto& event) {
			const auto found = m_parameterIndices.find(event.id);
			return event.type == 0 && found != m_parameterIndices.end() &&
				!(m_parameters[found->second].flags & ParameterInfo::kIsReadOnly);
		})) { throw std::runtime_error("Invalid VST3 state parameter packet"); }
		Vst3ComponentHandler::HostSetter hostSetter(m_handler);
		decodeVst3Events(queuedParameters, m_maxFrames, [&](const auto& event) {
			const auto index = m_parameterIndices.at(event.id);
			discardRestartParameters(event.id);
			require(m_controller->setParamNormalized(event.id, event.value), "Set state barrier parameter");
			m_pendingParameters[index] = {event.value, true}; m_controllerParameters[index].dirty = false;
			return true;
		});
	}
	// State commands run after the helper audio barrier. Flush pending edits in
	// a zero-sample block so saving never captures the processor's stale value.
	if (m_processing && !process(0, {}, {})) { throw std::runtime_error("VST3 parameter state barrier failed"); }
	synchronizeController();
	auto component = owned(new Vst3StateStream);
	require(m_component->getState(component), "Get component state");
	if (component->failed()) { throw std::runtime_error("Component state exceeded stream limits"); }
	auto controller = owned(new Vst3StateStream);
	bool hasController = false;
	if (m_controller)
	{
		const auto result = m_controller->getState(controller);
		hasController = result == kResultOk;
		if (!hasController && result != kNotImplemented && result != kResultFalse) { require(result, "Get controller state"); }
		if (controller->failed()) { throw std::runtime_error("Controller state exceeded stream limits"); }
	}
	const auto controllerSize = hasController ? controller->bytes().size() : 0;
	const auto total = 16 + component->bytes().size() + controllerSize;
	if (total > MaxControlBytes) { throw std::runtime_error("VST3 state envelope exceeds IPC capacity"); }
	std::vector<std::uint8_t> bytes(total);
	put(bytes, 0, 1, 4); put(bytes, 4, component->bytes().size(), 4); put(bytes, 8, controllerSize, 4);
	put(bytes, 12, hasController ? 1 : 0, 4);
	std::copy(component->bytes().begin(), component->bytes().end(), bytes.begin() + 16);
	if (hasController) { std::copy(controller->bytes().begin(), controller->bytes().end(), bytes.begin() + 16 + component->bytes().size()); }
	return bytes;
}

void Vst3Instance::restoreState(std::span<const std::uint8_t> bytes)
{
	if (!m_componentInitialized || bytes.size() < 16 || bytes.size() > MaxControlBytes || get(bytes, 0, 4) != 1 ||
		get(bytes, 12, 4) > 1) { throw std::runtime_error("Invalid VST3 state envelope"); }
	const auto componentSize = get(bytes, 4, 4), controllerSize = get(bytes, 8, 4);
	const bool hasController = get(bytes, 12, 4) != 0;
	if (componentSize + controllerSize != bytes.size() - 16 || (!hasController && controllerSize) || (hasController && !m_controller))
	{ throw std::runtime_error("Invalid VST3 state envelope lengths"); }
	if (!collectEdits()) { throw std::runtime_error("VST3 state callback barrier failed"); }
	for (auto& parameter : m_pendingParameters) { parameter.dirty = false; }
	for (auto& parameter : m_controllerParameters) { parameter.dirty = false; }
	discardRestartParameters();
	Vst3ComponentHandler::HostSetter stateSetter(m_handler);
	auto component = owned(new Vst3StateStream({bytes.begin() + 16, bytes.begin() + 16 + componentSize}, false));
	require(m_component->setState(component), "Set component state");
	if (component->failed()) { throw std::runtime_error("Invalid component state stream access"); }
	if (m_controller)
	{
		require(component->seek(0, IBStream::kIBSeekSet, nullptr), "Rewind component state");
		require(m_controller->setComponentState(component), "Set controller component state");
		if (component->failed()) { throw std::runtime_error("Invalid controller component state stream access"); }
		if (hasController)
		{
			auto controller = owned(new Vst3StateStream({bytes.begin() + 16 + componentSize, bytes.end()}, false));
			require(m_controller->setState(controller), "Set controller state");
			if (controller->failed()) { throw std::runtime_error("Invalid controller state stream access"); }
		}
	}
}

bool Vst3Instance::collectEdits()
{
	if (!m_handler) { return true; }
	if (m_handler->failed()) { return false; }
	RealtimeMidiQueue::Event edit{};
	for (unsigned count = 0; count < RealtimeMidiQueue::Capacity && m_handler->pop(edit); ++count)
	{
		const auto found = m_parameterIndices.find(edit.values[1]);
		if (found == m_parameterIndices.end() || m_feedback.size() == RealtimeMidiQueue::Capacity) { return false; }
		if (edit.values[0] == 1)
		{
			const auto value = std::bit_cast<double>(std::uint64_t{edit.values[2]} | (std::uint64_t{edit.values[3]} << 32));
			m_pendingParameters[found->second] = {value, true};
			// A newer GUI edit wins over older unreported audio automation.
			m_controllerParameters[found->second].dirty = false;
		}
		const auto value = std::bit_cast<double>(std::uint64_t{edit.values[2]} | (std::uint64_t{edit.values[3]} << 32));
		m_feedback.push_back({edit.values[0], edit.values[1], value, 0, 0, 0});
	}
	return true;
}

std::vector<std::uint8_t> Vst3Instance::parameterControl(std::span<const std::uint8_t> command)
{
	// Reconfiguration runs behind the session's audio pause/drain barrier.
	if (command.size() == 20 && get(command, 0, 4) == 2)
	{
		Vst3Setup configuration;
		if (!decodeVst3Setup(command, configuration)) { throw std::runtime_error("Invalid VST3 reconfiguration"); }
		if (!collectEdits()) { throw std::runtime_error("VST3 reconfiguration callback barrier failed"); }
		synchronizeController();
		if (m_processing) { require(m_processor->setProcessing(false), "Stop for setup change"); m_processing = false; }
		if (m_active) { require(m_component->setActive(false), "Deactivate for setup change"); m_active = false; }
		setup(configuration.sampleRate, configuration.maxFrames, configuration.offline);
		return metadata();
	}
	// Four-byte selector 1 requests a refreshed SDK-free metadata snapshot.
	if (command.size() == 4 && get(command, 0, 4) == 1) { return metadata(); }
	if (!m_controller || (command.size() != 0 && command.size() != 12)) { throw std::runtime_error("Invalid VST3 parameter control"); }
	if (!collectEdits()) { throw std::runtime_error("VST3 parameter callback overflow or invalid ParamID"); }
	synchronizeController();
	if (!command.empty())
	{
		const auto id = static_cast<ParamID>(get(command, 0, 4));
		const auto value = std::bit_cast<double>(get(command, 4, 8));
		const auto found = m_parameterIndices.find(id);
		if (found == m_parameterIndices.end() || !std::isfinite(value) || value < 0 || value > 1 ||
			(m_parameters[found->second].flags & ParameterInfo::kIsReadOnly)) { throw std::runtime_error("Invalid VST3 parameter setter"); }
		Vst3ComponentHandler::HostSetter hostSetter(m_handler);
		require(m_controller->setParamNormalized(id, value), "Set controller parameter");
		discardRestartParameters(id);
		m_pendingParameters[found->second] = {value, true};
		m_controllerParameters[found->second].dirty = false;
	}
	// Serialize GUI gestures separately from DSP output and its audio block ID.
	std::vector<std::uint8_t> reply;
	if (!encodeVst3Feedback(m_feedback, m_restartFlags, reply))
	{ throw std::runtime_error("Invalid VST3 parameter feedback"); }
	m_feedback.clear(); m_restartFlags = 0; return reply;
}

HWND Vst3Instance::showEditor()
{
	if (!m_controller) { throw std::runtime_error("VST3 instance has no editor controller"); }
	if (!collectEdits()) { throw std::runtime_error("VST3 editor callback barrier failed"); }
	synchronizeController();
	if (!m_editor) { m_editor = owned(new Vst3Editor); m_editor->open(*m_controller); }
	else { m_editor->show(true); }
	return m_editor->window();
}
void Vst3Instance::hideEditor() { if (m_editor) { m_editor->show(false); } }

bool Vst3Instance::blockMidiOutput(std::span<std::uint8_t> storage, std::uint32_t& written) noexcept
{
	if (!encodeVst3OutputEvents(m_blockMidiFeedback, storage, written)) { return false; }
	m_blockMidiFeedback.clear(); return true;
}

std::vector<std::uint8_t> Vst3Instance::midiOutput()
{
	std::vector<std::uint8_t> reply;
	if (!encodeVst3OutputEvents(m_midiFeedback, reply)) { throw std::runtime_error("Invalid VST3 MIDI output"); }
	m_midiFeedback.clear(); return reply;
}

void Vst3Instance::synchronizeController()
{
	if (!m_controller) { return; }
	Vst3ComponentHandler::HostSetter hostSetter(m_handler);
	for (std::size_t index = 0; index < m_controllerParameters.size(); ++index)
	{
		auto& parameter = m_controllerParameters[index];
		if (!parameter.dirty) { continue; }
		require(m_controller->setParamNormalized(m_parameters[index].id, parameter.value), "Synchronize controller automation");
		parameter.dirty = false;
	}
}
} // namespace lmms::vsthost
