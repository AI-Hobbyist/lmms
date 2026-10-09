/*
 * ProjectRenderer.cpp - ProjectRenderer-class for easily rendering projects
 *
 * Copyright (c) 2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include <QFile>

#include "ProjectRenderer.h"
#include "Song.h"
#include "PerfLog.h"
#include "SVSExportSnapshot.h"
#include "SVCClip.h"
#include "SVCTrack.h"

#include "AudioFileWave.h"
#include "AudioFileOgg.h"
#include "AudioFileMP3.h"
#include "AudioFileFlac.h"


namespace lmms
{


const std::array<ProjectRenderer::FileEncodeDevice, 5> ProjectRenderer::fileEncodeDevices
{

	FileEncodeDevice{ ProjectRenderer::ExportFileFormat::Wave,
		QT_TRANSLATE_NOOP( "ProjectRenderer", "WAV (*.wav)" ),
					".wav", &AudioFileWave::getInst },
	FileEncodeDevice{ ProjectRenderer::ExportFileFormat::Flac,
		QT_TRANSLATE_NOOP("ProjectRenderer", "FLAC (*.flac)"),
		".flac",
		&AudioFileFlac::getInst
	},
	FileEncodeDevice{ ProjectRenderer::ExportFileFormat::Ogg,
		QT_TRANSLATE_NOOP( "ProjectRenderer", "OGG (*.ogg)" ),
					".ogg",
#ifdef LMMS_HAVE_OGGVORBIS
					&AudioFileOgg::getInst
#else
					nullptr
#endif
									},
	FileEncodeDevice{ ProjectRenderer::ExportFileFormat::MP3,
		QT_TRANSLATE_NOOP( "ProjectRenderer", "MP3 (*.mp3)" ),
					".mp3",
#ifdef LMMS_HAVE_MP3LAME
					&AudioFileMP3::getInst
#else
					nullptr
#endif
									},
	// Insert your own file-encoder infos here.
	// Maybe one day the user can add own encoders inside the program.

	FileEncodeDevice{ ProjectRenderer::ExportFileFormat::Count, nullptr, nullptr, nullptr }

} ;

ProjectRenderer::ProjectRenderer(
	const OutputSettings& outputSettings, ExportFileFormat exportFileFormat, const QString& outputFilename)
	: QThread(Engine::audioEngine())
	, m_fileDev(nullptr)
	, m_progress(0)
	, m_abort(false)
{
	AudioFileDeviceInstantiaton audioEncoderFactory = fileEncodeDevices[static_cast<std::size_t>(exportFileFormat)].m_getDevInst;

	if (audioEncoderFactory)
	{
		bool successful = false;

		m_fileDev = audioEncoderFactory(
					outputFilename, outputSettings, DEFAULT_CHANNELS,
					Engine::audioEngine(), successful );
		if( !successful )
		{
			delete m_fileDev;
			m_fileDev = nullptr;
		}
	}
}




// Little help function for getting file format from a file extension
// (only for registered file-encoders).
ProjectRenderer::ExportFileFormat ProjectRenderer::getFileFormatFromExtension(
							const QString & _ext )
{
	int idx = 0;
	while( fileEncodeDevices[idx].m_fileFormat != ExportFileFormat::Count )
	{
		if( QString( fileEncodeDevices[idx].m_extension ) == _ext )
		{
			return( fileEncodeDevices[idx].m_fileFormat );
		}
		++idx;
	}

	return( ExportFileFormat::Wave ); // Default.
}




QString ProjectRenderer::getFileExtensionFromFormat(
		ExportFileFormat fmt )
{
	return fileEncodeDevices[static_cast<std::size_t>(fmt)].m_extension;
}




ProjectRenderer::~ProjectRenderer()
{
	if (isRunning())
	{
		m_abort = true;
		wait();
	}
	if (m_svsSnapshot)
	{
		m_svsSnapshot->completed = {};
		m_svsSnapshot->cancel();
		m_svsSnapshot->deactivate();
	}
	if (m_exportStarted.exchange(false))
		Engine::getSong()->stopExport();
	// The audio engine owns the device after startProcessing transfers it.
	if (!m_deviceTransferred)
	{
		delete m_fileDev;
	}
}

void ProjectRenderer::startProcessing()
{
	if( isReady() )
	{
		for (const auto* track : Engine::getSong()->tracks())
		{
			if (track->type() != Track::Type::SVC || track->isMuted()) { continue; }
			for (const auto* clip : track->getClips())
			{
				const auto* svcClip = static_cast<const SVCClip*>(clip);
				if (clip->isMuted() || svcClip->conversionComplete()) { continue; }
				m_renderError = tr("SVC track '%1': '%2' needs a successful re-render before export.")
					.arg(track->name(), clip->name());
				const auto path = m_fileDev->outputFile();
				delete m_fileDev;
				m_fileDev = nullptr;
				QFile::remove(path);
				QPointer<ProjectRenderer> target(this);
				emit svsExportFailed(m_renderError);
				if (target) { emit finished(); }
				return;
			}
		}
			if (!m_svsSnapshot)
			m_svsSnapshot = std::make_unique<svs::ExportSnapshot>(
				svs::ExportSnapshot::capture(*Engine::getSong(), m_fileDev->sampleRate()));
		Engine::audioEngine()->stopProcessing();
		m_svsSnapshot->freezeTimeline(*Engine::getSong());
		m_svsSnapshot->invalidated = [this](const QString& reason) {
			m_renderError = reason;
			m_abort = true;
			emit svsExportFailed(reason);
		};
		Engine::getSong()->startExport();
		m_exportStarted = true;
		m_svsSnapshot->completed = [this] {
			if (m_svsSnapshot->state() == svs::ExportSnapshot::State::Ready && !m_abort)
				m_svsSnapshot->activate(*Engine::getSong());
			if (m_svsSnapshot->state() != svs::ExportSnapshot::State::Ready || m_abort)
			{
				if (!m_abort)
					m_renderError = m_svsSnapshot->diagnostics().join('\n');
				const auto path = m_fileDev->outputFile();
				delete m_fileDev;
				m_fileDev = nullptr;
				QFile::remove(path);
				if (m_exportStarted.exchange(false))
					Engine::getSong()->stopExport();
				QPointer<ProjectRenderer> target(this);
				if (!m_renderError.isEmpty())
					emit svsExportFailed(m_renderError);
				if (!target)
					return;
				emit finished();
				return;
			}
			startPreparedRender();
		};
		connect(this, &QThread::finished, this, [this] {
			if (m_svsSnapshot)
				m_svsSnapshot->deactivate();
			emit finished();
		});
		m_svsSnapshot->prepare(m_ignoreFailedSVS);
	}
}
void ProjectRenderer::setSVSSnapshot(std::unique_ptr<svs::ExportSnapshot> snapshot)
{
	m_svsSnapshot = std::move(snapshot);
}
void ProjectRenderer::startPreparedRender()
{
		// Have to do audio engine stuff with GUI-thread affinity in order to
		// make slots connected to sampleRateChanged()-signals being called immediately.
		Engine::audioEngine()->setAudioDevice(m_fileDev, false);
		m_deviceTransferred = true;

		start(
#ifndef LMMS_BUILD_WIN32
			QThread::HighPriority
#endif
						);

}


void ProjectRenderer::run()
{
	PerfLogTimer perfLog("Project Render");

	// Skip first empty buffer.
	Engine::audioEngine()->renderNextPeriod();

	m_progress = 0;

	// Now start processing
	Engine::audioEngine()->startProcessing();

	// Continually track and emit progress percentage to listeners.
	while (!Engine::getSong()->isExportDone() && !m_abort)
	{
		const auto buffer = Engine::audioEngine()->renderNextPeriod();
		m_fileDev->writeBuffer(buffer.data(), buffer.size());
		if (m_fileDev->hasWriteError())
		{
			break;
		}

		const int nprog = Engine::getSong()->getExportProgress();
		if (m_progress != nprog)
		{
			m_progress = nprog;
			emit progressChanged(m_progress.load());
		}
	}

	// Notify the audio engine of the end of processing.
	Engine::audioEngine()->stopProcessing();

	Engine::getSong()->stopExport();
	m_exportStarted = false;
	m_fileDev->finalize();
	m_succeeded = !m_abort.load() && !m_fileDev->hasWriteError();

	perfLog.end();

	// If the user aborted export-process, the file has to be deleted.
	if( m_abort )
	{
		m_fileDev->discardOutput();
	}
}




void ProjectRenderer::abortProcessing()
{
	m_abort = true;
	if (m_svsSnapshot)
	{
		m_svsSnapshot->completed = {};
		m_svsSnapshot->cancel();
	}
	wait();
	if (m_exportStarted.exchange(false))
		Engine::getSong()->stopExport();
	if (m_svsSnapshot)
		m_svsSnapshot->deactivate();
	if (!m_deviceTransferred && m_fileDev)
	{
		const auto path = m_fileDev->outputFile();
		delete m_fileDev;
		m_fileDev = nullptr;
		QFile::remove(path);
	}
}



void ProjectRenderer::updateConsoleProgress()
{
	constexpr int cols = 50;
	static int rot = 0;
	auto buf = std::array<char, 80>{};
	auto prog = std::array<char, cols + 1>{};

	for( int i = 0; i < cols; ++i )
	{
		prog[i] = ( i*100/cols <= m_progress ? '-' : ' ' );
	}
	prog[cols] = 0;

	const auto activity = "|/-\\";
	std::fill(buf.begin(), buf.end(), 0);
	std::snprintf(buf.data(), buf.size(), "\r|%s|    %3d%%   %c  ", prog.data(), m_progress.load(), activity[rot]);
	rot = ( rot+1 ) % 4;

	fprintf( stderr, "%s", buf.data() );
	fflush( stderr );
}


} // namespace lmms
