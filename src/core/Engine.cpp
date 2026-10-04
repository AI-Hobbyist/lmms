/*
 * Engine.cpp - implementation of LMMS' engine-system
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include "Engine.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Mixer.h"
#include "Ladspa2LMMS.h"
#include "Lv2Manager.h"
#include "PatternStore.h"
#include "Plugin.h"
#include "PresetPreviewPlayHandle.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "BandLimitedWave.h"
#include "Oscillator.h"
#ifdef LMMS_BUILD_WIN32
#include "vsthost/CatalogJobs.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <memory>
#endif

#ifdef WANT_AGENT
#include "agent/ExportCommands.h"
#endif
#ifdef WANT_AGENT_MCP
#include "agent/mcp/HttpMcpServer.h"
#endif

namespace lmms
{

float Engine::s_framesPerTick;
AudioEngine* Engine::s_audioEngine = nullptr;
Mixer * Engine::s_mixer = nullptr;
PatternStore * Engine::s_patternStore = nullptr;
Song * Engine::s_song = nullptr;
ProjectJournal * Engine::s_projectJournal = nullptr;
#ifdef LMMS_HAVE_LV2
Lv2Manager * Engine::s_lv2Manager = nullptr;
#endif
Ladspa2LMMS * Engine::s_ladspaManager = nullptr;
void* Engine::s_dndPluginKey = nullptr;
#ifdef LMMS_BUILD_WIN32
namespace { std::unique_ptr<vsthost::CatalogJobs> catalogJobs; }
vsthost::CatalogJobs* Engine::vstCatalog() { return catalogJobs.get(); }
bool Engine::refreshVstCatalog(QString* error, bool force)
{
	if (error) { error->clear(); }
	if (!catalogJobs) { if (error) { *error = tr("VST catalog is unavailable."); } return false; }
	QString configurationError;
	auto roots = ConfigManager::inst()->vstScanRoots(&configurationError);
	if (!configurationError.isEmpty()) { if (error) { *error = configurationError; } return false; }
	catalogJobs->refresh(std::move(roots), {force}); return true;
}
#endif




void Engine::init( bool renderOnly )
{
	Engine *engine = inst();

	emit engine->initProgress(tr("Generating wavetables"));
	// generate (load from file) bandlimited wavetables
	BandLimitedWave::generateWaves();
	//initialize oscillators
	Oscillator::waveTableInit();

	emit engine->initProgress(tr("Initializing data structures"));
	s_projectJournal = new ProjectJournal;
	s_audioEngine = new AudioEngine( renderOnly );
	s_song = new Song;
	s_mixer = new Mixer;
	s_patternStore = new PatternStore;

#ifdef LMMS_HAVE_LV2
	s_lv2Manager = new Lv2Manager;
	s_lv2Manager->initPlugins();
#endif
	s_ladspaManager = new Ladspa2LMMS;

	s_projectJournal->setJournalling( true );

	emit engine->initProgress(tr("Opening audio and midi devices"));
	s_audioEngine->initDevices();

	PresetPreviewPlayHandle::init();

#ifdef LMMS_BUILD_WIN32
	const auto directories = QDir::searchPaths("plugins");
	const auto pluginDirectory = qEnvironmentVariableIsSet("LMMS_PLUGIN_DIR") ? qEnvironmentVariable("LMMS_PLUGIN_DIR") :
		(directories.isEmpty() ? QCoreApplication::applicationDirPath() + "/plugins" : directories.front());
	const auto helper = [&](const QString& name) { return QDir::cleanPath(pluginDirectory + "/" + name); };
	catalogJobs = std::make_unique<vsthost::CatalogJobs>(vsthost::PluginCatalog::Configuration{
		helper("32/RemoteVstPlugin32.exe"), helper("RemoteVstPlugin64.exe"),
		helper("32/RemoteVstHost32.exe"), helper("RemoteVstHost64.exe"),
		QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/vst/catalog-v1.json",
		"lmms-vst-catalog-2/" + ConfigManager::inst()->defaultVersion(), helper("RemoteCatalogIo.exe")});
	if (!renderOnly) { refreshVstCatalog(); }
#endif

	emit engine->initProgress(tr("Launching audio engine threads"));
	s_audioEngine->startProcessing();
#ifdef WANT_AGENT_MCP
	if (!renderOnly) { agent::mcp::applyConfiguration(); }
#endif
}




void Engine::destroy()
{
#ifdef LMMS_BUILD_WIN32
	// Cancel and join control work before any configuration/application owner dies.
	catalogJobs.reset();
#endif
#ifdef WANT_AGENT_MCP
	agent::mcp::shutdownService();
#endif
#ifdef WANT_AGENT
	agent::shutdownAudioExports();
#endif
	s_projectJournal->stopAllJournalling();
	s_audioEngine->stopProcessing();

	PresetPreviewPlayHandle::cleanup();

	s_song->clearProject();

	deleteHelper( &s_patternStore );

	deleteHelper( &s_mixer );
	deleteHelper( &s_audioEngine );

#ifdef LMMS_HAVE_LV2
	deleteHelper( &s_lv2Manager );
#endif
	deleteHelper( &s_ladspaManager );

	//delete ConfigManager::inst();
	deleteHelper( &s_projectJournal );

	deleteHelper( &s_song );

	delete ConfigManager::inst();

	// The oscillator FFT plans remain throughout the application lifecycle
	// due to being expensive to create, and being used whenever a userwave form is changed
	Oscillator::destroyFFTPlans();
}




float Engine::framesPerTick(sample_rate_t sampleRate)
{
	return sampleRate * 60.0f * 4 /
			DefaultTicksPerBar / s_song->getTempo();
}




void Engine::updateFramesPerTick()
{
	s_framesPerTick = s_audioEngine->outputSampleRate() * 60.0f * 4 / DefaultTicksPerBar / s_song->getTempo();
}




void Engine::setDndPluginKey(void *newKey)
{
	Q_ASSERT(static_cast<Plugin::Descriptor::SubPluginFeatures::Key*>(newKey));
	s_dndPluginKey = newKey;
}




void *Engine::pickDndPluginKey()
{
	return s_dndPluginKey;
}




Engine * Engine::s_instanceOfMe = nullptr;

} // namespace lmms
