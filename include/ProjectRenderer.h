/*
 * ProjectRenderer.h - ProjectRenderer class for easily rendering projects
 *
 * Copyright (c) 2008-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_PROJECT_RENDERER_H
#define LMMS_PROJECT_RENDERER_H

#include <atomic>
#include <memory>

#include "AudioFileDevice.h"
#include "AudioEngine.h"
#include "OutputSettings.h"

#include "lmms_export.h"

namespace lmms
{
namespace svs {class ExportSnapshot;}


class LMMS_EXPORT ProjectRenderer : public QThread
{
	Q_OBJECT
public:
	enum class ExportFileFormat : int
	{
		Wave,
		Flac,
		Ogg,
		MP3,
		Count
	} ;
	constexpr static auto NumFileFormats = static_cast<std::size_t>(ExportFileFormat::Count);

	struct FileEncodeDevice
	{
		bool isAvailable() const { return m_getDevInst != nullptr; }

		ExportFileFormat m_fileFormat;
		const char * m_description;
		const char * m_extension;
		AudioFileDeviceInstantiaton m_getDevInst;
	} ;

	ProjectRenderer(const OutputSettings& _os, ExportFileFormat _file_format, const QString& _out_file);
		~ProjectRenderer() override;
		int progressPercent() const { return m_progress.load(); }
		bool renderSucceeded() const { return m_succeeded.load(); }
		QString renderError() const {return m_renderError;}
		void setIgnoreFailedSVSRegions(bool ignore) {m_ignoreFailedSVS=ignore;}
			void setSVSSnapshot(std::unique_ptr<svs::ExportSnapshot>);

	bool isReady() const
	{
		return m_fileDev != nullptr;
	}

	static ExportFileFormat getFileFormatFromExtension(
							const QString & _ext );

	static QString getFileExtensionFromFormat( ExportFileFormat fmt );

	static const std::array<FileEncodeDevice, 5> fileEncodeDevices;

public slots:
	void startProcessing();
	void abortProcessing();

	void updateConsoleProgress();


signals:
	void progressChanged( int );
		void finished();
		void svsExportFailed(const QString& reason);


private:
	void run() override;
		void startPreparedRender();

	AudioFileDevice * m_fileDev;

		std::atomic_int m_progress;
		std::atomic_bool m_abort;
		std::atomic_bool m_succeeded{false};
		bool m_deviceTransferred = false;
			bool m_ignoreFailedSVS = false;
			std::atomic_bool m_exportStarted{false};
			QString m_renderError;
			std::unique_ptr<svs::ExportSnapshot> m_svsSnapshot;

} ;


} // namespace lmms

#endif // LMMS_PROJECT_RENDERER_H
