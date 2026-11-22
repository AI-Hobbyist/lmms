#ifndef LMMS_VSTHOST_VST_NODE_PORTS_H
#define LMMS_VSTHOST_VST_NODE_PORTS_H

#include <QCoreApplication>
#include <QString>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "vsthost/Vst3Metadata.h"
#include "LmmsTypes.h"

namespace lmms::vsthost {

enum class NodePortMedia { Audio, Midi };
enum class NodePortDirection { Input, Output };

struct VstNodeMidiEvent
{
	std::uint32_t bus;
	std::uint32_t offset;
	std::uint8_t status;
	std::uint8_t first;
	std::uint8_t second;
};

using VstNodeMidiReceiver = void (*)(void*, const VstNodeMidiEvent&) noexcept;
using VstNodeAudioReceiver = std::function<void(std::span<const float>, unsigned)>;

struct VstNodeRouting
{
	std::function<void(std::span<float>, unsigned)> audioInput;
	std::function<std::size_t(std::span<VstNodeMidiEvent>)> midiInput;
	std::function<void(const VstNodeMidiEvent&)> midiOutput;
};

struct VstNodePort
{
	NodePortMedia media;
	NodePortDirection direction;
	std::uint32_t bus;
	std::uint32_t channel;
	std::uint32_t bufferIndex;
	QString name;
	bool available;
	QString unavailableReason;
	bool operator==(const VstNodePort&) const = default;
	std::uint32_t audioKey() const noexcept { return (bus << 16) | channel; }

	QString key() const
	{
		return QStringLiteral("%1-%2:%3:%4")
			.arg(media == NodePortMedia::Audio ? QStringLiteral("audio") : QStringLiteral("midi"),
				direction == NodePortDirection::Input ? QStringLiteral("in") : QStringLiteral("out"))
			.arg(bus)
			.arg(channel);
	}
};

class VstNodeProcessor
{
public:
	virtual ~VstNodeProcessor() = default;
	virtual std::vector<VstNodePort> nodePorts() = 0;
	virtual unsigned nodeAudioIndex(std::uint32_t key, NodePortDirection direction) const noexcept = 0;
	virtual void setNodeAudioReceiver(VstNodeAudioReceiver receiver) = 0;
	virtual void setNodeRouting(VstNodeRouting routing) = 0;
	virtual bool hasNodeInput() const noexcept = 0;
	virtual bool processNodeBlock(f_cnt_t frames, std::span<const float> input, std::span<float> output,
		std::span<const VstNodeMidiEvent> events, VstNodeMidiReceiver receiver, void* context) = 0;
};

class VstNodeOwner
{
public:
	virtual ~VstNodeOwner() = default;
	virtual VstNodeProcessor* vstNodeProcessor() noexcept = 0;
};

inline std::vector<VstNodePort> vst3NodePorts(const Vst3Metadata& metadata)
{
	std::vector<VstNodePort> ports;
	for (const auto& bus : metadata.buses)
	{
		const auto direction = bus.direction == 0 ? NodePortDirection::Input : NodePortDirection::Output;
		const auto name = QString::fromStdString(bus.name);
		if (bus.media == 1)
		{
			// The event bus carries channel numbers in the events themselves.
			const bool usable = bus.active && bus.channels != 0 && bus.index < 32 && bus.channels <= 16;
			QString reason;
			if (!usable)
			{
				reason = !bus.active
					? QCoreApplication::translate("VstNodePorts", "The plugin did not activate this event bus.")
					: QCoreApplication::translate("VstNodePorts", "This event bus is outside the supported MIDI range.");
			}
			ports.push_back({NodePortMedia::Midi, direction, bus.index, 0, bus.index, name, usable, reason});
			continue;
		}
		const bool usable = bus.active && !(bus.flags & 2);
		QString reason;
		if (!usable)
		{
			reason = !bus.active
				? QCoreApplication::translate("VstNodePorts", "The plugin did not activate this audio bus.")
				: QCoreApplication::translate("VstNodePorts", "The plugin declared this bus as control voltage.");
		}
		for (std::uint32_t channel = 0; channel < bus.channels; ++channel)
		{
			ports.push_back({NodePortMedia::Audio, direction, bus.index, channel, bus.channelOffset + channel,
				bus.channels == 1 ? name : QStringLiteral("%1 %2").arg(name).arg(channel + 1),
				usable, reason});
		}
	}
	return ports;
}

} // namespace lmms::vsthost

#endif
