/*
 * VstEffect.cpp - class for handling VST effect plugins
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


#include "VstEffect.h"

#include "GuiApplication.h"
#include "Song.h"
#include "TextFloat.h"
#include "VstPlugin.h"
#include "vsthost/PluginCatalog.h"
#include "vsthost/CatalogLocatorCodec.h"
#include <QRegularExpression>
#include "VstSubPluginFeatures.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vsteffect_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"VST",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"plugin for using arbitrary VST effects inside LMMS." ),
	"Tobias Doerffel <tobydox/at/users.sf.net>",
	0x0200,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new VstSubPluginFeatures( Plugin::Type::Effect )
} ;

}


VstEffect::VstEffect( Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key ) :
	Effect( &vsteffect_plugin_descriptor, _parent, _key ),
	m_pluginMutex(),
	m_key( *_key ),
	m_nativeEffect(_key->attributes.value("format") == "vst3"),
	m_vstControls( this )
{
	bool loaded = false;
	if( !m_key.attributes["file"].isEmpty() )
	{
		loaded = openPlugin(m_key.attributes["file"]);
	}
	setDisplayName( m_key.attributes["file"].section( ".dll", 0, 0 ).isEmpty()
		? m_key.name : m_key.attributes["file"].section( ".dll", 0, 0 ) );

	setDontRun(!loaded);
}




Effect::ProcessStatus VstEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	assert(m_plugin != nullptr);
	static thread_local auto tempBuf = std::array<SampleFrame, MAXIMUM_BUFFER_SIZE>();

	std::memcpy(tempBuf.data(), buf, sizeof(SampleFrame) * frames);
	if (m_pluginMutex.tryLock((Engine::audioEngine()->renderOnly() || Engine::getSong()->isExporting()) ? -1 : 0))
	{
		m_plugin->processEffect(tempBuf.data(), tempBuf.data(), frames, wetLevel(), dryLevel());
		m_pluginMutex.unlock();
	}
	else if (m_nativeEffect)
	{
		// Do not leak an unaligned dry block across a control transition.
		std::fill_n(tempBuf.begin(), frames, SampleFrame{});
	}
	std::copy_n(tempBuf.begin(), frames, buf);

	// Silent input must continue flushing the bridge and native plugin tail.
	return m_nativeEffect ? ProcessStatus::Continue : ProcessStatus::ContinueIfNotQuiet;
}




bool VstEffect::openPlugin(const QString& plugin)
{
	gui::TextFloat* tf = nullptr;
	if( gui::getGUI() != nullptr )
	{
		tf = gui::TextFloat::displayMessage(
			VstPlugin::tr( "Loading plugin" ),
			VstPlugin::tr( "Please wait while loading VST plugin..." ),
				PLUGIN_NAME::getIconPixmap( "logo", 24, 24 ), 0 );
	}

	QMutexLocker ml( &m_pluginMutex ); Q_UNUSED( ml );
	bool validId = true;
	const auto shellId = m_key.attributes.contains("shellid") ? m_key.attributes["shellid"].toUInt(&validId) : 0;
	if (!validId || (m_key.attributes.contains("shellid") && !shellId))
	{ delete tf; collectErrorForUI(VstPlugin::tr("Invalid VST2 shell identity.")); return false; }
	vsthost::CatalogEntry selected;
	const bool native = m_key.attributes.value("format") == "vst3";
	if (native)
	{
		const auto cid = m_key.attributes.value("classid");
		const auto architecture = m_key.attributes.value("architecture");
		if (!QRegularExpression("^[0-9A-Fa-f]{32}$").match(cid).hasMatch() || (architecture != "32" && architecture != "64") || shellId)
		{ delete tf; collectErrorForUI(VstPlugin::tr("Invalid VST3 class identity or architecture.")); return false; }
		const auto raw = QByteArray::fromHex(cid.toLatin1()); std::memcpy(selected.identity.cid.data(), raw.constData(), 16);
		selected.identity.format = vsthost::Format::Vst3;
		selected.identity.architecture = architecture == "32" ? vsthost::Architecture::X86 : vsthost::Architecture::X64;
		QByteArray fingerprint;
		if (m_key.attributes.contains("fingerprint") &&
			!vsthost::decodeCatalogFingerprint(m_key.attributes.value("fingerprint"), fingerprint))
		{ delete tf; collectErrorForUI(VstPlugin::tr("Invalid VST3 module fingerprint.")); return false; }
		selected.locator = {plugin, m_key.attributes.value("binarypath"), m_key.attributes.value("version"), fingerprint};
		selected.name = m_key.name; selected.vendor = m_key.attributes.value("vendor");
	}
	m_plugin = QSharedPointer<VstPlugin>(new VstPlugin(plugin, shellId, {}, native ? &selected : nullptr));
	if( m_plugin->failed() )
	{
		m_plugin.clear();
		delete tf;
		collectErrorForUI(VstPlugin::tr("The VST plugin %1 could not be loaded.").arg(plugin));
		return false;
	}

	delete tf;

	m_key.attributes["file"] = plugin;
	m_vstControls.initializeParameterModels();
	return true;
}




extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model * _parent, void * _data )
{
	return new VstEffect( _parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>(
								_data ) );
}

}


} // namespace lmms
