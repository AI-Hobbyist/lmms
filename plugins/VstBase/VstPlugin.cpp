/*
 * VstPlugin.cpp - implementation of VstPlugin class
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "VstPlugin.h"

#include "communication.h"

#include <QtEndian>
#include <QDebug>
#include <QDir>
#include <QDomElement>
#include <QFileInfo>
#include <QLocale>
#include <QTemporaryFile>
#include <QSaveFile>
#include <QTimerEvent>
#include <QScopedValueRollback>
#include <QPointer>
#include <QTimer>
#include <bit>
#include <charconv>
#include <cmath>

#if defined(LMMS_BUILD_LINUX) && (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#	include <QX11Info>
#	include <X11EmbedContainer.h>
#endif

#include <QWindow>


#ifdef LMMS_BUILD_WIN32
#	include <windows.h>
#	include <QLayout>
#	include "vsthost/Vst2Scanner.h"
#	include "vsthost/Vst3HostProxy.h"
#	include "vsthost/Vst3Ports.h"
#	include "vsthost/PluginCatalog.h"
#	include "vsthost/Vst3Selection.h"
#endif

#include "AudioEngine.h"
#include "AudioFileDevice.h"
#include "ConfigManager.h"
#include "FileDialog.h"
#include "FontHelper.h"
#include "GuiApplication.h"
#include "LocaleHelper.h"
#include "MainWindow.h"
#include "PathUtil.h"
#include "SimpleTextFloat.h"
#include "Song.h"
#include "MidiEvent.h"
#include "SampleFrame.h"

#ifdef LMMS_BUILD_LINUX
#	include <X11/Xlib.h>
#endif

namespace PE
{
// Utilities for reading PE file machine type
// See specification at https://msdn.microsoft.com/library/windows/desktop/ms680547(v=vs.85).aspx

// Work around name conflict
#ifdef i386
#	undef i386
#endif

enum class MachineType : uint16_t
{
	unknown = 0x0,
	amd64 = 0x8664,
	i386 = 0x14c,
};

class FileInfo
{
public:
	FileInfo(QString filePath)
		: m_file(filePath)
		{
			if (!m_file.open(QFile::ReadOnly) || m_file.size() < 64) { throw std::runtime_error("Truncated PE file"); }
		m_map = m_file.map(0, m_file.size());
		if (m_map == nullptr) {
			throw std::runtime_error("Cannot map file");
		}
	}
	~FileInfo()
	{
		m_file.unmap(m_map);
	}

	MachineType machineType()
	{
		if (m_map[0] != 'M' || m_map[1] != 'Z') { throw std::runtime_error("Invalid DOS signature"); }
		const auto peOffset = qFromLittleEndian<std::uint32_t>(m_map + 0x3C);
		if (peOffset > static_cast<std::uint64_t>(m_file.size() - 6)) { throw std::runtime_error("Invalid PE header offset"); }
		uchar* peSignature = m_map + peOffset;
		if (memcmp(peSignature, "PE\0\0", 4)) {
			throw std::runtime_error("Invalid PE file");
		}
		uchar * coffHeader = peSignature + 4;
		uint16_t machineType = qFromLittleEndian<std::uint16_t>(coffHeader);
		return static_cast<MachineType>(machineType);
	}

private:
	QFile m_file;
	uchar* m_map;
};

} // namespace PE

namespace lmms
{

namespace { thread_local VstPlugin* applyingParameterEdit = nullptr; }

enum class ExecutableType
{
	Unknown, Win32, Win64, Linux64,
};

VstPlugin::ScanResult VstPlugin::scanModule(const QString& path, unsigned timeoutMs)
{
	ScanResult result;
#ifdef LMMS_BUILD_WIN32
	const auto absolute = PathUtil::toAbsolute(path);
	QString helper;
	try
	{
		PE::FileInfo info(absolute);
		switch (info.machineType())
		{
		case PE::MachineType::amd64: helper = REMOTE_VST_PLUGIN_FILEPATH_64; break;
		case PE::MachineType::i386: helper = REMOTE_VST_PLUGIN_FILEPATH_32; break;
		default: result.error = tr("Unsupported or invalid VST2 PE module: %1").arg(path); return result;
		}
	}
	catch (const std::runtime_error& error)
	{ result.error = QString::fromUtf8(error.what()); return result; }
	auto executable = QFileInfo(QDir("plugins:"), helper).absoluteFilePath();
	if (const auto* directory = std::getenv("LMMS_PLUGIN_DIR"))
	{ executable = QFileInfo(QDir(directory), helper).absoluteFilePath(); }
	if (!executable.endsWith(".exe", Qt::CaseInsensitive)) { executable += ".exe"; }
	const auto scan = vsthost::scanVst2({executable.toStdWString(), {L"headless"}, timeoutMs},
		absolute.toUtf8().toStdString(), timeoutMs);
	if (scan.error != vsthost::Error::None)
	{
		result.error = tr("VST2 scan failed for %1 (error %2, stage %3, native code %4).")
			.arg(path).arg(static_cast<unsigned>(scan.error)).arg(static_cast<unsigned>(scan.fault.stage))
			.arg(scan.fault.nativeCode, 0, 16);
		return result;
	}
	result.shell = scan.shell;
	for (const auto& entry : scan.entries) { result.entries.push_back({entry.id, QString::fromUtf8(entry.name)}); }
#else
	result.error = tr("VST2 discovery is currently implemented for Windows.");
#endif
	return result;
}

struct VstPlugin::Native
{
#ifdef LMMS_BUILD_WIN32
	vsthost::Vst3HostProxy proxy;
	vsthost::CatalogEntry entry;
	std::vector<std::uint32_t> ids; // Stable model slots, never controller enumeration indices.
	std::vector<float> input, output, dry;
	std::atomic<std::uint32_t> audioLayout{0};
	std::atomic<std::uint32_t> midiInputPort{UINT32_MAX};

	QByteArray lastState;
	QString preservedState;
	bool preserveState = false;
	bool visible = false;
	std::int64_t continuous = 0;
	void applyFeedback(VstPlugin& plugin, const vsthost::Vst3HostProxy::Feedback& feedback)
	{
		for (const auto& edit : feedback.edits)
		{
			const auto found = std::find(ids.begin(), ids.end(), edit.id);
			if (found != ids.end())
			{ plugin.applyParameterEdit(edit.phase == 3 ? 1 : edit.phase, int(found - ids.begin()), float(edit.value)); }
		}
		if (feedback.restart) { plugin.refreshNativeParameters(); }
	}
#endif
};

VstPlugin::VstPlugin(const QString& _plugin, std::uint32_t shellId, const QString& embedMethod, const vsthost::CatalogEntry* selection) :
	RemotePlugin(true),
	m_plugin( PathUtil::toAbsolute(_plugin) ),
	m_shellId(shellId),
	m_pluginWindowID( 0 ),
	m_embedMethod( !embedMethod.isEmpty() ? embedMethod : (gui::getGUI() != nullptr)
			? ConfigManager::inst()->vstEmbedMethod()
			: "headless" ),
	m_version( 0 ),
	m_currentProgram()
{
	setSplittedChannels( true );
#ifdef LMMS_BUILD_WIN32
	if (selection && selection->identity.format == vsthost::Format::Vst3)
	{
			m_native = std::make_unique<Native>(); m_native->entry = *selection;
		m_native->entry.locator.modulePath = PathUtil::toAbsolute(selection->locator.modulePath);
		if (!selection->locator.binaryPath.isEmpty())
		{ m_native->entry.locator.binaryPath = PathUtil::toAbsolute(selection->locator.binaryPath); }
		QString helper = selection->identity.architecture == vsthost::Architecture::X86
			? "32/RemoteVstHost32.exe" : "RemoteVstHost64.exe";
		auto executable = QFileInfo(QDir("plugins:"), helper).absoluteFilePath();
		if (const auto* directory = std::getenv("LMMS_PLUGIN_DIR"))
		{ executable = QFileInfo(QDir(directory), helper).absoluteFilePath(); }
		auto ioDirectory = QFileInfo(executable).dir();
		if (selection->identity.architecture == vsthost::Architecture::X86) { ioDirectory.cdUp(); }
		const vsthost::CatalogIo io(ioDirectory.absoluteFilePath("RemoteCatalogIo.exe"), 15000, {});
		QString selectionError;
		if (!vsthost::validateVst3Selection(io, executable, m_plugin, m_native->entry, selectionError))
		{
			m_failed = true;
			Engine::getSong()->collectError(tr("VST3 selection cannot be loaded: %1").arg(selectionError));
			return;
		}
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		const vsthost::Vst3Create instance{selection->identity.cid,
			double(Engine::audioEngine()->outputSampleRate()), static_cast<std::uint32_t>(frames),
			Engine::audioEngine()->renderOnly() || Engine::getSong()->isExporting(), m_plugin.toUtf8().toStdString()};
		m_failed = !m_native->proxy.open({executable.toStdWString(), {}, 15000, 100}, instance);
		if (m_failed)
		{
			const auto fault = m_native->proxy.fault();
			qWarning() << "VST3 initialization failed" << m_plugin
				<< "error" << static_cast<unsigned>(m_native->proxy.error())
				<< "stage" << static_cast<unsigned>(fault.stage) << "native" << fault.nativeCode;
			return;
		}
		m_name = selection->name.isEmpty() ? QFileInfo(m_plugin).completeBaseName() : selection->name;
		m_vendorString = selection->vendor; m_productString = m_name;
		for (const auto& parameter : m_native->proxy.metadata().parameters) { m_native->ids.push_back(parameter.id); }
		m_parameterCount = static_cast<int>(m_native->ids.size());
		m_native->input.resize(vsthost::AudioQueue::MaxFrames * vsthost::AudioQueue::MaxChannels);
		m_native->output.resize(vsthost::AudioQueue::MaxFrames * vsthost::AudioQueue::MaxChannels);
			m_native->dry.resize(vsthost::AudioQueue::MaxFrames * vsthost::AudioQueue::MaxChannels);
		refreshNativeParameters();
		m_native->proxy.setMetadataPublication([this] { refreshNativeParameters(); });
		connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, &VstPlugin::updateSampleRate);
		m_idleTimer.start(50); connect(&m_idleTimer, &QTimer::timeout, this, &VstPlugin::idleUpdate);
		return;
	}
#endif

	auto pluginType = ExecutableType::Unknown;
#ifdef LMMS_BUILD_LINUX
	QFileInfo fi(m_plugin);
	if (fi.suffix() == "so")
	{
		pluginType = ExecutableType::Linux64;
	}
	else
#endif
	{
		try {
			PE::FileInfo peInfo(m_plugin);
			switch (peInfo.machineType())
			{
			case PE::MachineType::amd64:
				pluginType = ExecutableType::Win64;
				break;
			case PE::MachineType::i386:
				pluginType = ExecutableType::Win32;
				break;
			default:
				qWarning() << "Unknown PE machine type"
					<< QString::number(static_cast<uint16_t>(peInfo.machineType()), 16);
				break;
			}
		} catch (std::runtime_error& e) {
			qCritical() << "Error while determining PE file's machine type: " << e.what();
		}
	}

	switch(pluginType)
	{
	case ExecutableType::Win64:
		tryLoad( REMOTE_VST_PLUGIN_FILEPATH_64 ); // Default: RemoteVstPlugin64
		break;
	case ExecutableType::Win32:
		tryLoad( REMOTE_VST_PLUGIN_FILEPATH_32 ); // Default: 32/RemoteVstPlugin32
		break;
#ifdef LMMS_BUILD_LINUX
	case ExecutableType::Linux64:
		tryLoad( NATIVE_LINUX_REMOTE_VST_PLUGIN_FILEPATH_64 ); // Default: NativeLinuxRemoteVstPlugin32
		break;
#endif
	default:
		m_failed = true;
		return;
	}

	setTempo( Engine::getSong()->getTempo() );

	connect( Engine::getSong(), SIGNAL( tempoChanged( lmms::bpm_t ) ),
			this, SLOT( setTempo( lmms::bpm_t ) ), Qt::DirectConnection );
	connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ),
				this, SLOT( updateSampleRate() ) );

	// Poll native GUI edits without pausing the audio pipeline.
	m_idleTimer.start( 50 );
	connect( &m_idleTimer, SIGNAL( timeout() ),
				this, SLOT( idleUpdate() ) );
}




VstPlugin::~VstPlugin()
{
	for (const auto index : m_parameterGestures)
	{ if (auto model = m_parameterModels.value(index)) { model->restoreJournallingState(); } }
	delete m_pluginWidget;
}

bool VstPlugin::failed() const
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return m_failed || m_native->proxy.error() != vsthost::Error::None; }
#endif
	return RemotePlugin::failed();
}

bool VstPlugin::isRunning()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return m_native->proxy.running(); }
#endif
	return RemotePlugin::isRunning();
}

bool VstPlugin::hasEditor() const
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return !failed() && m_native->proxy.running() && m_native->proxy.metadata().hasEditor; }
#endif
	return m_pluginWindowID != 0;
}

int VstPlugin::isUIVisible()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return m_native->visible ? 1 : 0; }
#endif
	return RemotePlugin::isUIVisible();
}

void VstPlugin::refreshNativeParameters()
{
#ifdef LMMS_BUILD_WIN32
	if (!m_native) { return; }
	const auto& metadata = m_native->proxy.metadata();
	std::uint32_t midiInput = UINT32_MAX;
	vsthost::resolveVst3EventPort(metadata, 0, vsthost::Vst3MidiPorts::Automatic, midiInput);
	m_native->midiInputPort.store(midiInput, std::memory_order_release);
	vsthost::Vst3StereoPort inputPort, outputPort;
	vsthost::resolveVst3Port(metadata, 0, vsthost::Vst3AudioPorts::Automatic, 0, inputPort);
	vsthost::resolveVst3Port(metadata, 1, vsthost::Vst3AudioPorts::Automatic, 0, outputPort);
	m_native->audioLayout.store(metadata.inputs | (metadata.outputs << 8) |
		(vsthost::encodeVst3Port(inputPort) << 16) | (vsthost::encodeVst3Port(outputPort) << 24), std::memory_order_release);
	m_parameterDump.clear(); m_allParameterLabels.resize(m_native->ids.size()); m_allParameterDisplays.resize(m_native->ids.size());
	for (std::size_t index = 0; index < m_native->ids.size(); ++index)
	{
		const auto found = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
			[&](const auto& parameter) { return parameter.id == m_native->ids[index]; });
		if (found == metadata.parameters.end()) { continue; }
		m_parameterDump["param" + QString::number(index)] = QString::number(index) + ':' +
			QString::fromUtf8(found->title).replace(':', ' ') + ':' + QString::number(found->value, 'g', 17);
		m_allParameterLabels[index] = QString::fromUtf8(found->units);
		m_allParameterDisplays[index] = QString::number(found->value, 'g', 6);
	}
#endif
}

bool VstPlugin::process(const SampleFrame* input, SampleFrame* output)
{
	return process(input, output, Engine::audioEngine()->framesPerPeriod());
}

bool VstPlugin::process(const SampleFrame* input, SampleFrame* output, f_cnt_t frames)
{
	return processEffect(input, output, frames, 1.0f, 0.0f);
}

bool VstPlugin::processEffect(const SampleFrame* input, SampleFrame* output, f_cnt_t frames, float wet, float dry)
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		if (!output) { return false; }
		if (!frames || frames > vsthost::AudioQueue::MaxFrames) { zeroSampleFrames(output, frames); return false; }
		const auto offline = Engine::audioEngine()->renderOnly() || Engine::getSong()->isExporting();
		std::vector<vsthost::Vst3OutputEvent> generated; // Populated only by the renderer.
		// Consume unused plugin events with their audio block to keep queues bounded.
		const auto receive = +[](void*, const vsthost::Vst3OutputEvent&,
			const vsthost::Vst3Transport&, std::uint32_t) noexcept { return true; };
		const auto render = [&] {
			const auto layout = m_native->audioLayout.load(std::memory_order_acquire);
			const auto inputs = layout & 255, outputs = (layout >> 8) & 255;
			const auto mapping = layout >> 16;
			const auto inputPort = vsthost::decodeVst3Port(mapping & 255), outputPort = vsthost::decodeVst3Port(mapping >> 8);
				if (inputs > vsthost::AudioQueue::MaxChannels || inputPort.offset > inputs || inputPort.channels > inputs - inputPort.offset) { return false; }
			for (unsigned frame = 0; frame < frames; ++frame)
			{
				const auto left = input ? input[frame][0] : 0.f, right = input ? input[frame][1] : 0.f;
				auto nativeInput = std::span(m_native->input).subspan(frame * inputs, inputs);
				vsthost::writeVst3Port(nativeInput, inputPort, left, right);
				vsthost::writeVst3Port(std::span(m_native->dry).subspan(frame * outputs, outputs), outputPort, left, right);
			}
			const auto* song = Engine::getSong();
			vsthost::Vst3Transport transport;
			transport.flags = (song->isPlaying() || song->isExporting() ? 1 : 0) | (song->isRecording() ? 2 : 0);
			transport.tempo = Engine::getSong()->getTempo();
			transport.numerator = Engine::getSong()->getTimeSigModel().getNumerator();
			transport.denominator = Engine::getSong()->getTimeSigModel().getDenominator();
			transport.samples = song->getFrames();
			transport.music = double(song->getTicks()) / (DefaultTicksPerBar / 4);
			const auto bar = double(transport.numerator) * 4 / transport.denominator;
			transport.bar = std::floor(transport.music / bar) * bar;
			transport.continuous = m_native->continuous;
			const bool success = m_native->proxy.process({static_cast<std::uint32_t>(frames), inputs, outputs},
				std::span(m_native->input).first(std::size_t(frames) * inputs),
				std::span(m_native->output).first(std::size_t(frames) * outputs), transport,
				offline,
				std::span(m_native->dry).first(std::size_t(frames) * outputs), wet, dry, offline ? &generated : nullptr, offline ? nullptr : receive, nullptr);
			for (unsigned frame = 0; frame < frames; ++frame)
			{
				const auto samples = success ? vsthost::readVst3Port(std::span(m_native->output).subspan(frame * outputs, outputs), outputPort) :
					std::array<float, 2>{};
				output[frame] = SampleFrame(samples[0], samples[1]);
			}
			return success;
		};
		const auto success = offline ? render() : m_native->proxy.withAudio(render);
		if (!success)
		{
			zeroSampleFrames(output, frames);
		}
		m_native->continuous += frames;
		return success;
	}
#endif
	// Preserve the VST2 dry/wet path and its existing fixed-period bridge.
	static thread_local auto original = std::array<SampleFrame, MAXIMUM_BUFFER_SIZE>();
	if (!output || frames > MAXIMUM_BUFFER_SIZE) { return false; }
	if (input) { std::copy_n(input, frames, original.begin()); }
	else { std::fill_n(original.begin(), frames, SampleFrame{}); }
	const auto processed = RemotePlugin::process(input, output);
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{ output[frame] = output[frame] * wet + original[frame] * dry; }
	return processed;
}

void VstPlugin::processMidiEvent(const MidiEvent& event, f_cnt_t offset)
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		const auto selection = m_native->midiInputPort.load(std::memory_order_acquire);
		const auto port = static_cast<std::uint32_t>(selection);
		const auto channel = static_cast<std::uint32_t>(event.channel());
		if (port == UINT32_MAX || channel >= (port >> 5)) { return; }
		vsthost::Vst3BlockEvent translated{}; translated.bus = port & 31; translated.offset = offset; translated.channel = channel;
		translated.id = static_cast<std::uint32_t>(event.key()); translated.value = event.velocity() / 127.0;
		switch (event.type())
		{
		case MidiNoteOn: translated.type = event.velocity() ? 1 : 2; break;
		case MidiNoteOff: translated.type = 2; break;
		case MidiKeyPressure: translated.type = 3; break;
		case MidiControlChange: translated.type = 4; translated.id = event.controllerNumber(); translated.value = event.controllerValue() / 127.0; break;
		case MidiChannelPressure: translated.type = 4; translated.id = 128; translated.value = event.channelPressure() / 127.0; break;
		case MidiPitchBend: translated.type = 4; translated.id = 129; translated.value = event.pitchBend() / 16383.0; break;
		default: return;
		}
		m_native->proxy.postEvent(translated); return;
	}
#endif
	RemotePlugin::processMidiEvent(event, offset);
}

QString VstPlugin::parameterStateKey(int index) const
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		if (index < 0 || static_cast<std::size_t>(index) >= m_native->ids.size()) { return {}; }
		return QString("vst3param_%1").arg(m_native->ids[index], 8, 16, QLatin1Char('0'));
	}
#endif
	return QString("param%1").arg(index);
}

void VstPlugin::bindParameterModel(int index, FloatModel* model)
{
	if (m_parameterGestures.remove(index))
	{ if (auto previous = m_parameterModels.value(index)) { previous->restoreJournallingState(); } }
	m_parameterModels[index] = model;
}

void VstPlugin::applyParameterEdit(unsigned phase, int index, float value)
{
	auto model = m_parameterModels.value(index);
	if (phase == 0)
	{
		if (model && !m_parameterGestures.contains(index))
		{ model->addJournalCheckPoint(); model->saveJournallingState(false); m_parameterGestures.insert(index); }
		emit parameterEditBegan(index);
	}
	else if (phase == 1)
	{
		// Only suppress echoes for this proxy on this thread. Linked models for
		// other instances and automation on the audio thread keep sending normally.
		QScopedValueRollback<VstPlugin*> applyingEdit(applyingParameterEdit, this);
		if (model) { model->setValue(value); }
		emit parameterEdited(index, value);
	}
	else
	{
		if (m_parameterGestures.remove(index) && model) { model->restoreJournallingState(); }
		emit parameterEditEnded(index);
	}
}




void VstPlugin::tryLoad( const QString &remoteVstPluginExecutable )
{
#ifdef LMMS_BUILD_WIN32
	initSupervisedVst(remoteVstPluginExecutable, m_embedMethod);
#else
	init( remoteVstPluginExecutable, false, {m_embedMethod} );
#endif

	waitForHostInfoGotten();
	if( failed() )
	{
		return;
	}

	lock();

	VstHostLanguage hlang = VstHostLanguage::English;
	switch( QLocale::system().language() )
	{
		case QLocale::French: hlang = VstHostLanguage::French; break;
		case QLocale::German: hlang = VstHostLanguage::German; break;
		case QLocale::Italian: hlang = VstHostLanguage::Italian; break;
		case QLocale::Japanese: hlang = VstHostLanguage::Japanese; break;
		case QLocale::Korean: hlang = VstHostLanguage::Korean; break;
		case QLocale::Spanish: hlang = VstHostLanguage::Spanish; break;
		default: break;
	}
	sendMessage( message( IdVstSetLanguage ).addInt( static_cast<int>(hlang) ) );
	message load(IdVstLoadPlugin); load.addString(QSTR_TO_STDSTR(m_plugin));
	if (m_shellId) { load.addString(std::to_string(m_shellId)); }
	sendMessage(load);

	waitForInitDone();

	unlock();
}




void VstPlugin::loadSettings( const QDomElement & _this )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		const auto& identity = m_native->entry.identity;
		const QByteArray cid(reinterpret_cast<const char*>(identity.cid.data()), identity.cid.size());
		if (_this.attribute("format") != "vst3" || _this.attribute("classid").toLatin1().toLower() != cid.toHex())
		{ Engine::getSong()->collectError(tr("VST3 state identity does not match the selected class.")); return; }
		if (_this.hasAttribute("vst3state"))
		{
			m_native->preservedState = _this.attribute("vst3state");
			m_native->preserveState = true;
			constexpr qsizetype maximumEncoded = ((vsthost::MaxControlBytes + 2ull) / 3) * 4;
			if (m_native->preservedState.size() > maximumEncoded)
			{ Engine::getSong()->collectError(tr("VST3 project state exceeds the size limit. Original data is preserved.")); return; }
			const auto state = QByteArray::fromBase64(m_native->preservedState.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
			const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(state.constData()), std::size_t(state.size()));
			if (!vsthost::validVst3State(bytes))
			{ Engine::getSong()->collectError(tr("Invalid VST3 project state. Original data is preserved.")); return; }
			loadChunk(state);
			if (m_native->preserveState) { return; }
		}
		m_native->proxy.refreshMetadata();
		return;
	}
#endif
	if( _this.hasAttribute( "program" ) )
	{
		setProgram( _this.attribute( "program" ).toInt() );
	}

	const int num_params = _this.attribute( "numparams" ).toInt();
	// if it exists try to load settings chunk
	if( _this.hasAttribute( "chunk" ) )
	{
		loadChunk( QByteArray::fromBase64(
				_this.attribute( "chunk" ).toUtf8() ) );
	}
	else if( num_params > 0 )
	{
		// no chunk, restore individual parameters
		QMap<QString, QString> dump;
		for( int i = 0; i < num_params; ++i )
		{
			const QString key = "param" +
						QString::number( i );
			dump[key] = _this.attribute( key );
		}
		setParameterDump( dump );
	}
}




void VstPlugin::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		const auto& entry = m_native->entry;
		_this.setAttribute("format", "vst3");
		_this.setAttribute("pluginname", m_name);
		_this.setAttribute("vendor", m_vendorString);
		_this.setAttribute("product", m_productString);
		_this.setAttribute("classid", QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(entry.identity.cid.data()), 16).toHex()));
		_this.setAttribute("architecture", entry.identity.architecture == vsthost::Architecture::X86 ? "32" : "64");
		_this.setAttribute("modulepath", entry.locator.modulePath);
		_this.setAttribute("binarypath", entry.locator.binaryPath);
		_this.setAttribute("version", entry.locator.version);
		_this.setAttribute("fingerprint", QString::fromLatin1(entry.locator.fingerprint.toHex()));
		_this.setAttribute("guivisible", m_native->visible ? 1 : 0);
		_this.setAttribute("vst3state", m_native->preserveState ? m_native->preservedState : QString::fromLatin1(saveChunk().toBase64()));
		return;
	}
#endif
	if (m_shellId) { _this.setAttribute("shellid", QString::number(m_shellId)); }
	if ( m_embedMethod != "none" )
	{
		if( pluginWidget() != nullptr )
		{
			_this.setAttribute( "guivisible", pluginWidget()->isVisible() );
		}
	}
	else
	{
		int visible = isUIVisible();
		if ( visible != -1 )
		{
			_this.setAttribute( "guivisible", visible );
		}
	}

	// try to save all settings in a chunk
	QByteArray chunk = saveChunk();
	if( !chunk.isEmpty() )
	{
		_this.setAttribute( "chunk", QString( chunk.toBase64() ) );
	}
	else
	{
		// plugin doesn't seem to support chunks, therefore save
		// individual parameters
		const QMap<QString, QString> & dump = parameterDump();
		_this.setAttribute( "numparams", dump.size() );
		for( QMap<QString, QString>::const_iterator it = dump.begin();
							it != dump.end(); ++it )
		{
			_this.setAttribute( it.key(), it.value() );
		}
	}

	_this.setAttribute( "program", currentProgram() );
}

void VstPlugin::toggleUI()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { if (m_native->visible) { hideUI(); } else { showUI(); } return; }
#endif
	if ( m_embedMethod == "none" )
	{
		RemotePlugin::toggleUI();
	}
	else if (pluginWidget())
	{
		toggleEditorVisibility();
	}
}




void VstPlugin::setTempo( bpm_t _bpm )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return; }
#endif
	if (postVstTempo(static_cast<unsigned>(_bpm))) { return; }
	lock();
	sendMessage( message( IdVstSetTempo ).addInt( _bpm ) );
	unlock();
}




void VstPlugin::updateSampleRate()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		const auto result = m_native->proxy.configure({double(Engine::audioEngine()->outputSampleRate()),
			static_cast<std::uint32_t>(Engine::audioEngine()->framesPerPeriod()),
			Engine::audioEngine()->renderOnly() || Engine::getSong()->isExporting() ||
			dynamic_cast<AudioFileDevice*>(Engine::audioEngine()->audioDev()) != nullptr});
		if (result == vsthost::Error::None) { refreshNativeParameters(); }
		else { Engine::getSong()->collectError(tr("VST3 processing reconfiguration failed (error %1).").arg(static_cast<unsigned>(result))); }
		return;
	}
#endif
	lock();
	sendMessage( message( IdSampleRateInformation ).
			addInt( Engine::audioEngine()->outputSampleRate() ) );
	waitForMessage( IdInformationUpdated, true );
	unlock();
}




int VstPlugin::currentProgram()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return 0; }
#endif
	lock();
	sendMessage( message( IdVstCurrentProgram ) );
	waitForMessage( IdVstCurrentProgram, true );
	unlock();

	return m_currentProgram;
}



const QMap<QString, QString> & VstPlugin::parameterDump()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { m_native->proxy.refreshMetadata(); refreshNativeParameters(); return m_parameterDump; }
#endif
	lock();
	sendMessage( IdVstGetParameterDump );
	waitForMessage( IdVstParameterDump, true );
	unlock();

	return m_parameterDump;
}




void VstPlugin::setParameterDump( const QMap<QString, QString> & _pdump )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { for (const auto& value : _pdump) { setParam(value.section(':', 0, 0).toInt(), LocaleHelper::toFloat(value.section(':', 2, -1))); } return; }
#endif
	message m( IdVstSetParameterDump );
	m.addInt( _pdump.size() );
	for (const auto& str : _pdump)
	{
		const VstParameterDumpItem item =
		{
			str.section(':', 0, 0).toInt(), "", LocaleHelper::toFloat(str.section(':', 2, -1))
		};
		m.addInt( item.index );
		m.addString( item.shortLabel );
		m.addFloat( item.value );
	}
	lock();
	sendMessage( m );
	unlock();
}

QWidget *VstPlugin::pluginWidget()
{
	return m_pluginWidget;
}




bool VstPlugin::processMessage( const message & _m )
{
	if (_m.id == IdVstParameterEdits)
	{
		auto parse = [&_m](unsigned argument, std::uint32_t& value)
		{
			const auto text = _m.getString(argument);
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
			return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
		};
		struct Edit { unsigned phase; int index; float value; };
		std::vector<Edit> edits;
		std::uint32_t count = 0;
		if (!_m.argumentCount() || !parse(0, count) || count > 512 || _m.argumentCount() != 1 + 3 * count)
		{ m_failed = true; return false; }
		for (unsigned i = 0; i < count; ++i)
		{
			std::uint32_t phase = 0, index = 0, bits = 0;
			if (!parse(1 + 3 * i, phase) || !parse(2 + 3 * i, index) || !parse(3 + 3 * i, bits) ||
				phase > 2 || index >= static_cast<unsigned>(m_parameterCount))
			{ m_failed = true; return false; }
			const auto value = std::bit_cast<float>(bits);
			if (!std::isfinite(value) || value < 0 || value > 1) { m_failed = true; return false; }
			edits.push_back({phase, static_cast<int>(index), value});
		}
		QMetaObject::invokeMethod(this, [this, edits = std::move(edits)]
		{
			for (const auto& edit : edits) { applyParameterEdit(edit.phase, edit.index, edit.value); }
		}, Qt::QueuedConnection);
		return true;
	}
	if (_m.id == IdVstParameterCount)
	{
		m_parameterCount = _m.getInt();
		if (m_parameterCount < 0 || m_parameterCount > 65536) { m_failed = true; return false; }
		return true;
	}
	switch( _m.id )
	{
	case IdVstPluginWindowID:
		m_pluginWindowID = _m.getInt();
		if (m_embedMethod == "none" && !gui::GuiApplication::isWayland()
			&& ConfigManager::inst()->value("ui", "vstalwaysontop").toInt())
		{
#ifdef LMMS_BUILD_WIN32
			// We're changing the owner, not the parent,
			// so this is legal despite MSDN's warning
			SetWindowLongPtr( (HWND)(intptr_t) m_pluginWindowID,
					GWLP_HWNDPARENT,
					(LONG_PTR) gui::getGUI()->mainWindow()->winId() );
#endif

#if defined(LMMS_BUILD_LINUX) && (QT_VERSION < QT_VERSION_CHECK(6,0,0))
			XSetTransientForHint( QX11Info::display(),
					m_pluginWindowID,
					gui::getGUI()->mainWindow()->winId() );
#endif
		}
		break;

	case IdVstPluginEditorGeometry:
		m_pluginGeometry = QSize( _m.getInt( 0 ),
								  _m.getInt( 1 ) );
			break;

		case IdVstPluginName:
			m_name = _m.getQString();
			break;

		case IdVstPluginVersion:
			m_version = _m.getInt();
			break;

		case IdVstPluginVendorString:
			m_vendorString = _m.getQString();
			break;

		case IdVstPluginProductString:
			m_productString = _m.getQString();
			break;

		case IdVstCurrentProgram:
			m_currentProgram = _m.getInt();
			break;

		case IdVstCurrentProgramName:
			m_currentProgramName = _m.getQString();
			break;

		case IdVstProgramNames:
			m_allProgramNames = _m.getQString();
			break;

		case IdVstLoadAllParameterLabels:
		{
			const auto labels = _m.getQString();
			m_allParameterLabels.clear();
			for (int i = 0; i < labels.size();)
			{
				const int length = labels[i].digitValue();
				m_allParameterLabels.push_back(labels.mid(i + 1, length));
				i += length + 1;
			}
			break;
		}

		case IdVstLoadAllParameterDisplays:
		{
			const auto displays = _m.getQString();
			m_allParameterDisplays.clear();
			for (int i = 0; i < displays.size();)
			{
				const int length = displays[i].digitValue();
				m_allParameterDisplays.push_back(displays.mid(i + 1, length));
				i += length + 1;
			}
			break;
		}

		case IdVstUpdateParameterLabel:
			m_allParameterLabels.at(static_cast<std::size_t>(_m.getInt(0))) = _m.getQString(1);
			break;

		case IdVstUpdateParameterDisplay:
			m_allParameterDisplays.at(static_cast<std::size_t>(_m.getInt(0))) = _m.getQString(1);
			break;

		case IdVstPluginUniqueID:
			// TODO: display graphically in case of failure
			printf("unique ID: %s\n", _m.getString().c_str() );
			break;

		case IdVstParameterDump:
		{
			m_parameterDump.clear();
			const int num_params = _m.getInt();
			int p = 0;
			for( int i = 0; i < num_params; ++i )
			{
				VstParameterDumpItem item;
				item.index = _m.getInt( ++p );
				item.shortLabel = _m.getString( ++p );
				item.value = _m.getFloat( ++p );
	m_parameterDump["param" + QString::number( item.index )] =
				QString::number( item.index ) + ":" +
/*uncomented*/				/*QString( item.shortLabel )*/ QString::fromStdString(item.shortLabel) + ":" +
					QString::number( item.value );
			}
			break;
		}
		default:
			return RemotePlugin::processMessage( _m );
	}
	return true;

}


QWidget *VstPlugin::editor()
{
	return m_pluginWidget;
}


void VstPlugin::openPreset()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		gui::FileDialog dialog(nullptr, tr("Open Preset"), "", tr("VST3 Plugin Preset (*.vstpreset)"));
		dialog.setFileMode(gui::FileDialog::ExistingFile);
		if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
		{
			QFile file(dialog.selectedFiles().front());
			if (file.open(QIODevice::ReadOnly) && file.size() <= vsthost::MaxControlBytes + 65536)
			{
				const auto bytes = file.readAll();
				if (m_native->proxy.restorePreset(std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()), std::size_t(bytes.size()))) != vsthost::Error::None)
				{ Engine::getSong()->collectError(tr("Invalid VST3 preset or class identity.")); }
				refreshNativeParameters();
			}
		}
		return;
	}
#endif
	gui::FileDialog ofd(nullptr, tr("Open Preset"), "", tr("VST Plugin Preset (*.fxp *.fxb)"));
	ofd.setFileMode(gui::FileDialog::ExistingFiles);
	if (ofd.exec() == QDialog::Accepted && !ofd.selectedFiles().isEmpty())
	{
		lock();
		sendMessage(message(IdLoadPresetFile).addString(QSTR_TO_STDSTR(
			QDir::toNativeSeparators(ofd.selectedFiles()[0]))));
		waitForMessage(IdLoadPresetFile, true);
		unlock();
	}
}




void VstPlugin::setProgram( int index )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return; }
#endif
	lock();
	sendMessage( message( IdVstSetProgram ).addInt( index ) );
	waitForMessage( IdVstSetProgram, true );
	unlock();
}




void VstPlugin::rotateProgram( int offset )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return; }
#endif
	lock();
	sendMessage( message( IdVstRotateProgram ).addInt( offset ) );
	waitForMessage( IdVstRotateProgram, true );
	unlock();
}




void VstPlugin::loadProgramNames()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { m_allProgramNames.clear(); return; }
#endif
	lock();
	sendMessage( message( IdVstProgramNames ) );
	waitForMessage( IdVstProgramNames, true );
	unlock();
}




void VstPlugin::loadParameterLabels()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { refreshNativeParameters(); return; }
#endif
	lock();
	sendMessage(message(IdVstLoadAllParameterLabels));
	waitForMessage(IdVstLoadAllParameterLabels, true);
	unlock();
}




void VstPlugin::loadParameterDisplays()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { refreshNativeParameters(); return; }
#endif
	lock();
	sendMessage(message(IdVstLoadAllParameterDisplays));
	waitForMessage(IdVstLoadAllParameterDisplays, true);
	unlock();
}




void VstPlugin::updateParameterLabel(int index)
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { refreshNativeParameters(); return; }
#endif
	lock();
	sendMessage(message(IdVstUpdateParameterLabel).addInt(index));
	waitForMessage(IdVstUpdateParameterLabel, true);
	unlock();
}




void VstPlugin::updateParameterDisplay(int index)
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { refreshNativeParameters(); return; }
#endif
	lock();
	sendMessage(message(IdVstUpdateParameterDisplay).addInt(index));
	waitForMessage(IdVstUpdateParameterDisplay, true);
	unlock();
}




void VstPlugin::savePreset()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		gui::FileDialog dialog(nullptr, tr("Save Preset"), "", tr("VST3 Plugin Preset (*.vstpreset)"));
		dialog.setAcceptMode(gui::FileDialog::AcceptSave); dialog.setDefaultSuffix("vstpreset");
		if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
		{
			const auto preset = m_native->proxy.preset(); QSaveFile file(dialog.selectedFiles().front());
			if (preset.error != vsthost::Error::None || !file.open(QIODevice::WriteOnly) ||
				file.write(reinterpret_cast<const char*>(preset.payload.data()), preset.payload.size()) != qint64(preset.payload.size()) || !file.commit())
			{ Engine::getSong()->collectError(tr("Could not save VST3 preset.")); }
		}
		return;
	}
#endif
	QString presName = currentProgramName().isEmpty() ? tr(": default") : currentProgramName();
	presName.replace("\"", "'"); // QFileDialog unable to handle double quotes properly

	gui::FileDialog sfd(nullptr, tr("Save Preset"), presName.section(": ", 1, 1) + tr(".fxp"),
		tr("VST Plugin Preset (*.fxp *.fxb)"));

	if (p_name != "") // remember last directory
	{
		sfd.setDirectory(QFileInfo(p_name).absolutePath());
	}

	sfd.setAcceptMode(gui::FileDialog::AcceptSave);
	sfd.setFileMode(gui::FileDialog::AnyFile);
	if (sfd.exec() == QDialog::Accepted && !sfd.selectedFiles().isEmpty() && sfd.selectedFiles()[0] != "")
	{
		QString fns = sfd.selectedFiles()[0];
		p_name = fns;

		if ((fns.toUpper().indexOf(tr(".FXP")) == -1) && (fns.toUpper().indexOf(tr(".FXB")) == -1))
		{
			fns = fns + tr(".fxb");
		}
		else
		{
			fns = fns.left(fns.length() - 4) + (fns.right(4)).toLower();
		}
		lock();
		sendMessage(message(IdSavePresetFile).addString(QSTR_TO_STDSTR(QDir::toNativeSeparators(fns))));
		waitForMessage(IdSavePresetFile, true);
		unlock();
	}
}




void VstPlugin::setParam( int i, float f )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		if (applyingParameterEdit != this && i >= 0 && std::size_t(i) < m_native->ids.size())
		{
			if (QThread::currentThread() == thread())
			{ m_native->applyFeedback(*this, m_native->proxy.setParameter(m_native->ids[i], f)); }
			else { m_native->proxy.postEvent({0, 0, 0, 0, m_native->ids[i], f}); }
		}
		return;
	}
#endif
	if (applyingParameterEdit == this) { return; }
	if (postVstParameter(i, f)) { return; }
	lock();
	sendMessage( message( IdVstSetParameter ).addInt( i ).addFloat( f ) );
	//waitForMessage( IdVstSetParameter, true );
	unlock();
}



void VstPlugin::idleUpdate()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		const auto feedback = m_native->proxy.poll();
		m_native->applyFeedback(*this, feedback);
		if (feedback.error != vsthost::Error::None && !m_failed)
		{
			m_failed = true;
			const auto fault = m_native->proxy.fault();
			qWarning() << "VST3 session failed" << m_name
				<< "error" << static_cast<unsigned>(fault.error)
				<< "stage" << static_cast<unsigned>(fault.stage)
				<< "native" << fault.nativeCode;
		}
		return;
	}
#endif
	lock();
	sendMessage( message( IdVstIdleUpdate ) );
	unlock();
}

void VstPlugin::showUI()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { if (hasEditor() && m_embedMethod != "headless") { m_native->visible = m_native->proxy.showEditor().error == vsthost::Error::None; } return; }
#endif
	if ( m_embedMethod == "none" )
	{
		RemotePlugin::showUI();
	}
	else if ( m_embedMethod != "headless" )
	{
		if (! editor()) {
			qWarning() << "VstPlugin::showUI called before VstPlugin::createUI";
		}
		toggleEditorVisibility( true );
	}
}

void VstPlugin::hideUI()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { m_native->proxy.hideEditor(); m_native->visible = false; return; }
#endif
	if ( m_embedMethod == "none" )
	{
		RemotePlugin::hideUI();
	}
	else if ( pluginWidget() != nullptr )
	{
		toggleEditorVisibility( false );
	}
}

// X11Embed only
void VstPlugin::handleClientEmbed()
{
	lock();
	sendMessage( IdShowUI );
	unlock();
}



void VstPlugin::loadChunk( const QByteArray & _chunk )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		const auto* begin = reinterpret_cast<const std::uint8_t*>(_chunk.constData());
		const auto bytes = std::span(begin, std::size_t(_chunk.size()));
		if (!vsthost::validVst3State(bytes))
		{ Engine::getSong()->collectError(tr("Invalid VST3 state envelope. Current state is unchanged.")); return; }
		// Retain the imported blob before calling native code: if restoration fails,
		// a later save must not erase the project's only recoverable state.
		m_native->lastState = _chunk;
		m_native->preservedState = QString::fromLatin1(_chunk.toBase64()); m_native->preserveState = true;
		if (m_native->proxy.restoreState({begin, begin + _chunk.size()}) != vsthost::Error::None)
		{ Engine::getSong()->collectError(tr("VST3 state restoration failed. Original data is preserved.")); return; }
		m_native->preserveState = false; refreshNativeParameters(); return;
	}
#endif
	QTemporaryFile tf;
	if( tf.open() )
	{
		tf.write( _chunk );
		tf.flush();

		lock();
		sendMessage( message( IdLoadSettingsFromFile ).
				addString(
					QSTR_TO_STDSTR(
						QDir::toNativeSeparators( tf.fileName() ) ) ).
				addInt( _chunk.size() ) );
		waitForMessage( IdLoadSettingsFromFile, true );
		unlock();
	}
}




QByteArray VstPlugin::saveChunk()
{
#ifdef LMMS_BUILD_WIN32
	if (m_native)
	{
		const auto state = m_native->proxy.state();
		if (state.error != vsthost::Error::None || !vsthost::validVst3State(state.payload))
		{ Engine::getSong()->collectError(tr("VST3 state could not be saved. Previous state is preserved.")); return m_native->lastState; }
		m_native->lastState = QByteArray(reinterpret_cast<const char*>(state.payload.data()), state.payload.size());
		return m_native->lastState;
	}
#endif
	QByteArray a;
	QTemporaryFile tf;
	if( tf.open() )
	{
		lock();
		sendMessage( message( IdSaveSettingsToFile ).
				addString(
					QSTR_TO_STDSTR(
						QDir::toNativeSeparators( tf.fileName() ) ) ) );
		waitForMessage( IdSaveSettingsToFile, true );
		unlock();
		a = tf.readAll();
	}

	return a;
}

void VstPlugin::toggleEditorVisibility( int visible )
{
	QWidget* w = editor();
	if ( ! w ) {
		return;
	}

	if ( visible < 0 ) {
		visible = ! w->isVisible();
	}
	w->setVisible( visible );
}

void VstPlugin::createUI( QWidget * parent )
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return; }
#endif
	if ( m_pluginWidget ) {
		qWarning() << "VstPlugin::createUI called twice";
		m_pluginWidget->setParent( parent );
		return;
	}

	if( m_pluginWindowID == 0 )
	{
		return;
	}

	QWidget* container = nullptr;

	if (m_embedMethod == "qt" )
	{
		QWindow* vw = QWindow::fromWinId(m_pluginWindowID);
		container = QWidget::createWindowContainer(vw, parent );
		container->installEventFilter(this);
	} else

#ifdef LMMS_BUILD_WIN32
	if (m_embedMethod == "win32" )
	{
		QWidget * helper = new QWidget;
		QHBoxLayout * l = new QHBoxLayout( helper );
		QWidget * target = new QWidget( helper );
		l->setSpacing( 0 );
		l->setContentsMargins(0, 0, 0, 0);
		l->addWidget( target );

		// we've to call that for making sure, Qt created the windows
		helper->winId();
		HWND targetHandle = (HWND)target->winId();
		HWND pluginHandle = (HWND)(intptr_t)m_pluginWindowID;

		DWORD style = GetWindowLong(pluginHandle, GWL_STYLE);
		style = style & ~(WS_POPUP);
		style = style | WS_CHILD;
		SetWindowLong(pluginHandle, GWL_STYLE, style);
		SetParent(pluginHandle, targetHandle);

		DWORD threadId = GetWindowThreadProcessId(pluginHandle, nullptr);
		DWORD currentThreadId = GetCurrentThreadId();
		AttachThreadInput(currentThreadId, threadId, true);

		container = helper;
		RemotePlugin::showUI();

	} else
#endif

#if defined(LMMS_BUILD_LINUX) && (QT_VERSION < QT_VERSION_CHECK(6,0,0))
	if (m_embedMethod == "xembed" )
	{
		if (parent)
		{
			parent->setAttribute(Qt::WA_NativeWindow);
		}
		auto embedContainer = new QX11EmbedContainer(parent);
		connect(embedContainer, SIGNAL(clientIsEmbedded()), this, SLOT(handleClientEmbed()));
		embedContainer->embedClient( m_pluginWindowID );
		container = embedContainer;
	} else
#endif
	{
		qCritical() << "Unknown embed method" << m_embedMethod;
		return;
	}

	container->setFixedSize( m_pluginGeometry );
	container->setWindowTitle( name() );

	m_pluginWidget = container;
}

bool VstPlugin::eventFilter(QObject *obj, QEvent *event)
{
	if (embedMethod() == "qt" && obj == m_pluginWidget)
	{
		if (event->type() == QEvent::Show) {
			RemotePlugin::showUI();
		}
		qDebug() << obj << event;
	}
	return false;
}

QString VstPlugin::embedMethod() const
{
#ifdef LMMS_BUILD_WIN32
	if (m_native) { return m_embedMethod == "headless" ? "headless" : "none"; }
#endif
	return m_embedMethod;
}

namespace gui {

VstPluginKnob::VstPluginKnob(VstPlugin* plugin, int paramIndex, const QString& name, QWidget* parent)
	: gui::Knob{gui::KnobType::Bright26, name.left(15), SMALL_FONT_SIZE, parent, name}
	, m_plugin{plugin}
	, m_paramIndex{paramIndex}
{
	assert(m_plugin != nullptr);
	setDescription(name + ":");
}

void VstPluginKnob::timerEvent(QTimerEvent* event)
{
	if (event->timerId() != m_rateLimitTimerId) { return; }

	if (textFloat().source() != static_cast<FloatModelEditorBase*>(this))
	{
		// This knob is no longer controlling the text float,
		// so we don't need the timer to continue running.
		killTimer(m_rateLimitTimerId);
		m_rateLimitTimerId = 0;
		return;
	}

	// Enough time has passed, so time for an update
	m_updateNow = true;
}

auto VstPluginKnob::currentValueToText() -> QString
{
	constexpr auto updatesPerSecond = 15;

	if (m_rateLimitTimerId == 0)
	{
		// Use a timer to control the rate of text float updates.
		// Otherwise the CPU will spike when the parameter emits a bunch of dataChanged()
		// events in a short period of time - i.e. when the parameter's value changes rapidly.
		m_rateLimitTimerId = startTimer(std::chrono::milliseconds{1000} / updatesPerSecond);
	}

	return getParameterText();
}

auto VstPluginKnob::currentValueToTextUpdate() -> std::optional<QString>
{
	if (!m_updateNow) { return std::nullopt; }
	m_updateNow = false;

	return getParameterText();
}

auto VstPluginKnob::getParameterText() const -> QString
{
	m_plugin->updateParameterLabel(m_paramIndex);
	m_plugin->updateParameterDisplay(m_paramIndex);

	const auto& paramLabels = m_plugin->allParameterLabels();
	const auto& paramDisplays = m_plugin->allParameterDisplays();
	assert(paramLabels.size() == paramDisplays.size());

	assert(static_cast<std::size_t>(m_paramIndex) < paramLabels.size());
	return paramDisplays[m_paramIndex] + ' ' + paramLabels[m_paramIndex];
}

} // namespace gui
} // namespace lmms
