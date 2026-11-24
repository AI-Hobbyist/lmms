/*
 * MainWindow.cpp - implementation of LMMS-main-window
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "MainWindow.h"
#include "SVSProjectController.h"

#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDomElement>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QLocale>
#include <QMdiArea>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <tuple>
#include <utility>
#include <memory>

#include "AboutDialog.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "ControllerRackView.h"
#include "DataFile.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "Effect.h"
#include "EffectChain.h"
#include "vsthost/VstNodePorts.h"
#include "vsthost/NativeVstPresetDrop.h"
#include "EffectSelectDialog.h"
#include "Engine.h"
#ifdef LMMS_BUILD_WIN32
#include "vsthost/CatalogJobs.h"
#endif
#include "ExportProjectDialog.h"
#include "FileBrowser.h"
#include "FileDialog.h"
#include "Metronome.h"
#include "Mixer.h"
#include "MixerView.h"
#include "MidiClip.h"
#include "MidiClipView.h"
#include "NodeWorkflowCanvas.h"
#include "NodePatternTriggers.h"
#include "NodeAudioSilenceThreshold.h"
#include "GuiApplication.h"
#include "ImportFilter.h"
#include "Instrument.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "MicrotunerConfig.h"
#include "PathUtil.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "PianoRoll.h"
#include "PianoView.h"
#include "PluginBrowser.h"
#include "SVCBrowser.h"
#include "SVCCatalog.h"
#include "SVCTrack.h"
#include "SVCViews.h"
#include "SVSBrowser.h"
#include "SVSClip.h"
#include "SVSModel.h"
#include "SVSTrack.h"
#include "SVSViews.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "PluginFactory.h"
#include "PluginView.h"
#include "ProjectJournal.h"
#include "ProjectNotes.h"
#include "ProjectRenderer.h"
#include "RecentProjectsMenu.h"
#include "RemotePluginBase.h"
#include "SetupDialog.h"
#include "SideBar.h"
#include "SongEditor.h"
#include "SubWindow.h"
#include "TemplatesMenu.h"
#include "TextFloat.h"
#include "ToolButton.h"
#include "ToolPlugin.h"
#include "VersionedSaveDialog.h"

#include "lmmsversion.h"

#ifdef WANT_AGENT_CHAT
#include "agent/chat/ChatDock.h"
#endif


namespace lmms::gui
{


MainWindow::MainWindow() :
	m_workspace( nullptr ),
	m_toolsMenu( nullptr ),
	m_autoSaveTimer( this ),
	m_viewMenu( nullptr ),
	m_metronomeToggle( 0 ),
	m_session( SessionState::Normal )
{
	setAttribute( Qt::WA_DeleteOnClose );

	auto main_widget = new QWidget(this);
	auto vbox = new QVBoxLayout(main_widget);
	vbox->setSpacing( 0 );
	vbox->setContentsMargins(0, 0, 0, 0);

	auto w = new QWidget(main_widget);
	auto hbox = new QHBoxLayout(w);
	hbox->setSpacing( 0 );
	hbox->setContentsMargins(0, 0, 0, 0);

	auto sideBar = new SideBar(Qt::Vertical, w);

	auto splitter = new QSplitter(Qt::Horizontal, w);
	splitter->setChildrenCollapsible( false );

	ConfigManager* confMgr = ConfigManager::inst();
	bool sideBarOnRight = confMgr->value("ui", "sidebaronright").toInt();

	emit initProgress(tr("Preparing plugin browser"));
	sideBar->appendTab( new PluginBrowser( splitter ) );
	sideBar->appendTab(new SVCBrowser(splitter));
	sideBar->appendTab(new SVSBrowser(splitter));
	emit initProgress(tr("Preparing file browsers"));

	sideBar->appendTab(new FileBrowser(FileBrowser::Type::Favorites, ConfigManager::inst()->favoriteItems().join("*"),
		FileItem::defaultFilters(), tr("My Favorites"),
		embed::getIconPixmap("star").transformed(QTransform().rotate(90)), splitter, false, "", ""));

	sideBar->appendTab(new FileBrowser(FileBrowser::Type::Normal,
		confMgr->userProjectsDir() + "*" + confMgr->factoryProjectsDir(), "*.mmp *.mmpn *.mmpnz *.mmpz *.xml *.mid *.mpt",
		tr("My Projects"), embed::getIconPixmap("project_file").transformed(QTransform().rotate(90)), splitter, false,
		confMgr->userProjectsDir(), confMgr->factoryProjectsDir()));

	sideBar->appendTab(new FileBrowser(FileBrowser::Type::Normal,
		confMgr->userSamplesDir() + "*" + confMgr->factorySamplesDir(), FileItem::defaultFilters(), tr("My Samples"),
		embed::getIconPixmap("sample_file").transformed(QTransform().rotate(90)), splitter, false,
		confMgr->userSamplesDir(), confMgr->factorySamplesDir()));

	sideBar->appendTab(new FileBrowser(FileBrowser::Type::Normal,
		confMgr->userPresetsDir() + "*" + confMgr->factoryPresetsDir(), "*.xpf *.cs.xml *.xiz *.lv2", tr("My Presets"),
		embed::getIconPixmap("preset_file").transformed(QTransform().rotate(90)), splitter, false,
		confMgr->userPresetsDir(), confMgr->factoryPresetsDir()));

	sideBar->appendTab(new FileBrowser(FileBrowser::Type::Normal, QDir::homePath(), FileItem::defaultFilters(),
		tr("My Home"), embed::getIconPixmap("home").transformed(QTransform().rotate(90)), splitter, false));

	QStringList root_paths;
	QString title = tr("Root Directory");
	bool dirs_as_items = false;

#ifdef LMMS_BUILD_APPLE
	title = tr( "Volumes" );
	root_paths += "/Volumes";
#elif defined(LMMS_BUILD_WIN32)
	title = tr( "My Computer" );
	dirs_as_items = true;
#endif

#if ! defined(LMMS_BUILD_APPLE)
	QFileInfoList drives = QDir::drives();
	for( const QFileInfo & drive : drives )
	{
		root_paths += drive.absolutePath();
	}
#endif

	sideBar->appendTab(new FileBrowser(FileBrowser::Type::Normal, root_paths.join("*"), FileItem::defaultFilters(), title,
		embed::getIconPixmap("computer").transformed(QTransform().rotate(90)), splitter, dirs_as_items));

	m_workspace = new MovableQMdiArea(splitter);

	// Load background
	emit initProgress(tr("Loading background picture"));
	QString backgroundPicFile = ConfigManager::inst()->backgroundPicFile();
	QImage backgroundPic;
	if( !backgroundPicFile.isEmpty() )
	{
		backgroundPic = QImage( backgroundPicFile );
	}
	if( !backgroundPicFile.isNull() )
	{
		m_workspace->setBackground( backgroundPic );
	}
	else
	{
		m_workspace->setBackground( Qt::NoBrush );
	}

	m_workspace->setOption( QMdiArea::DontMaximizeSubWindowOnActivation );
	m_workspace->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_workspace->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	hbox->addWidget(sideBar);
	hbox->addWidget(splitter);
	// If the user wants the sidebar on the right, we move the workspace and
	// the splitter to the "left" side, or the first widgets in their list
	if (sideBarOnRight)
	{
		splitter->insertWidget(0, m_workspace);
		hbox->insertWidget(0, splitter);
	}

	// create global-toolbar at the top of our window
	m_toolBar = new QWidget( main_widget );
	m_toolBar->setObjectName( "mainToolbar" );
	m_toolBar->setFixedHeight( 64 );
	m_toolBar->move( 0, 0 );

	// add layout for organizing quite complex toolbar-layouting
	m_toolBarLayout = new QGridLayout( m_toolBar/*, 2, 1*/ );
	m_toolBarLayout->setContentsMargins(0, 0, 0, 0);
	m_toolBarLayout->setSpacing( 0 );

	vbox->addWidget( m_toolBar );
	vbox->addWidget( w );
	setCentralWidget( main_widget );

#ifdef LMMS_BUILD_WIN32
	auto* scanStatus = statusBar();
	scanStatus->setSizeGripEnabled(false);
	auto* scanLabel = new QLabel(scanStatus);
	scanLabel->setTextFormat(Qt::PlainText);
	scanLabel->setMaximumWidth(420);
	auto* scanBar = new QProgressBar(scanStatus);
	scanBar->setFixedWidth(100);
	scanBar->setAccessibleName(tr("VST scan progress"));
	scanStatus->addPermanentWidget(scanLabel);
	scanStatus->addPermanentWidget(scanBar);
	scanStatus->hide();
	auto* scanTimer = new QTimer(scanStatus);
	connect(scanTimer, &QTimer::timeout, scanStatus, [scanStatus, scanLabel, scanBar] {
		const auto* jobs = Engine::vstCatalog();
		const auto current = jobs ? jobs->scanProgress() : std::nullopt;
		scanStatus->setVisible(current.has_value());
		if (!current)
		{
			return;
		}
		const auto& progress = *current;
		if (progress.total == 0)
		{
			scanLabel->setText(tr("Finding VST plug-ins…"));
			scanLabel->setToolTip({});
			scanBar->setRange(0, 0);
			return;
		}
		const auto module = tr("Module: %1 (%2/%3)")
			.arg(progress.path, QString::number(progress.completed), QString::number(progress.total));
		const auto plugin = progress.pluginName.isEmpty() ? QFileInfo(progress.path).fileName() : progress.pluginName;
		const auto stage = progress.stage == vsthost::PluginCatalog::ScanStage::ExportingPresets
			? tr("Exporting presets: %1") : tr("Scanning VST: %1");
		auto activity = stage.arg(plugin);
		if (progress.pluginsTotal > 0)
		{
			activity += tr(" (%1/%2 processed)").arg(progress.pluginsCompleted).arg(progress.pluginsTotal);
		}
		scanLabel->setText(activity);
		scanLabel->setToolTip(activity + '\n' + module);
		scanBar->setToolTip(module);
		scanBar->setRange(0, static_cast<int>(progress.total));
		scanBar->setValue(static_cast<int>(progress.completed));
	});
	scanTimer->start(150);
#endif

	m_updateTimer.start( 1000 / 60, this );  // 60 fps

	if( ConfigManager::inst()->value( "ui", "enableautosave" ).toInt() )
	{
		// connect auto save
		connect(&m_autoSaveTimer, SIGNAL(timeout()), this, SLOT(autoSave()));
		m_autoSaveInterval = ConfigManager::inst()->value(
					"ui", "saveinterval" ).toInt() < 1 ?
						DEFAULT_AUTO_SAVE_INTERVAL :
				ConfigManager::inst()->value(
					"ui", "saveinterval" ).toInt();

		// The auto save function mustn't run until there is a project
		// to save or it will run over recover.mmp if you hesitate at the
		// recover messagebox for a minute. It is now started in main.
		// See autoSaveTimerReset() in MainWindow.h
	}

	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
				this, SLOT(updatePlayPauseIcons()));

	connect(Engine::getSong(), SIGNAL(modified()), SLOT(onSongModified()));
	connect(Engine::getSong(), SIGNAL(projectFileNameChanged()), SLOT(onProjectFileNameChanged()));

	maximized = isMaximized();
	new QShortcut(QKeySequence(Qt::Key_F11), this, SLOT(toggleFullscreen()));

	if (ConfigManager::inst()->value("tooltips", "disabled").toInt())
	{
		qApp->installEventFilter(this);
	}

	installEventFilter(this);
}




MainWindow::~MainWindow()
{
	for( PluginView *view : m_tools )
	{
		delete view->model();
		delete view;
	}
	// TODO: Close tools
	// dependencies are such that the editors must be destroyed BEFORE Song is deletect in Engine::destroy
	//   see issue #2015 on github
	delete getGUI()->automationEditor();
	delete getGUI()->pianoRoll();
	delete getGUI()->songEditor();
	// destroy engine which will do further cleanups etc.
	Engine::destroy();
}




void MainWindow::finalize()
{
	resetWindowTitle();
	setWindowIcon( embed::getIconPixmap( "icon_small" ) );

	auto addAction = [this](QMenu* menu, std::string_view icon, const QString& text,
		const QKeySequence& shortcut, auto(MainWindow::* slot)()) -> QAction*
	{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 3, 0))
		return menu->addAction(embed::getIconPixmap(icon), text, shortcut, this, slot);
#else
		return menu->addAction(embed::getIconPixmap(icon), text, this, slot, shortcut);
#endif
	};

	// project-popup-menu
	auto project_menu = new QMenu(this);
	menuBar()->addMenu( project_menu )->setText( tr( "&File" ) );

	addAction(project_menu, "project_new", tr("&New"),
		QKeySequence::New, &MainWindow::createNewProject);

	project_menu->addAction(tr("New Node Workflow Project"), this, [this] {
		if (mayChangeProject(true))
		{
			Engine::getSong()->createNodeProject();
			showNodeWorkflow();
		}
	});
	auto* nodeEditorAction = project_menu->addAction(tr("Node Editor"), this, &MainWindow::showNodeWorkflow);
	connect(project_menu, &QMenu::aboutToShow, nodeEditorAction, [nodeEditorAction] {
		nodeEditorAction->setVisible(Engine::getSong()->isNodeProject());
	});
	auto* modeMenu = project_menu->addMenu(tr("Workspace Mode"));
	auto* modes = new QActionGroup(modeMenu);
	auto* classicMode = modeMenu->addAction(tr("Classic Workspace"));
	auto* nodeMode = modeMenu->addAction(tr("Node Workspace"));
	for (auto* mode : {classicMode, nodeMode})
	{
		mode->setCheckable(true);
		modes->addAction(mode);
	}
	classicMode->setChecked(true);
	connect(modeMenu, &QMenu::aboutToShow, this, [classicMode, nodeMode] {
		(Engine::getSong()->isNodeProject() ? nodeMode : classicMode)->setChecked(true);
	});
	connect(classicMode, &QAction::triggered, this, [this] {
		auto* song = Engine::getSong();
		if (song->isNodeProject())
		{
			const auto changes = song->classicConversionChanges();
			if (!changes.isEmpty() && QMessageBox::warning(this, tr("Convert to Classic Project"),
				tr("The classic copy will use these settings:") + "\n\n" + changes.join("\n"),
				QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
			{
				return;
			}
			if (!saveWorkspaceCopy(false))
			{
				return;
			}
		}
		if (auto* editor = m_workspace->findChild<SubWindow*>("nodeWorkflowEditor"))
		{
			editor->hide();
		}
	});
	const auto enterNodeWorkspace = [this] {
		if (Engine::getSong()->isNodeProject() || saveWorkspaceCopy(true))
		{
			showNodeWorkflow();
		}
	};
	connect(nodeMode, &QAction::triggered, this, enterNodeWorkspace);
	project_menu->addAction(tr("Import Classic Project as Node Project…"), this,
		[this] { importClassicProjectAsNode(); });


	auto templates_menu = new TemplatesMenu( this );
	project_menu->addMenu(templates_menu);

	addAction(project_menu, "project_open", tr("&Open..."),
		QKeySequence::Open, &MainWindow::openProject);

	project_menu->addMenu(new RecentProjectsMenu(this));

	addAction(project_menu, "project_save", tr("&Save"),
		QKeySequence::Save, &MainWindow::saveProject);

	addAction(project_menu, "project_save", tr("Save &As..."),
		keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_S), &MainWindow::saveProjectAs);

	addAction(project_menu, "project_save", tr("Save as New &Version"),
		keySequence(Qt::CTRL, Qt::ALT, Qt::Key_S), &MainWindow::saveProjectAsNewVersion);

	project_menu->addAction(embed::getIconPixmap("project_save"), tr("Save as default template"),
		this, &MainWindow::saveProjectAsDefaultTemplate);

	project_menu->addSeparator();

	project_menu->addAction(embed::getIconPixmap("project_import"), tr("Import..."),
		this, &MainWindow::onImportProject);

	auto* svsProjectMenu = project_menu->addMenu(QCoreApplication::translate("SVSProjectUI", "SVS project"));
	svsProjectMenu->setObjectName("svsProjectMenu");
	auto* svsProjectController = new SVSProjectController(this);
	svsProjectMenu->addAction(QCoreApplication::translate("SVSProjectUI", "Import SVS project"), svsProjectController,
		&SVSProjectController::importProject);
	svsProjectMenu->addAction(QCoreApplication::translate("SVSProjectUI", "Export SVS project"), svsProjectController,
		&SVSProjectController::exportProject);

	addAction(project_menu, "project_export", tr("E&xport..."), keySequence(Qt::CTRL, Qt::Key_E),
		&MainWindow::onExportProject);

	addAction(project_menu, "project_export", tr("Export &Tracks..."),
		keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_E), &MainWindow::onExportProjectTracks);

	addAction(project_menu, "midi_file", tr("Export &MIDI..."),
		keySequence(Qt::CTRL, Qt::Key_M), &MainWindow::onExportProjectMidi);

	project_menu->addSeparator();

	project_menu->addAction(embed::getIconPixmap("exit"), tr("&Quit"),
		qApp, SLOT(closeAllWindows()))->setShortcut(keySequence(Qt::CTRL, Qt::Key_Q));

	auto edit_menu = new QMenu(this);
	menuBar()->addMenu( edit_menu )->setText( tr( "&Edit" ) );

	m_undoAction = addAction(edit_menu, "edit_undo", tr("Undo"),
		QKeySequence::Undo, &MainWindow::undo);

	m_redoAction = addAction(edit_menu, "edit_redo", tr("Redo"),
		QKeySequence::Redo, &MainWindow::redo);

	m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
	m_redoAction->setShortcutContext(Qt::ApplicationShortcut);

	edit_menu->addSeparator();
	edit_menu->addAction(embed::getIconPixmap("microtuner"), tr("Scales and keymaps"),
		this, SLOT(toggleMicrotunerWin()));
	edit_menu->addAction(embed::getIconPixmap("setup_general"), tr("Settings"),
		this, SLOT(showSettingsDialog()));

	connect(edit_menu, SIGNAL(aboutToShow()), this, SLOT(updateUndoRedoButtons()));

	m_nodeEditorAction = new QAction(QIcon(embed::getIconPixmap("node_editor")), tr("Show Node Editor"), this);
	m_nodeEditorAction->setObjectName("nodeWorkflowToggleAction");
	connect(m_nodeEditorAction, &QAction::triggered, this, [this] {
		auto* editor = m_workspace->findChild<SubWindow*>("nodeWorkflowEditor");
		if (editor && editor->isVisible())
		{
			editor->hide();
		}
		else
		{
			showNodeWorkflow();
		}
		updateNodeEditorAction();
	});
	auto* song = Engine::getSong();
	connect(song, &Song::projectLoaded, this, &MainWindow::updateNodeEditorAction);
	connect(song, &Song::projectFileNameChanged, this, &MainWindow::updateNodeEditorAction);
	m_viewMenu = new QMenu( this );
	menuBar()->addMenu( m_viewMenu )->setText( tr( "&View" ) );
	connect( m_viewMenu, SIGNAL(aboutToShow()),
		 this, SLOT(updateViewMenu()));
	connect( m_viewMenu, SIGNAL(triggered(QAction*)), this,
		SLOT(updateConfig(QAction*)));
	updateViewMenu();

#ifdef WANT_AGENT_CHAT
	agent::chat::attachChatDock(this, m_viewMenu);
#endif


	m_toolsMenu = new QMenu( this );
	for( const Plugin::Descriptor* desc : getPluginFactory()->descriptors(Plugin::Type::Tool) )
	{
		m_toolsMenu->addAction( desc->logo->pixmap(), desc->displayName );
		m_tools.push_back( ToolPlugin::instantiate( desc->name, /*this*/nullptr )
						   ->createView(this) );
	}
	if( !m_toolsMenu->isEmpty() )
	{
		menuBar()->addMenu( m_toolsMenu )->setText( tr( "&Tools" ) );
		connect( m_toolsMenu, SIGNAL(triggered(QAction*)),
					this, SLOT(showTool(QAction*)));
	}


	// help-popup-menu
	auto help_menu = new QMenu(this);
	menuBar()->addMenu( help_menu )->setText( tr( "&Help" ) );
	// May use offline help
	if( true )
	{
		help_menu->addAction( embed::getIconPixmap( "help" ),
						tr( "Online Help" ),
						this, SLOT(browseHelp()));
	}
	else
	{
		help_menu->addAction( embed::getIconPixmap( "help" ),
							tr( "Help" ),
							this, SLOT(help()));
	}

	help_menu->addSeparator();
	help_menu->addAction( embed::getIconPixmap( "icon_small" ), tr( "About" ),
				  this, SLOT(aboutLMMS()));

	// create tool-buttons
	auto project_new = new ToolButton(
		embed::getIconPixmap("project_new"), tr("Create new project"), this, SLOT(createNewProject()), m_toolBar);

	auto project_new_from_template = new ToolButton(embed::getIconPixmap("project_new_from_template"),
		tr("Create new project from template"), this, SLOT(emptySlot()), m_toolBar);
	project_new_from_template->setMenu( templates_menu );
	project_new_from_template->setPopupMode( ToolButton::InstantPopup );

	auto project_open = new ToolButton(
		embed::getIconPixmap("project_open"), tr("Open existing project"), this, SLOT(openProject()), m_toolBar);

	auto project_open_recent = new ToolButton(embed::getIconPixmap("project_open_recent"),
		tr("Recently opened projects"), this, SLOT(emptySlot()), m_toolBar);
	project_open_recent->setMenu( new RecentProjectsMenu(this) );
	project_open_recent->setPopupMode( ToolButton::InstantPopup );

	auto project_save = new ToolButton(
		embed::getIconPixmap("project_save"), tr("Save current project"), this, SLOT(saveProject()), m_toolBar);

	auto project_export = new ToolButton(
		embed::getIconPixmap("project_export"), tr("Export current project"), this, SLOT(onExportProject()), m_toolBar);

	m_metronomeToggle = new ToolButton(
				embed::getIconPixmap( "metronome" ),
				tr( "Metronome" ),
				this, SLOT(onToggleMetronome()),
							m_toolBar );
	m_metronomeToggle->setCheckable(true);
	m_metronomeToggle->setChecked(Engine::getSong()->metronome().active());

	m_toolBarLayout->setColumnMinimumWidth( 0, 5 );
	m_toolBarLayout->addWidget( project_new, 0, 1 );
	m_toolBarLayout->addWidget( project_new_from_template, 0, 2 );
	m_toolBarLayout->addWidget( project_open, 0, 3 );
	m_toolBarLayout->addWidget( project_open_recent, 0, 4 );
	m_toolBarLayout->addWidget( project_save, 0, 5 );
	m_toolBarLayout->addWidget( project_export, 0, 6 );
	m_toolBarLayout->addWidget( m_metronomeToggle, 0, 7 );


	// window-toolbar
	auto song_editor_window = new ToolButton(embed::getIconPixmap("songeditor"), tr("Song Editor") + " (Ctrl+1)", this,
		SLOT(toggleSongEditorWin()), m_toolBar);
	song_editor_window->setShortcut(keySequence(Qt::CTRL, Qt::Key_1));

	auto pattern_editor_window = new ToolButton(embed::getIconPixmap("pattern_track_btn"),
		tr("Pattern Editor") + " (Ctrl+2)", this, SLOT(togglePatternEditorWin()), m_toolBar);
	pattern_editor_window->setShortcut(keySequence(Qt::CTRL, Qt::Key_2));

	auto piano_roll_window = new ToolButton(
		embed::getIconPixmap("piano"), tr("Piano Roll") + " (Ctrl+3)", this, SLOT(togglePianoRollWin()), m_toolBar);
	piano_roll_window->setShortcut(keySequence(Qt::CTRL, Qt::Key_3));

	auto automation_editor_window = new ToolButton(embed::getIconPixmap("automation"),
		tr("Automation Editor") + " (Ctrl+4)", this, SLOT(toggleAutomationEditorWin()), m_toolBar);
	automation_editor_window->setShortcut(keySequence(Qt::CTRL, Qt::Key_4));

	auto mixer_window = new ToolButton(
		embed::getIconPixmap("mixer"), tr("Mixer") + " (Ctrl+5)", this, SLOT(toggleMixerWin()), m_toolBar);
	mixer_window->setShortcut(keySequence(Qt::CTRL, Qt::Key_5));

	auto controllers_window = new ToolButton(embed::getIconPixmap("controller"),
		tr("Show/hide controller rack") + " (Ctrl+6)", this, SLOT(toggleControllerRack()), m_toolBar);
	controllers_window->setShortcut(keySequence(Qt::CTRL, Qt::Key_6));

	auto project_notes_window = new ToolButton(embed::getIconPixmap("project_notes"),
		tr("Show/hide project notes") + " (Ctrl+7)", this, SLOT(toggleProjectNotesWin()), m_toolBar);
	project_notes_window->setShortcut(keySequence(Qt::CTRL, Qt::Key_7));

	m_toolBarLayout->addWidget( song_editor_window, 1, 1 );
	m_toolBarLayout->addWidget( pattern_editor_window, 1, 2 );
	m_toolBarLayout->addWidget( piano_roll_window, 1, 3 );
	m_toolBarLayout->addWidget( automation_editor_window, 1, 4 );
	m_toolBarLayout->addWidget( mixer_window, 1, 5 );
	m_toolBarLayout->addWidget( controllers_window, 1, 6 );
	m_toolBarLayout->addWidget( project_notes_window, 1, 7 );
	auto* nodeEditorButton = new ToolButton(m_toolBar);
	nodeEditorButton->setObjectName("nodeWorkflowToggleButton");
	nodeEditorButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
	nodeEditorButton->setDefaultAction(m_nodeEditorAction);
	m_toolBarLayout->addWidget(nodeEditorButton, 1, 8);
	m_toolBarLayout->setColumnStretch( 100, 1 );

	// setup-dialog opened before?
	if( !ConfigManager::inst()->value( "app", "configured" ).toInt() )
	{
		ConfigManager::inst()->setValue( "app", "configured", "1" );
		// no, so show it that user can setup everything
		SetupDialog sd;
		sd.exec();
	}
	// look whether the audio engine failed to start the audio device selected by the
	// user and is using AudioDummy as a fallback
	// or the audio device is set to invalid one
	else if( Engine::audioEngine()->audioDevStartFailed() || !AudioEngine::isAudioDevNameValid(
		ConfigManager::inst()->value( "audioengine", "audiodev" ) ) )
	{
		QMessageBox::critical(nullptr, "Audio device setup failed",
			tr("Failed to setup audio device for playback. Try adjusting your audio device settings (e.g. the sample rate), then restart LMMS."));

		// if so, offer the audio settings section of the setup dialog
		SetupDialog sd( SetupDialog::ConfigTab::AudioSettings );
		sd.exec();
	}

	// Add editor subwindows
	addWindowedWidget(getGUI()->automationEditor());
	addWindowedWidget(getGUI()->patternEditor());
	addWindowedWidget(getGUI()->pianoRoll());
	addWindowedWidget(getGUI()->songEditor());

	getGUI()->automationEditor()->parentWidget()->hide();
	getGUI()->patternEditor()->parentWidget()->move(610, 5);
	getGUI()->patternEditor()->parentWidget()->hide();
	getGUI()->pianoRoll()->parentWidget()->move(5, 5);
	getGUI()->pianoRoll()->parentWidget()->hide();
	getGUI()->songEditor()->parentWidget()->move(5, 5);
	getGUI()->songEditor()->parentWidget()->show();

	// reset window title every time we change the state of a subwindow to show the correct title
	for( const QMdiSubWindow * subWindow : workspace()->subWindowList() )
	{
		connect( subWindow, SIGNAL(windowStateChanged(Qt::WindowStates,Qt::WindowStates)), this, SLOT(resetWindowTitle()));
	}
}


int MainWindow::addWidgetToToolBar( QWidget * _w, int _row, int _col )
{
	int col = ( _col == -1 ) ? m_toolBarLayout->columnCount() + 7 : _col;
	if( _w->height() > 32 || _row == -1 )
	{
		m_toolBarLayout->addWidget( _w, 0, col, 2, 1 );
	}
	else
	{
		m_toolBarLayout->addWidget( _w, _row, col );
	}
	return( col );
}




void MainWindow::addSpacingToToolBar( int _size )
{
	m_toolBarLayout->setColumnMinimumWidth( m_toolBarLayout->columnCount() +
								7, _size );
}




namespace
{

class TriggerLcdText final : public QLabel
{
public:
	TriggerLcdText(const QString& text, const QFont& font, int width, QWidget* parent, bool expand = false)
		: QLabel(text, parent)
	{
		setObjectName("nodeWorkflowTriggerLcdText");
		setTextFormat(Qt::PlainText);
		setFont(font);
		if (expand)
		{
			setMinimumWidth(width);
			setSizePolicy(QSizePolicy::Expanding, sizePolicy().verticalPolicy());
		}
		else
		{
			setFixedWidth(width);
		}
		connect(&m_timer, &QTimer::timeout, this, [this] {
			update();
		});
	}

protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		if (overflow() > 0)
		{
			m_timer.start(45);
		}
		else
		{
			m_timer.stop();
		}
	}

	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), text().isEmpty() ? QColor("#101C14") : QColor("#13261A"));
		painter.setPen(QColor("#36533B"));
		painter.drawLine(width() - 1, 0, width() - 1, height() - 1);
		painter.setClipRect(rect().adjusted(5, 0, -6, -1));
		painter.setPen(QColor("#9DE0A4"));
		// Rows can change every playback refresh. Keep the marquee phase across row rebuilds.
		static const auto clock = [] {
			QElapsedTimer timer;
			timer.start();
			return timer;
		}();
		const int excess = overflow();
		const int offset = excess > 0 ? std::clamp(
			static_cast<int>((clock.elapsed() / 45) % (excess + 16)) - 8, 0, excess) : 0;
		setProperty("lcdScrollOffset", offset);
		painter.drawText(5 - offset,
			(height() - fontMetrics().height()) / 2 + fontMetrics().ascent(), text());
	}

private:
	int overflow() const
	{
		return fontMetrics().horizontalAdvance(text()) - (width() - 11);
	}

	QTimer m_timer;
};


class TriggerNoteStrip final : public QWidget
{
public:
	TriggerNoteStrip(QList<QRectF> notes, QWidget* parent)
		: QWidget(parent)
		, m_notes(std::move(notes))
	{
		setObjectName("nodeWorkflowTriggerNotePreview");
		setMinimumWidth(104);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	}

	void movePlayhead(qreal position)
	{
		if (m_position != position)
		{
			m_position = position;
			setProperty("playheadPosition", position);
			update();
		}
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), QColor("#102017"));
		QPen notePen(QColor("#79CC91"));
		notePen.setCosmetic(true);
		painter.setPen(notePen);
		const int center = rect().center().x();
		const qreal scale = width() - 4;
		const qreal origin = m_position >= 0 ? center - m_position * scale : 2;
		for (const auto& note : m_notes)
		{
			const qreal y = 4 + note.center().y() * (height() - 8);
			painter.drawLine(QPointF(origin + note.left() * scale, y), QPointF(origin + note.right() * scale, y));
		}
		if (m_position >= 0)
		{
			const int x = center;
			painter.setPen(QColor("#E9D88A"));
			painter.drawLine(x, 3, x, height() - 4);
			painter.drawLine(x - 2, 3, x + 2, 3);
		}
	}

private:
	QList<QRectF> m_notes;
	qreal m_position = -1;
};


class TriggerStepStrip final : public QWidget
{
public:
	TriggerStepStrip(QList<bool> steps, QWidget* parent)
		: QWidget(parent), m_steps(std::move(steps))
	{
		setObjectName("nodeWorkflowTriggerStepStrip");
		setMinimumWidth(157);
		setFixedHeight(22);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	}

	void movePlayhead(int step, bool fired)
	{
		if (m_cursor == step && m_fired == fired)
		{
			return;
		}
		m_cursor = step;
		m_fired = fired;
		setProperty("stepIndex", step);
		setAccessibleDescription(QString::number(step + 1));
		update();
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		QPainter painter(this);
		painter.fillRect(event->rect(), QColor("#102017"));
		const int origin = m_cursor >= 0 ? rect().center().x() - 3 - m_cursor * StepWidth : 4;
		const int first = std::max(0, (event->rect().left() - origin - 7) / StepWidth);
		const int last = std::min(static_cast<int>(m_steps.size()) - 1,
			(event->rect().right() - origin) / StepWidth);
		for (int step = first; step <= last; ++step)
		{
			const int x = origin + step * StepWidth;
			const bool selected = step == m_cursor;
			painter.setPen(selected ? QColor("#E9D88A") : QColor("#3E634C"));
			painter.setBrush(m_steps.at(step) || (selected && m_fired)
				? QColor("#79CC91") : QColor("#203A2B"));
			painter.drawRoundedRect(QRect(x, 7, 7, 9), 1, 1);
			if (selected)
			{
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor("#E9D88A"));
				painter.drawPolygon(QPolygon{QPoint(x + 1, 1), QPoint(x + 6, 1), QPoint(x + 3, 6)});
			}
		}
	}

private:
	static constexpr int StepWidth = 10;
	QList<bool> m_steps;
	int m_cursor = -1;
	bool m_fired = false;
};

} // namespace

static AudioBusHandle* nodeWorkflowAudioBus(Track& track)
{
	switch (track.type())
	{
		case Track::Type::Instrument:
			return static_cast<InstrumentTrack&>(track).audioBusHandle();
		case Track::Type::Sample:
			return static_cast<SampleTrack&>(track).audioBusHandle();
		case Track::Type::SVS:
			return static_cast<SVSTrack&>(track).audioBusHandle();
		case Track::Type::SVC:
			return static_cast<SVCTrack&>(track).audioBusHandle();
		default:
			return nullptr;
	}
}

bool MainWindow::saveWorkspaceCopy(bool nodeFormat)
{
	auto* song = Engine::getSong();
	if (!nodeFormat && song->hasExtendedSourceGraphs())
	{
		QMessageBox::warning(this, tr("Classic Project Unavailable"),
			tr("Conversion was refused because the classic project format cannot store nodes and routing added inside track subgraphs."));
		return false;
	}
	const QFileInfo source(song->projectFileName());
	const bool compressed = ConfigManager::inst()->value("app", "nommpz").toInt() == 0;
	const QString suffix = nodeFormat
		? (compressed ? "mmpnz" : "mmpn")
		: (compressed ? "mmpz" : "mmp");
	FileDialog dialog(this, nodeFormat ? tr("Save Node Project") : tr("Save Classic Project"),
		source.filePath().isEmpty() ? ConfigManager::inst()->userProjectsDir() : source.absolutePath(),
		nodeFormat ? tr("LMMS Node Workflow Project (*.mmpnz *.mmpn)") : tr("LMMS Classic Projects (*.mmp *.mmpz)"));
	dialog.setAcceptMode(FileDialog::AcceptSave);
	dialog.setDefaultSuffix(suffix);
	if (!source.filePath().isEmpty())
	{
		dialog.selectFile(source.completeBaseName() + "." + suffix);
	}
	if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
	{
		return false;
	}
	const auto fileName = dialog.selectedFiles().front();
	const bool saved = nodeFormat ? song->convertToNodeProject(fileName) : song->convertToClassicProject(fileName);
	handleSaveResult(fileName, saved);
	return saved;
}

bool MainWindow::canImportAsClassicProject(const QString& path)
{
	DataFile source(path);
	if (Song::hasExtendedSourceGraphs(source.nodeWorkflow(), source.content()))
	{
		QMessageBox::warning(this, tr("Classic Project Unavailable"),
			tr("Import was refused because the classic project format cannot store nodes and routing added inside track subgraphs."));
		return false;
	}
	return true;
}

void MainWindow::importClassicProjectAsNode(const QString& path)
{
	QString file = path;
	if (file.isEmpty())
	{
		FileDialog dialog(this, tr("Import Classic Project"), ConfigManager::inst()->userProjectsDir(),
			tr("LMMS Classic Projects (*.mmp *.mmpz)"));
		dialog.setFileMode(FileDialog::ExistingFile);
		if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
		{
			return;
		}
		file = dialog.selectedFiles().front();
	}
	if (!canImportAsClassicProject(file) || !mayChangeProject(false))
	{
		return;
	}
	const auto extension = QFileInfo(file).suffix().toLower();
	if (extension != "mmp" && extension != "mmpz")
	{
		return;
	}
	auto* song = Engine::getSong();
	bool imported = false;
	song->stop();
	{
		const auto loaded = connect(song, &Song::projectLoaded, this, [&imported] { imported = true; });
		const auto restore = qScopeGuard([this, loaded] {
			disconnect(loaded);
			setCursor(Qt::ArrowCursor);
		});
		setCursor(Qt::WaitCursor);
		song->loadProject(file);
	}
	if (imported && (song->isNodeProject() || saveWorkspaceCopy(true)))
	{
		showNodeWorkflow();
	}
}

void MainWindow::showNodeWorkflow()
{
	auto* song = Engine::getSong();
	if (!song->isNodeProject())
	{
		return;
	}
	if (auto* editor = m_workspace->findChild<SubWindow*>("nodeWorkflowEditor"))
	{
		editor->show();
		m_workspace->setActiveSubWindow(editor);
		return;
	}

	auto* content = new QWidget;
	content->setObjectName("nodeWorkflowContent");
	content->setWindowTitle(tr("Node Editor"));
	auto* layout = new QVBoxLayout(content);
	auto* controls = new QHBoxLayout;
	const auto makeTool = [content](std::string_view icon, const QString& label) {
		auto* button = new QToolButton(content);
		button->setProperty("nodeWorkflowTool", true);
		button->setIcon(embed::getIconPixmap(icon, 16, 16));
		button->setToolTip(label);
		button->setAccessibleName(label);
		return button;
	};
	auto* back = makeTool("node_root", tr("Root Graph"));
	auto* fit = makeTool("node_fit", tr("Fit Canvas"));
	auto* add = makeTool("node_add", tr("Add Node"));
	add->setObjectName("nodeWorkflowAdd");
	auto* signalFlow = makeTool("node_signal", tr("Signal Flow"));
	signalFlow->setObjectName("nodeWorkflowSignalFlow");
	signalFlow->setCheckable(true);
	auto* canvas = new NodeWorkflowCanvas(content);
	canvas->setObjectName("nodeWorkflowCanvas");
	canvas->setLayoutHandlers([song](const QString& key) { return song->nodeLayoutPosition(key); },
		[song](const QString& key, const QPointF& position) { song->setNodeLayoutPosition(key, position); });
	controls->addWidget(add);
	controls->addWidget(back);
	controls->addWidget(fit);
	auto* breadcrumb = new QLabel(content);
	breadcrumb->setObjectName("nodeWorkflowBreadcrumb");
	breadcrumb->setTextFormat(Qt::RichText);
	breadcrumb->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
	breadcrumb->setFocusPolicy(Qt::StrongFocus);
	controls->addWidget(breadcrumb);
	const auto setBreadcrumb = [breadcrumb](const QList<QPair<QString, QString>>& segments) {
		breadcrumb->ensurePolished();
		const auto linkColor = breadcrumb->palette().color(QPalette::WindowText).name();
		QStringList parts;
		for (const auto& [destination, label] : segments)
		{
			const auto text = label.toHtmlEscaped();
			parts.append(destination.isEmpty() ? text
				: QStringLiteral("<a href=\"%1\" style=\"color: %2\">%3</a>").arg(destination, linkColor, text));
		}
		breadcrumb->setText(parts.join(QStringLiteral(" &nbsp;›&nbsp; ")));
	};
	controls->addStretch();
	auto* play = makeTool("play", tr("Play Song"));
	auto* stop = makeTool("stop", tr("Stop Song"));
	play->setObjectName("nodeWorkflowPlay");
	stop->setObjectName("nodeWorkflowStop");
	auto* songEditor = getGUI()->songEditor();
	connect(play, &QToolButton::clicked, songEditor->playAction(), &QAction::trigger);
	connect(stop, &QToolButton::clicked, songEditor, [songEditor] {
		QMetaObject::invokeMethod(songEditor, "stop");
	});
	const auto refreshTransport = [this, song, play, stop] {
		const bool playingSong = song->isPlaying() && song->playMode() == Song::PlayMode::Song;
		const auto label = playingSong ? tr("Pause Song") : tr("Play Song");
		play->setIcon(embed::getIconPixmap(playingSong ? "pause" : "play", 16, 16));
		play->setToolTip(label);
		play->setAccessibleName(label);
		stop->setEnabled(song->isPlaying() || song->isPaused());
	};
	connect(song, &Song::playbackStateChanged, play, refreshTransport);
	refreshTransport();
	controls->addWidget(play);
	controls->addWidget(stop);
	auto* aggregate = makeTool("agent_chat_settings", tr("Aggregation Settings"));
	aggregate->setObjectName("nodeWorkflowAggregation");
	auto* aggregationMenu = new QMenu(aggregate);
	auto* merge = aggregationMenu->addAction(tr("Merge Connections Sharing a Port"));
	merge->setObjectName("nodeWorkflowAggregationEnabled");
	merge->setCheckable(true);
	auto* pcb = aggregationMenu->addAction(tr("PCB Bus Routing"));
	pcb->setObjectName("nodeWorkflowPcbBusRouting");
	pcb->setCheckable(true);
	auto* thresholdAction = new QWidgetAction(aggregationMenu);
	auto* thresholdRow = new QWidget(aggregationMenu);
	auto* thresholdLayout = new QHBoxLayout(thresholdRow);
	thresholdLayout->addWidget(new QLabel(tr("Connections Greater Than"), thresholdRow));
	auto* threshold = new QSpinBox(thresholdRow);
	threshold->setObjectName("nodeWorkflowAggregationThreshold");
	threshold->setAccessibleName(tr("Connections Greater Than"));
	threshold->setRange(1, 9999);
	thresholdLayout->addWidget(threshold);
	thresholdAction->setDefaultWidget(thresholdRow);
	aggregationMenu->addAction(thresholdAction);
	aggregate->setMenu(aggregationMenu);
	aggregate->setPopupMode(QToolButton::InstantPopup);
	auto* config = ConfigManager::inst();
	const auto restoreAggregation = [canvas, merge, pcb, threshold, config, song] {
		const QSignalBlocker blockMerge(merge);
		const QSignalBlocker blockPcb(pcb);
		const QSignalBlocker blockThreshold(threshold);
		bool validThreshold = false;
		const auto fallbackThreshold = config->value("ui", "nodeaggregatethreshold").toInt(&validThreshold);
		const auto [enabled, count] = song->nodeConnectionAggregation().value_or(qMakePair(
			config->value("ui", "nodeaggregate").toInt() != 0, validThreshold ? fallbackThreshold : 4));
		merge->setChecked(enabled);
		threshold->setValue(count);
		canvas->setConnectionAggregation(enabled, threshold->value());
		pcb->setChecked(song->nodePcbBusRouting().value_or(true));
		canvas->setPcbBusRouting(pcb->isChecked());
	};
	const auto applyAggregation = [canvas, merge, threshold, config, song] {
		canvas->setConnectionAggregation(merge->isChecked(), threshold->value());
		song->setNodeConnectionAggregation(merge->isChecked(), threshold->value());
		config->setValue("ui", "nodeaggregate", merge->isChecked() ? "1" : "0");
		config->setValue("ui", "nodeaggregatethreshold", QString::number(threshold->value()));
	};
	connect(merge, &QAction::toggled, canvas, applyAggregation);
	connect(pcb, &QAction::toggled, canvas, [canvas, song](bool enabled) {
		canvas->setPcbBusRouting(enabled);
		song->setNodePcbBusRouting(enabled);
	});
	connect(threshold, qOverload<int>(&QSpinBox::valueChanged), canvas, applyAggregation);
	connect(song, &Song::projectLoaded, aggregate, restoreAggregation);
	restoreAggregation();
	controls->addWidget(aggregate);
	auto* signalMenu = new QMenu(signalFlow);
	auto* silenceAction = new QWidgetAction(signalMenu);
	auto* silenceRow = new QWidget(signalMenu);
	auto* silenceLayout = new QHBoxLayout(silenceRow);
	const auto silenceLabel = tr("Audio Silence Threshold");
	silenceLayout->addWidget(new QLabel(silenceLabel, silenceRow));
	auto* silenceThreshold = new QDoubleSpinBox(silenceRow);
	silenceThreshold->setObjectName("nodeWorkflowAudioSilenceThreshold");
	silenceThreshold->setAccessibleName(silenceLabel);
	silenceThreshold->setDecimals(1);
	silenceThreshold->setRange(-120.0, 0.0);
	silenceThreshold->setSuffix(QStringLiteral(" dBFS"));
	bool validSilenceThreshold = false;
	const auto savedSilenceThreshold = config->value("ui", "nodeaudiosilencedb")
		.toDouble(&validSilenceThreshold);
	silenceThreshold->setValue(validSilenceThreshold && std::isfinite(savedSilenceThreshold)
		? savedSilenceThreshold : DefaultNodeAudioSilenceDb);
	setNodeAudioSilenceDb(silenceThreshold->value());
	silenceLayout->addWidget(silenceThreshold);
	silenceAction->setDefaultWidget(silenceRow);
	signalMenu->addAction(silenceAction);
	auto* triggerHideAction = new QWidgetAction(signalMenu);
	auto* triggerHideRow = new QWidget(signalMenu);
	auto* triggerHideLayout = new QHBoxLayout(triggerHideRow);
	const auto triggerHideLabel = tr("Trigger Row Hide Delay");
	triggerHideLayout->addWidget(new QLabel(triggerHideLabel, triggerHideRow));
	auto* triggerHideDelay = new QSpinBox(triggerHideRow);
	triggerHideDelay->setObjectName("nodeWorkflowTriggerHideDelay");
	triggerHideDelay->setAccessibleName(triggerHideLabel);
	triggerHideDelay->setRange(0, 30000);
	triggerHideDelay->setSingleStep(50);
	triggerHideDelay->setSuffix(QStringLiteral(" ms"));
	bool validTriggerDelay = false;
	const auto savedTriggerDelay = config->value("ui", "nodetriggerhidedelay").toInt(&validTriggerDelay);
	triggerHideDelay->setValue(validTriggerDelay ? savedTriggerDelay : NodePatternTriggers::DefaultHideDelayMs);
	NodePatternTriggers::setHideDelay(triggerHideDelay->value());
	triggerHideLayout->addWidget(triggerHideDelay);
	triggerHideAction->setDefaultWidget(triggerHideRow);
	signalMenu->addAction(triggerHideAction);
	connect(triggerHideDelay, qOverload<int>(&QSpinBox::valueChanged), triggerHideDelay, [config](int value) {
		NodePatternTriggers::setHideDelay(value);
		config->setValue("ui", "nodetriggerhidedelay", QString::number(value));
	});
	signalFlow->setMenu(signalMenu);
	signalFlow->setPopupMode(QToolButton::InstantPopup);
	auto* showSignalFlow = new QAction(tr("Show Signal Flow"), signalMenu);
	signalMenu->insertAction(silenceAction, showSignalFlow);
	showSignalFlow->setCheckable(true);
	showSignalFlow->setChecked(signalFlow->isChecked());
	connect(showSignalFlow, &QAction::toggled, signalFlow, &QToolButton::setChecked);
	connect(signalFlow, &QToolButton::toggled, showSignalFlow, &QAction::setChecked);
	connect(silenceThreshold, qOverload<double>(&QDoubleSpinBox::valueChanged), silenceThreshold, [config](double value) {
		setNodeAudioSilenceDb(value);
		config->setValue("ui", "nodeaudiosilencedb", QString::number(value));
	});
	connect(signalMenu, &QMenu::aboutToHide, signalMenu, [config] { config->saveConfigFile(); });
	controls->addWidget(signalFlow);
	auto* zoom = new QToolButton(content);
	zoom->setObjectName("nodeWorkflowZoom");
	zoom->setText(QStringLiteral("100%"));
	zoom->setAccessibleName(tr("Canvas Zoom"));
	zoom->setPopupMode(QToolButton::InstantPopup);
	auto* zoomMenu = new QMenu(zoom);
	zoomMenu->addAction(QStringLiteral("100%"), canvas, [canvas] { canvas->setZoom(1); });
	zoom->setMenu(zoomMenu);
	connect(canvas, &NodeWorkflowCanvas::zoomChanged, zoom, [zoom](qreal scale) {
		zoom->setText(QString::number(qRound(scale * 100)) + '%');
	});
	controls->addWidget(zoom);
	connect(signalFlow, &QToolButton::toggled, canvas, &NodeWorkflowCanvas::setSignalFlowVisible);
	signalFlow->setChecked(true);
	layout->addLayout(controls);
	// Keep the graph and its optional pattern row in the same column.
	auto* body = new QHBoxLayout;
	body->setSpacing(0);
	auto* graphColumn = new QVBoxLayout;
	graphColumn->setSpacing(0);
	graphColumn->addWidget(canvas, 1);
	body->addLayout(graphColumn, 1);

	auto* inspector = new QWidget(content);
	inspector->setObjectName("nodeWorkflowInspector");
	inspector->setFixedWidth(344);
	auto* inspectorLayout = new QVBoxLayout(inspector);
	inspectorLayout->setContentsMargins(17, 15, 20, 15);
	inspectorLayout->setSpacing(9);
	auto* inspectorHeading = new QLabel(tr("Inspector"), inspector);
	inspectorHeading->setObjectName("nodeWorkflowInspectorHeading");
	inspectorLayout->addWidget(inspectorHeading);
	auto* inspectorTitle = new QLabel(inspector);
	inspectorTitle->setObjectName("nodeWorkflowInspectorTitle");
	inspectorTitle->setWordWrap(true);
	inspectorLayout->addWidget(inspectorTitle);
	auto* inspectorDescription = new QLabel(inspector);
	inspectorDescription->setObjectName("nodeWorkflowInspectorDescription");
	inspectorDescription->setWordWrap(true);
	inspectorLayout->addWidget(inspectorDescription);
	inspectorLayout->addSpacing(10);
	auto* inspectorFields = new QGridLayout;
	inspectorFields->setColumnStretch(1, 1);
	inspectorFields->setHorizontalSpacing(10);
	inspectorFields->setVerticalSpacing(9);
	inspectorLayout->addLayout(inspectorFields);
	inspectorLayout->addSpacing(5);
	auto* inspectorActions = new QVBoxLayout;
	inspectorActions->setSpacing(8);
	inspectorLayout->addLayout(inspectorActions);
	using TriggerKind = NodePatternTriggers::Kind;
	auto* globalTriggerTitle = new QLabel(tr("Global Triggers"), inspector);
	globalTriggerTitle->hide();
	inspectorLayout->addWidget(globalTriggerTitle);
	auto* triggerLcd = new QScrollArea(inspector);
	triggerLcd->setObjectName("nodeWorkflowTriggerLcd");
	triggerLcd->setWidgetResizable(true);
	triggerLcd->setFixedHeight(148);
	triggerLcd->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	triggerLcd->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	triggerLcd->setAccessibleName(tr("Trigger Sources"));
	triggerLcd->hide();
	inspectorLayout->addWidget(triggerLcd);
	auto* triggerTimer = new QTimer(triggerLcd);
	triggerTimer->setInterval(65);
	auto triggerTrack = std::make_shared<QPointer<Track>>();
	auto triggerAll = std::make_shared<bool>(false);
	auto triggerKind = std::make_shared<std::optional<TriggerKind>>();
	struct DisplayRow
	{
		QString pattern;
		QString type;
		QString detail;
		QList<bool> steps;
		int playhead = -1;
		bool fired = false;
		QList<QRectF> notes;
		qreal notePosition = -1;

		bool operator==(const DisplayRow& other) const
		{
			return pattern == other.pattern && type == other.type && detail == other.detail
				&& steps == other.steps && notes == other.notes;
		}
	};
	auto displayedTriggers = std::make_shared<QList<DisplayRow>>();
	const auto clearTriggers = [triggerLcd, triggerTimer, triggerTrack, triggerAll, displayedTriggers, globalTriggerTitle] {
		if (triggerTimer->isActive())
		{
			NodePatternTriggers::watch(nullptr);
			triggerTrack->clear();
			*triggerAll = false;
			triggerTimer->stop();
			displayedTriggers->clear();
			delete triggerLcd->takeWidget();
			triggerLcd->hide();
			globalTriggerTitle->hide();
		}
	};
	connect(canvas, &NodeWorkflowCanvas::connectionSelectionCleared, triggerLcd, [triggerLcd, triggerKind] {
		triggerLcd->setProperty("wireSelected", false);
		triggerKind->reset();
	});
	connect(song, &Song::projectAboutToReset, triggerLcd, clearTriggers);
	QFont triggerFont = triggerLcd->font();
	const auto language = config->value("app", "language").isEmpty()
		? QLocale().name() : config->value("app", "language");
	const auto fontRegion = language.startsWith("ja") ? QStringLiteral("ja")
		: language.startsWith("ko") ? QStringLiteral("ko")
		: language.startsWith("zh") ? QStringLiteral("zh_hans") : QStringLiteral("latin");
	const auto fontPath = config->defaultThemeDir() + QStringLiteral("/trigger-lcd/fusion-pixel-12px-monospaced-")
		+ fontRegion + QStringLiteral(".ttf");
	static QHash<QString, QStringList> triggerFontFamilies;
	if (!triggerFontFamilies.contains(fontPath))
	{
		triggerFontFamilies.insert(fontPath,
			QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(fontPath)));
	}
	const auto& fontFamilies = triggerFontFamilies[fontPath];
	if (!fontFamilies.isEmpty())
	{
		triggerFont.setFamily(fontFamilies.front());
	}
	triggerFont.setPixelSize(12);
	const auto refreshTriggers = [this, song, triggerTrack, triggerAll, triggerKind, triggerLcd,
		triggerFont, displayedTriggers, clearTriggers] {
		if (!*triggerTrack && !*triggerAll)
		{
			clearTriggers();
			return;
		}
		const bool observe = *triggerAll || (*triggerTrack)->type() == Track::Type::Instrument
			|| triggerLcd->property("wireSelected").toBool();
		auto events = !song->isStopped() && observe
			? NodePatternTriggers::recent(*triggerKind)
			: std::vector<NodePatternTriggers::Trigger>{};
		std::stable_sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
			return std::tie(left.pattern, left.kind) < std::tie(right.pattern, right.kind);
		});
		QList<DisplayRow> rows;
		for (const auto& event : events)
		{
			const auto* pattern = PatternTrack::findPatternTrack(event.pattern);
			const auto& members = Engine::patternStore()->tracks();
			const auto member = std::find(members.cbegin(), members.cend(), event.track);
			if (!pattern || member == members.cend() || event.pattern >= (*member)->numOfClips())
			{
				continue;
			}
			auto* track = *member;
			const auto patternLabel = *triggerAll ? pattern->name() + QStringLiteral(" · ") + track->name()
				: pattern->name();
			QString type;
			QStringList values;
			switch (event.kind)
			{
				case TriggerKind::Step:
				{
					auto* clip = dynamic_cast<MidiClip*>(track->getClip(event.pattern));
					if (!clip || clip->steps() <= 0)
					{
						continue;
					}
					QList<bool> steps;
					steps.reserve(clip->steps());
					for (int step = 0; step < clip->steps(); ++step)
					{
						steps.append(clip->noteAtStep(step) != nullptr);
					}
					rows.append({patternLabel, tr("Step"), {}, std::move(steps),
						NodePatternTriggers::stepIndex(event.tick), event.notes.test(0)});
					continue;
				}
				case TriggerKind::Note:
				{
					type = tr("Note");
					const QStringList names{QStringLiteral("C"), QStringLiteral("C♯"), QStringLiteral("D"),
						QStringLiteral("D♯"), QStringLiteral("E"), QStringLiteral("F"), QStringLiteral("F♯"),
						QStringLiteral("G"), QStringLiteral("G♯"), QStringLiteral("A"), QStringLiteral("A♯"),
						QStringLiteral("B")};
					for (int key = 0; key < NumKeys; ++key)
					{
						if (event.notes.test(key))
						{
							values.append(names.at(key % KeysPerOctave)
								+ QString::number(key / KeysPerOctave + FirstOctave));
						}
					}
					break;
				}
				case TriggerKind::Audio:
				{
					type = tr("Audio");
					const auto* clip = dynamic_cast<SampleClip*>(track->getClip(event.pattern));
					if (!clip)
					{
						continue;
					}
					const auto sample = QFileInfo(clip->sampleFile()).fileName();
					values.append(sample.isEmpty() ? clip->name() : sample);
					break;
				}
			}
			DisplayRow row{patternLabel, type, values.join(QStringLiteral(" / "))};
			if (event.kind == TriggerKind::Note)
			{
				const auto* clip = dynamic_cast<const MidiClip*>(track->getClip(event.pattern));
				if (!clip || values.isEmpty())
				{
					continue;
				}
				row.notes = MidiClipView::notePreviewGeometry(*clip);
				if (event.playbackTick && !song->isStopped())
				{
					const auto length = std::max(1, clip->length().getTicks());
					if (*event.playbackTick >= 0 && *event.playbackTick < length)
					{
						row.notePosition = static_cast<qreal>(*event.playbackTick) / length;
					}
				}
			}
			rows.append(std::move(row));
		}
		if (rows == *displayedTriggers && triggerLcd->widget())
		{
			const auto strips = triggerLcd->widget()->findChildren<QWidget*>("nodeWorkflowTriggerStepStrip");
			const auto previews = triggerLcd->widget()->findChildren<QWidget*>("nodeWorkflowTriggerNotePreview");
			qsizetype stepIndex = 0;
			qsizetype noteIndex = 0;
			for (const auto& row : rows)
			{
				if (!row.steps.isEmpty())
				{
					static_cast<TriggerStepStrip*>(strips.at(stepIndex++))->movePlayhead(row.playhead, row.fired);
				}
				if (!row.notes.isEmpty())
				{
					static_cast<TriggerNoteStrip*>(previews.at(noteIndex++))->movePlayhead(row.notePosition);
				}
			}
			return;
		}
		const int scrollPosition = triggerLcd->verticalScrollBar()->value();
		auto* list = new QWidget;
		list->setObjectName(rows.isEmpty() ? "nodeWorkflowTriggerLcdIdle" : "nodeWorkflowTriggerLcdRows");
		auto* rowsLayout = new QVBoxLayout(list);
		rowsLayout->setContentsMargins(0, 0, 0, 0);
		rowsLayout->setSpacing(0);
		constexpr int RowHeight = 23;
		list->setMinimumHeight(static_cast<int>(rows.size()) * RowHeight);
		const auto addRow = [&](const DisplayRow& trigger) {
			auto* row = new QWidget(list);
			row->setObjectName("nodeWorkflowTriggerStepRow");
			auto* layout = new QHBoxLayout(row);
			layout->setContentsMargins(0, 0, 0, 1);
			layout->setSpacing(0);
			auto* pattern = new TriggerLcdText(trigger.pattern, triggerFont, 86, row);
			pattern->setProperty("nodeWorkflowTriggerLcdRow", true);
			layout->addWidget(pattern);
			layout->addWidget(new TriggerLcdText(trigger.type, triggerFont, 45, row));

			if (!trigger.steps.isEmpty())
			{
				auto* strip = new TriggerStepStrip(trigger.steps, row);
				strip->movePlayhead(trigger.playhead, trigger.fired);
				layout->addWidget(strip, 1);
			}
			else
			{
				const bool hasPreview = !trigger.notes.isEmpty();
				auto* detail = new TriggerLcdText(trigger.detail, triggerFont, hasPreview ? 53 : 157, row, !hasPreview);
				detail->setObjectName("nodeWorkflowTriggerLcdDetail");
				layout->addWidget(detail, hasPreview ? 0 : 1);
				if (hasPreview)
				{
					auto* preview = new TriggerNoteStrip(trigger.notes, row);
					preview->movePlayhead(trigger.notePosition);
					layout->addWidget(preview, 1);
				}
			}
			row->setFixedHeight(RowHeight);
			rowsLayout->addWidget(row);
		};
		for (const auto& row : rows)
		{
			addRow(row);
		}
		rowsLayout->addStretch();
		triggerLcd->setWidget(list);
		triggerLcd->verticalScrollBar()->setValue(scrollPosition);
		*displayedTriggers = std::move(rows);
	};
	connect(triggerTimer, &QTimer::timeout, triggerLcd, refreshTriggers);
	connect(song, &Song::playbackStateChanged, triggerLcd, [song, triggerTrack, triggerAll, refreshTriggers] {
		if ((*triggerTrack || *triggerAll) && song->isStopped())
		{
			NodePatternTriggers::watch(triggerTrack->data(), *triggerAll);
			refreshTriggers();
		}
	});
	const auto inspectTriggers = [triggerTrack, triggerAll, triggerKind, triggerLcd,
		triggerTimer, refreshTriggers, globalTriggerTitle]
		(Track* track, std::optional<TriggerKind> kind, bool wireSelected, bool all = false) {
		if (triggerTrack->data() != track || *triggerAll != all)
		{
			*triggerTrack = track;
			*triggerAll = all;
			NodePatternTriggers::watch(track, all);
		}
		*triggerKind = kind;
		triggerLcd->setProperty("wireSelected", wireSelected);
		globalTriggerTitle->setVisible(all);
		triggerLcd->show();
		triggerTimer->start();
		refreshTriggers();
	};
	inspectorLayout->addStretch();
	body->addWidget(inspector);
	layout->addLayout(body, 1);

	using InspectorField = QPair<QString, QString>;
	using InspectorAction = QPair<QString, std::function<void()>>;
	const auto setInspector = [inspector, inspectorTitle, inspectorDescription, inspectorFields, inspectorActions]
		(const QString& title, const QString& description, const QList<InspectorField>& fields,
			const QList<InspectorAction>& actions) {
		inspectorTitle->setText(title);
		inspectorDescription->setText(description);
		for (auto* layout : {static_cast<QLayout*>(inspectorFields), static_cast<QLayout*>(inspectorActions)})
		{
			while (auto* item = layout->takeAt(0))
			{
				item->widget()->hide();
				item->widget()->deleteLater();
				delete item;
			}
		}
		QList<QLabel*> values;
		int row = 0;
		for (const auto& [label, value] : fields)
		{
			auto* name = new QLabel(label, inspector);
			name->setObjectName("nodeWorkflowInspectorField");
			name->setWordWrap(true);
			auto* display = new QLabel(value, inspector);
			display->setObjectName("nodeWorkflowInspectorValue");
			display->setTextFormat(Qt::PlainText);
			display->setWordWrap(true);
			inspectorFields->addWidget(name, row, 0);
			inspectorFields->addWidget(display, row, 1);
			values.append(display);
			++row;
		}
		for (const auto& [label, action] : actions)
		{
			auto* button = new QPushButton(label, inspector);
			button->setObjectName("nodeWorkflowInspectorAction");
			button->setEnabled(static_cast<bool>(action));
			QObject::connect(button, &QPushButton::clicked, inspector, [action] {
				if (action)
				{
					action();
				}
			});
			inspectorActions->addWidget(button, 0, Qt::AlignLeft);
		}
		return values;
	};
	auto* patternPanel = new QWidget(content);
	patternPanel->setObjectName("nodeWorkflowPatternPanel");
	patternPanel->setFocusPolicy(Qt::ClickFocus);
	patternPanel->setFixedHeight(31);
	auto* patternLayout = new QHBoxLayout(patternPanel);
	patternLayout->setContentsMargins(12, 1, 26, 1);
	patternLayout->setSpacing(8);
	graphColumn->addWidget(patternPanel);
	patternPanel->hide();

	auto* rootFooter = new QWidget(content);
	rootFooter->setObjectName("nodeWorkflowFooter");
	rootFooter->setFixedHeight(36);
	auto* footerLayout = new QHBoxLayout(rootFooter);
	footerLayout->setContentsMargins(14, 0, 26, 0);
	footerLayout->setSpacing(0);
	auto* footerStatus = new QLabel(tr("Workspace  /  Root Graph"), rootFooter);
	footerLayout->addWidget(footerStatus);
	auto* invalidVstConnections = new QLabel(rootFooter);
	invalidVstConnections->hide();
	footerLayout->addWidget(invalidVstConnections);
	footerLayout->addStretch();
	const auto addLegend = [rootFooter, footerLayout](const QString& title, const QString& type) {
		auto* label = new QLabel(QStringLiteral("●  ") + title, rootFooter);
		label->setProperty("nodeWorkflowLegend", type);
		label->setFixedWidth(std::max(88, label->sizeHint().width() + 8));
		footerLayout->addWidget(label);
	};
	addLegend(tr("Audio"), "audio");
	addLegend(tr("MIDI"), "midi");
	addLegend(tr("Step Events"), "step");
	addLegend(tr("SVS Voice"), "voice");
	addLegend(tr("Aggregate Bus"), "aggregate");
	addLegend(tr("Offline"), "offline");
	layout->addWidget(rootFooter);

	const auto activeTracks = std::make_shared<const TrackContainer*>(nullptr);
	const auto activeSource = std::make_shared<QPointer<Track>>();
	const auto confirmSourceGraphExtension = [this] {
		auto* config = ConfigManager::inst();
		if (config->value("ui", "hideextendedsourcewarning").toInt() != 0)
		{
			return true;
		}
		QMessageBox warning(QMessageBox::Warning, tr("Node Project Only"),
			tr("The classic project format cannot store nodes and routing added inside track subgraphs. "
				"Adding this node will prevent conversion to a classic project."),
			QMessageBox::Ok | QMessageBox::Cancel, this);
		warning.setObjectName("nodeWorkflowSourceGraphWarning");
		auto* remember = new QCheckBox(tr("Do not show again"), &warning);
		remember->setObjectName("nodeWorkflowSkipSourceGraphWarning");
		warning.setCheckBox(remember);
		warning.setDefaultButton(QMessageBox::Cancel);
		if (warning.exec() != QMessageBox::Ok)
		{
			return false;
		}
		if (remember->isChecked())
		{
			config->setValue("ui", "hideextendedsourcewarning", "1");
			config->saveConfigFile();
		}
		return true;
	};
	const auto boundaryLabel = [this](const QString& signal) {
		if (signal == "audio") { return tr("Mono"); }
		if (signal == "step") { return tr("Step Events"); }
		if (signal == "voice") { return tr("Voice Events"); }
		return tr("MIDI");
	};
	const auto boundaryColor = [canvas](const QString& signal) {
		if (signal == "audio") { return canvas->audioPortColor(); }
		if (signal == "step") { return canvas->stepPortColor(); }
		if (signal == "voice") { return canvas->voicePortColor(); }
		return canvas->midiPortColor();
	};
	const auto boundaryPortLabel = [this, boundaryLabel](const Song::NodeBoundaryPort& port) {
		if (!port.stereoGroup.isEmpty())
		{
			if (!port.name.isEmpty())
			{
				return port.name + (port.stereoSide == 0 ? QStringLiteral(" L") : QStringLiteral(" R"));
			}
			return port.stereoSide == 0 ? tr("Audio L") : tr("Audio R");
		}
		return port.name.isEmpty() ? boundaryLabel(port.signal) : port.name;
	};
	const auto activeGraphScope = std::make_shared<QString>(QStringLiteral("root/"));
	const auto appendBoundaryModules = [this, song, boundaryPortLabel, boundaryColor, activeGraphScope](const QString& graph,
		QList<NodeWorkflowCanvas::Node>& nodes, QList<NodeWorkflowCanvas::Port>& ports,
		const std::function<void()>& redraw,
		const std::function<void(const Song::NodeBoundaryPort&, NodeWorkflowCanvas::Port&)>& configure) {
		QRectF bounds;
		for (const auto& node : std::as_const(nodes))
		{
			bounds = bounds.united(node.bounds);
		}
		qreal inputY = bounds.top();
		qreal outputY = bounds.top();
		QHash<QString, QRectF> stereoNodes;
		for (const auto& port : song->nodeBoundaryPorts(graph))
		{
			auto& y = port.output ? outputY : inputY;
			const qreal x = port.output ? bounds.right() + 90 : bounds.left() - 270;
			const bool stereo = !port.stereoGroup.isEmpty();
			const QString id = stereo ? port.stereoGroup : port.id;
			QRectF nodeBounds = stereoNodes.value(id);
			if (nodeBounds.isNull())
			{
				nodeBounds = QRectF(x, y, 180, stereo ? 102 : 76);
				const auto title = port.name.isEmpty() ? (port.output ? tr("Output Port") : tr("Input Port"))
					: port.name;
				nodes.append({nodeBounds, title, {}});
				auto& node = nodes.back();
				node.layoutKey = graph + QStringLiteral("boundary/") + id;
				node.rename = [this, song, graph, id, title, redraw] {
					bool accepted = false;
					const auto name = QInputDialog::getText(this, tr("Rename Port"), tr("Name"),
						QLineEdit::Normal, title, &accepted);
					if (accepted && song->renameNodeBoundaryPort(graph, id, name))
					{
						redraw();
					}
				};
				node.remove = [song, graph, id, redraw] {
					song->removeNodeBoundaryPort(graph, id);
					redraw();
				};
				node.copy = [song, activeGraphScope, signal = port.signal, name = port.name, output = port.output, stereo, redraw] {
					return std::function<void(const QPointF&)>{[=](const QPointF& at) {
						const auto graph = *activeGraphScope;
						const auto id = song->createNodeBoundaryPort(graph, signal, output, stereo);
						if (!id.isEmpty())
						{
							if (!name.isEmpty())
							{
								song->renameNodeBoundaryPort(graph, id, name);
							}
							song->setNodeLayoutPosition(graph + QStringLiteral("boundary/") + id, at);
							redraw();
						}
					}};
				};
				if (stereo)
				{
					stereoNodes.insert(id, nodeBounds);
				}
				y += nodeBounds.height() + 20;
			}
			ports.append({QPointF(port.output ? nodeBounds.left() : nodeBounds.right(),
				nodeBounds.top() + 49 + (stereo && port.stereoSide == 1 ? 25 : 0)),
				boundaryColor(port.signal), boundaryPortLabel(port), !port.output, {}});
			ports.back().graphEndpoint = port.id;
			ports.back().graphSignal = port.signal;
			configure(port, ports.back());
		}
	};
	const auto appendBoundaryProjection = [song, boundaryPortLabel, boundaryColor](const QString& graph,
		NodeWorkflowCanvas::Node& node, QList<NodeWorkflowCanvas::Port>& ports,
		const std::function<void(const Song::NodeBoundaryPort&, NodeWorkflowCanvas::Port&)>& configure = {}) {
		qreal y = node.bounds.bottom() + 14;
		for (const auto& port : song->nodeBoundaryPorts(graph))
		{
			const qreal x = port.output ? node.bounds.right() : node.bounds.left();
			ports.append({QPointF(x, y), boundaryColor(port.signal), boundaryPortLabel(port), port.output, {}});
			ports.back().graphEndpoint = port.id;
			ports.back().graphSignal = port.signal;
			if (configure)
			{
				configure(port, ports.back());
			}
			node.bounds.setBottom(y + 18);
			y += 26;
		}
	};
	const auto customPath = std::make_shared<QList<Song::NodeCustomGraph>>();
	const auto returnFromCustom = std::make_shared<std::function<void()>>();
	const auto appendCustomNodes = [this, song, appendBoundaryProjection, activeGraphScope](const QString& scope,
		QList<NodeWorkflowCanvas::Node>& nodes, QList<NodeWorkflowCanvas::Port>& ports,
		const std::function<void(const Song::NodeCustomGraph&)>& enter, const std::function<void()>& redraw) {
		QRectF bounds;
		for (const auto& node : std::as_const(nodes))
		{
			bounds = bounds.united(node.bounds);
		}
		qreal y = nodes.isEmpty() ? 40 : bounds.bottom() + 60;
		for (const auto& graph : song->nodeCustomGraphs(scope))
		{
			nodes.append({QRectF(nodes.size() > 1 ? bounds.left() : 40, y, 200, 84), graph.name,
				[enter, graph] { enter(graph); }});
			auto& node = nodes.back();
			node.layoutKey = QStringLiteral("custom/") + graph.id;
			node.copy = [song, activeGraphScope, graph, redraw] {
				const auto snapshot = song->copyNodeCustomGraph(graph.id);
				return std::function<void(const QPointF&)>{[song, activeGraphScope, redraw, snapshot](const QPointF& position) {
					const auto id = song->pasteNodeCustomGraph(*activeGraphScope, snapshot);
					if (!id.isEmpty())
					{
						song->setNodeLayoutPosition(QStringLiteral("custom/") + id, position);
						redraw();
					}
				}};
			};
			node.rename = [this, song, graph, redraw] {
				bool accepted = false;
				const auto name = QInputDialog::getText(this, tr("Rename Subgraph"), tr("Name"),
					QLineEdit::Normal, graph.name, &accepted);
				if (accepted && song->renameNodeCustomGraph(graph.id, name))
				{
					redraw();
				}
			};
			node.remove = [this, song, graph, redraw] {
				if (QMessageBox::question(this, tr("Delete Node"), tr("Delete %1?").arg(graph.name),
					QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes
					&& song->removeNodeCustomGraph(graph.id))
				{
					redraw();
				}
			};
			appendBoundaryProjection(QStringLiteral("custom/") + graph.id + '/', node, ports,
				[](const auto& boundary, auto& endpoint) {
					endpoint.graphEndpoint = boundary.id;
					endpoint.connectionGroup = QStringLiteral("custom:") + boundary.signal;
				});
			y = node.bounds.bottom() + 30;
		}
	};
	const auto attachCustomConnections = [song, invalidVstConnections](const QString& scope, QList<NodeWorkflowCanvas::Port>& ports,
		QList<NodeWorkflowCanvas::Connection>& cables, const std::function<void()>& redraw,
		const QHash<QString, bool>& hiddenVstPorts = {}) {
		QHash<QString, qsizetype> endpoints;
		auto inputs = std::make_shared<QStringList>();
		const auto isNativeEndpoint = [](const QString& endpoint) {
			const auto key = endpoint.section('/', -1);
			return key.startsWith("audio-in:") || key.startsWith("audio-out:")
				|| key.startsWith("midi-in:") || key.startsWith("midi-out:");
		};
		for (qsizetype index = 0; index < ports.size(); ++index)
		{
			auto& port = ports[index];
			if (port.graphEndpoint.isEmpty())
			{
				continue;
			}
			endpoints.insert(port.graphEndpoint, index);
			if (port.graphSignal.isEmpty() && port.connectionGroup.startsWith("custom:"))
			{
				port.graphSignal = port.connectionGroup.mid(7);
			}
			if (port.graphSignal.isEmpty() && isNativeEndpoint(port.graphEndpoint)) { continue; }
			if (!port.output)
			{
				port.graphChannel = static_cast<unsigned>(inputs->size());
				inputs->append(port.graphEndpoint);
			}
			else
			{
				port.graphConnectTo = [song, scope, id = port.graphEndpoint, inputs, redraw](unsigned target) {
					if (target < static_cast<unsigned>(inputs->size())
						&& song->setNodeCustomConnection(scope, id, inputs->at(target), true))
					{
						redraw();
					}
				};
			}
		}
		QStringList unavailable;
		for (const auto& edge : song->nodeCustomConnections(scope))
		{
			const auto source = endpoints.constFind(edge.source);
			const auto target = endpoints.constFind(edge.destination);
			const bool sourceVisible = source != endpoints.cend();
			const bool targetVisible = target != endpoints.cend();
			const bool connected = sourceVisible && targetVisible;
			const auto available = [&](const QString& endpoint, auto position) {
				if (position == endpoints.cend())
				{
					return hiddenVstPorts.value(endpoint, false);
				}
				return !isNativeEndpoint(endpoint) || !ports.at(*position).graphSignal.isEmpty();
			};
			const bool invalid = (isNativeEndpoint(edge.source) || isNativeEndpoint(edge.destination))
				&& (!available(edge.source, source) || !available(edge.destination, target));
			if (invalid)
			{
				unavailable.append(tr("%1 → %2").arg(edge.source, edge.destination));
			}
			if (connected)
			{
				const auto& output = ports.at(*source);
				cables.append({output.position, ports.at(*target).position, invalid ? QColor(Qt::gray) : output.color, {},
					[song, scope, edge, redraw] {
						if (song->setNodeCustomConnection(scope, edge.source, edge.destination, false))
						{
							redraw();
						}
					}});
			}
		}
		invalidVstConnections->setText(tr("Unavailable VST connections: %1").arg(unavailable.size()));
		invalidVstConnections->setToolTip(unavailable.join('\n'));
		invalidVstConnections->setVisible(!unavailable.isEmpty());
	};
	const auto appendVstPorts = [canvas](auto* plugin, const QString& prefix, NodeWorkflowCanvas::Node& node,
		QList<NodeWorkflowCanvas::Port>& ports, unsigned inputRow, unsigned outputRow) {
		auto* owner = dynamic_cast<vsthost::VstNodeOwner*>(plugin);
		auto* processor = owner ? owner->vstNodeProcessor() : nullptr;
		if (!processor) { return false; }
		for (const auto& spec : processor->nodePorts())
		{
			const bool output = spec.direction == vsthost::NodePortDirection::Output;
			const bool audio = spec.media == vsthost::NodePortMedia::Audio;
			const auto color = audio ? canvas->audioPortColor() : canvas->midiPortColor();
			const auto signal = audio ? QStringLiteral("audio") : QStringLiteral("midi");
			const auto row = output ? outputRow++ : inputRow++;
			const auto label = spec.available ? spec.name : tr("%1 (%2)").arg(spec.name, spec.unavailableReason);
			ports.append({QPointF(output ? node.bounds.right() : node.bounds.left(), node.bounds.top() + 48 + 27 * row),
				spec.available ? color : Qt::gray, label, output, {}});
			ports.back().graphEndpoint = prefix + spec.key();
			if (spec.available)
			{
				ports.back().graphSignal = signal;
			}
		}
		node.bounds.setHeight(std::max(node.bounds.height(), qreal(48 + 27 * std::max(inputRow, outputRow))));
		return true;
	};
	const auto appendCustomEffects = [this, song, canvas, activeGraphScope, appendVstPorts](const QString& scope,
		QList<NodeWorkflowCanvas::Node>& nodes, QList<NodeWorkflowCanvas::Port>& ports,
		const std::function<void()>& redraw) {
		const auto channelFor = [](const QUuid& id) {
			auto* mixer = Engine::mixer();
			for (mix_ch_t channel = 1; channel < mixer->numChannels(); ++channel)
			{
				if (mixer->mixerChannel(channel)->nodeWorkflowId() == id)
				{
					return channel;
				}
			}
			return mix_ch_t{0};
		};
		auto* mixer = Engine::mixer();
		qreal y = 40;
		for (mix_ch_t index = 1; index < mixer->numChannels(); ++index)
		{
			auto* channel = mixer->mixerChannel(index);
			const auto id = channel->nodeWorkflowId();
			if (song->nodeCustomEffectScope(id) != scope)
			{
				continue;
			}
			const auto prefix = QStringLiteral("effect/") + id.toString(QUuid::WithoutBraces) + '/';
			nodes.append({QRectF(340, y, 195, 106), channel->m_name, [this] { toggleMixerWin(); }});
			auto& node = nodes.back();
			node.layoutKey = scope + prefix;
			node.copy = [song, id, channelFor, activeGraphScope, redraw] {
				const auto index = channelFor(id);
				if (index == 0)
				{
					return std::function<void(const QPointF&)>{};
				}
				auto* source = Engine::mixer()->mixerChannel(index);
				QDomDocument document;
				auto state = document.createElement("channel");
				document.appendChild(state);
				source->m_fxChain.saveState(document, state);
				const auto name = source->m_name;
				const auto volume = source->m_volumeModel.value();
				return std::function<void(const QPointF&)>{[song, activeGraphScope, redraw, state, name, volume]
					(const QPointF& position) {
					const auto scope = *activeGraphScope;
					if (!song->isNodeProject() || !scope.startsWith("custom/"))
					{
						return;
					}
					const auto guard = Engine::audioEngine()->requestChangesGuard();
					auto* mixer = Engine::mixer();
					const auto index = mixer->createChannel();
					auto* target = mixer->mixerChannel(index);
					target->m_name = name;
					target->m_volumeModel.setValue(volume);
					target->m_fxChain.restoreState(state.firstChildElement());
					target->m_nodeEffect = true;
					const auto id = target->nodeWorkflowId();
					song->setNodeCustomEffectScope(id, scope);
					mixer->deleteChannelSend(index, 0);
					song->setNodeLayoutPosition(scope + QStringLiteral("effect/")
						+ id.toString(QUuid::WithoutBraces) + '/', position);
					redraw();
				}};
			};
			node.remove = [song, id, channelFor, redraw] {
				const auto index = channelFor(id);
				if (index != 0)
				{
					song->setNodeCustomEffectScope(id, {});
					Engine::mixer()->deleteChannel(index);
					redraw();
				}
			};
			for (int side = 0; side < 2; ++side)
			{
				const auto label = side == 0 ? tr("Audio L") : tr("Audio R");
				for (const bool output : {false, true})
				{
					ports.append({QPointF(output ? 535 : 340, y + 48 + 27 * side),
						canvas->audioPortColor(), label, output, {}, QStringLiteral("custom:audio")});
					ports.back().graphEndpoint = prefix + (output ? QStringLiteral("out-") : QStringLiteral("in-"))
						+ (side == 0 ? 'L' : 'R');
				}
			}
			appendVstPorts(channel->m_fxChain.effectAt(0), prefix, node, ports, 2, 2);
			y += node.bounds.height() + 24;
		}
	};
	const auto appendCustomInstruments = [this, song, canvas, activeGraphScope, appendVstPorts](const QString& scope,
		QList<NodeWorkflowCanvas::Node>& nodes, QList<NodeWorkflowCanvas::Port>& ports,
		const std::function<void()>& redraw) {
		qreal y = 40;
		for (const auto& [id, track] : song->nodeCustomInstruments(scope))
		{
			const auto prefix = QStringLiteral("instrument/") + id + '/';
			const QPointer<InstrumentTrack> guarded(track);
			nodes.append({QRectF(65, y, 185, 106), track->instrumentName(), [this, guarded] {
				if (guarded && guarded->instrument())
				{
					auto* dialog = new QDialog(this);
					dialog->setAttribute(Qt::WA_DeleteOnClose);
					dialog->setWindowTitle(guarded->name());
					auto* layout = new QVBoxLayout(dialog);
					layout->addWidget(guarded->instrument()->createView(dialog));
					connect(guarded.data(), &QObject::destroyed, dialog, &QDialog::close);
					dialog->show();
				}
			}});
			nodes.back().layoutKey = scope + prefix;
			nodes.back().copy = [song, guarded, activeGraphScope, redraw] {
				const auto state = guarded ? song->copyNodeInstrumentLayer(*guarded, {}) : QDomElement{};
				return std::function<void(const QPointF&)>{[song, activeGraphScope, redraw, state](const QPointF& position) {
					const auto scope = *activeGraphScope;
					const auto id = song->pasteNodeCustomInstrument(scope, state);
					if (!id.isEmpty())
					{
						song->setNodeLayoutPosition(scope + QStringLiteral("instrument/") + id + '/', position);
						redraw();
					}
				}};
			};
			nodes.back().remove = [song, scope, id, redraw] {
				song->removeNodeCustomInstrument(scope, id);
				redraw();
			};
			ports.append({QPointF(65, y + 48), canvas->midiPortColor(), tr("MIDI"), false, {},
				QStringLiteral("custom:midi")});
			ports.back().graphEndpoint = prefix + QStringLiteral("midi");
			for (int side = 0; side < 2; ++side)
			{
				ports.append({QPointF(250, y + 48 + 27 * side), canvas->audioPortColor(),
					side == 0 ? tr("Audio L") : tr("Audio R"), true, {}, QStringLiteral("custom:audio")});
				ports.back().graphEndpoint = prefix + (side == 0 ? QStringLiteral("out-L") : QStringLiteral("out-R"));
			}
			appendVstPorts(track->instrument(), prefix, nodes.back(), ports, 1, 2);
			y += nodes.back().bounds.height() + 24;
		}
	};
	const auto showCustom = [this, canvas, song, customPath, activeGraphScope, patternPanel, clearTriggers,
		setBreadcrumb, setInspector, appendBoundaryModules, appendCustomNodes, attachCustomConnections, appendCustomEffects, appendCustomInstruments](const auto& render, bool fitCanvas) {
		if (customPath->isEmpty())
		{
			return;
		}
		const auto graph = customPath->back();
		const auto scope = QStringLiteral("custom/") + graph.id + '/';
		*activeGraphScope = scope;
		clearTriggers();
		patternPanel->hide();
		canvas->setAcceptDrops(true);
		canvas->setClipboardScope(NodeWorkflowCanvas::ClipboardScope::Custom);
		QList<QPair<QString, QString>> segments{{"root", tr("Project")}};
		for (qsizetype index = 0; index < customPath->size(); ++index)
		{
			segments.append({index + 1 < customPath->size() ? QStringLiteral("custom:") + QString::number(index)
				: QString{}, customPath->at(index).name});
		}
		setBreadcrumb(segments);
		setInspector(graph.name, tr("Custom Subgraph"), {}, {});
		QList<NodeWorkflowCanvas::Node> nodes;
		QList<NodeWorkflowCanvas::Port> ports;
		const auto redraw = [render] { render(render, false); };
		appendCustomEffects(scope, nodes, ports, redraw);
		appendCustomInstruments(scope, nodes, ports, redraw);
		appendCustomNodes(scope, nodes, ports, [customPath, render](const Song::NodeCustomGraph& child) {
			customPath->append(child);
			render(render, true);
		}, redraw);
		appendBoundaryModules(scope, nodes, ports, redraw, [](const auto& boundary, auto& endpoint) {
			endpoint.graphEndpoint = boundary.id;
			endpoint.connectionGroup = QStringLiteral("custom:") + boundary.signal;
		});
		QList<NodeWorkflowCanvas::Connection> cables;
		attachCustomConnections(scope, ports, cables, redraw);
		canvas->setGraph(std::move(nodes), std::move(cables), std::move(ports));
		if (fitCanvas)
		{
			canvas->fitGraph();
		}
	};
	const auto appendCustomChildren = [appendCustomNodes, attachCustomConnections, customPath,
		returnFromCustom = std::weak_ptr<std::function<void()>>(returnFromCustom), showCustom](
		const QString& scope, QList<NodeWorkflowCanvas::Node>& nodes, QList<NodeWorkflowCanvas::Port>& ports,
		QList<NodeWorkflowCanvas::Connection>& cables, const std::function<void()>& redraw,
		const QHash<QString, bool>& hiddenVstPorts = {}) {
		appendCustomNodes(scope, nodes, ports, [customPath, returnFromCustom, showCustom, redraw](const auto& graph) {
			if (const auto back = returnFromCustom.lock())
			{
				*back = redraw;
				*customPath = {graph};
				showCustom(showCustom, true);
			}
		}, redraw);
		attachCustomConnections(scope, ports, cables, redraw, hiddenVstPorts);
	};
	const auto bindProjectedOutput = [](NodeWorkflowCanvas::Port& required,
		NodeWorkflowCanvas::Port& projected, QList<NodeWorkflowCanvas::Connection>& cables) {
		projected.connectionGroup = std::exchange(required.connectionGroup, {});
		projected.channel = required.channel;
		projected.connectTo = std::exchange(required.connectTo, {});
		projected.edit = std::exchange(required.edit, {});
		projected.labelBackground = required.labelBackground;
		for (auto& cable : cables)
		{
			if (cable.source == required.position)
			{
				cable.source = projected.position;
			}
		}
	};
	const auto showSource = [this, canvas, song, activeTracks, activeSource, patternPanel, patternLayout, add, footerStatus,
		inspectTriggers, clearTriggers, activeGraphScope, customPath, appendCustomChildren,
		setBreadcrumb, setInspector, breadcrumb, appendBoundaryModules, appendVstPorts](
		const auto& render, Track* track, bool fitCanvas) -> void
	{
		customPath->clear();
		*activeTracks = nullptr;
		*activeSource = track;
		canvas->setClipboardScope(NodeWorkflowCanvas::ClipboardScope::Source);
		const bool inPattern = track->trackContainer() == Engine::patternStore();
		if (inPattern && (track->type() == Track::Type::Instrument || track->type() == Track::Type::Sample))
		{
			inspectTriggers(track, std::nullopt, false);
		}
		else
		{
			clearTriggers();
		}
		const auto triggerSelection = [inPattern, guarded = QPointer<Track>(track), inspectTriggers]
			(std::optional<TriggerKind> kind) -> std::function<void()> {
			if (!inPattern)
			{
				return {};
			}
			return [guarded, inspectTriggers, kind] {
				if (guarded)
				{
					inspectTriggers(guarded, kind, true);
				}
			};
		};
		setBreadcrumb({{"root", tr("Project")},
			{inPattern ? "pattern" : "song", inPattern ? tr("Pattern") : tr("Multitrack")},
			{{}, track->name()}});
		footerStatus->setText(tr("Audio ports connect one channel at a time"));
		canvas->setAcceptDrops(true);
		add->setEnabled(true);
		const auto trackColor = track->color();
		const auto layoutPrefix = QStringLiteral("source/") + track->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/';
		*activeGraphScope = layoutPrefix;
		const auto redrawSource = [render, guarded = QPointer<Track>(track)] {
			if (guarded)
			{
				render(render, guarded.data(), false);
			}
		};
		const auto trackPortLabel = [this, track](const QString& label) {
			return tr("%1 · %2").arg(track->name(), label);
		};
		const auto appendSourceEffects = [&](QList<NodeWorkflowCanvas::Node>& nodes, QList<NodeWorkflowCanvas::Port>& ports) {
			auto* bus = nodeWorkflowAudioBus(*track);
			auto* chain = bus ? bus->effects() : nullptr;
			if (!chain)
			{
				return;
			}
			qreal x = 30;
			for (const int index : song->sourceGraphEffectSlots(*track))
			{
				if (auto* effect = chain->effectAt(index))
				{
					nodes.append({QRectF(x, 245, 175, 76), effect->displayName(), {}});
					auto& node = nodes.back();
					node.layoutKey = layoutPrefix + QStringLiteral("effect/")
						+ effect->property("nodeWorkflowSourceEffect").toString();
					node.remove = [song, render, source = QPointer<Track>(track), guarded = QPointer<Effect>(effect)] {
						if (source && guarded)
						{
							const auto guard = Engine::audioEngine()->requestChangesGuard();
							nodeWorkflowAudioBus(*source)->effects()->removeEffect(guarded);
							delete guarded.data();
							song->setModified();
							render(render, source.data(), false);
						}
					};
					node.copy = [song, render, activeSource, guarded = QPointer<Effect>(effect)] {
						QDomDocument document;
						auto root = document.createElement("effect-copy");
						document.appendChild(root);
						if (guarded)
						{
							auto state = guarded->saveState(document, root);
							state.setAttribute("name", QString::fromUtf8(guarded->descriptor()->name));
							state.appendChild(guarded->key().saveXML(document));
							const auto journal = state.elementsByTagName("journallingObject");
							while (journal.count() > 0)
							{
								auto child = journal.item(0);
								child.parentNode().removeChild(child);
							}
						}
						return std::function<void(const QPointF&)>{[song, render, activeSource, root](const QPointF& point) {
							const auto source = *activeSource;
							const auto state = root.firstChildElement("effect");
							auto* bus = source ? nodeWorkflowAudioBus(*source) : nullptr;
							auto* chain = bus ? bus->effects() : nullptr;
							if (!chain || state.isNull())
							{
								return;
							}
							EffectKey key(state.firstChildElement("key"));
							auto* effect = Effect::instantiate(state.attribute("name"), chain, &key);
							if (!effect || !effect->isOkay())
							{
								delete effect;
								return;
							}
							const auto guard = Engine::audioEngine()->requestChangesGuard();
							effect->restoreState(state);
							const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
							effect->setProperty("nodeWorkflowSourceEffect", id);
							chain->appendEffect(effect);
							song->setNodeLayoutPosition(QStringLiteral("source/")
								+ source->nodeWorkflowId().toString(QUuid::WithoutBraces)
								+ QStringLiteral("/effect/") + id, point);
							song->setModified();
							render(render, source.data(), false);
						}};
					};
					appendVstPorts(effect, QStringLiteral("source-effect/")
						+ effect->property("nodeWorkflowSourceEffect").toString() + '/', node, ports, 0, 0);
					x += 195;
				}
			}
		};
		const auto appendSourceBoundaryPorts = [&](const QString& group,
			QList<NodeWorkflowCanvas::Node>& nodes, QList<NodeWorkflowCanvas::Connection>& cables,
			QList<NodeWorkflowCanvas::Port>& ports) {
			const auto reserved = song->nodeBoundaryPorts(layoutPrefix);
			const auto trackId = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
			std::optional<NodeWorkflowCanvas::Connection> midiWire;
			if (std::any_of(reserved.cbegin(), reserved.cend(), [&trackId](const auto& port) {
				return !port.output && port.signal == "midi" && port.track == trackId;
			}))
			{
				const auto wire = std::find_if(cables.begin(), cables.end(), [](const auto& connection) {
					return connection.source == QPointF(185, 130) && connection.destination == QPointF(240, 130);
				});
				if (wire != cables.end())
				{
					midiWire = *wire;
					cables.erase(wire);
				}
			}
			for (auto& port : ports)
			{
				if (port.connectionGroup != group)
				{
					continue;
				}
				if (!port.output && port.channel < 2)
				{
					port.graphEndpoint = QStringLiteral("track-output/") + trackId
						+ (port.channel == 0 ? QStringLiteral("/in-L") : QStringLiteral("/in-R"));
					port.graphSignal = QStringLiteral("audio");
				}
				else if (port.output && port.connectTo)
				{
					port.connectTo = [song, guarded = QPointer<Track>(track), layoutPrefix, reserved,
						side = static_cast<ch_cnt_t>(port.channel), native = port.connectTo](unsigned target) {
						if (!guarded)
						{
							return;
						}
						if (target < 2)
						{
							native(target);
						}
						else if (target - 2 < static_cast<unsigned>(reserved.size()))
						{
							const auto& output = reserved.at(target - 2);
							const auto targetSide = !output.track.isEmpty() ? output.channel
								: (!output.stereoGroup.isEmpty() ? output.stereoSide : side);
							if (song->setNodeBoundaryAudioSource(layoutPrefix, output.id, *guarded,
								static_cast<ch_cnt_t>(targetSide), true))
							{
								native(targetSide);
							}
						}
					};
				}
			}
			appendBoundaryModules(layoutPrefix, nodes, ports,
				[render, activeSource, source = QPointer<Track>(track)] {
					if (source && *activeSource == source)
					{
						render(render, source.data(), false);
					}
				},
				[&](const Song::NodeBoundaryPort& boundary, NodeWorkflowCanvas::Port& endpoint) {
					if (!boundary.output && boundary.signal == "midi")
					{
						const auto native = std::find_if(ports.cbegin(), ports.cend(), [](const auto& port) {
							return port.output && port.connectionGroup == "source-midi-input";
						});
						if (native == ports.cend())
						{
							return;
						}
						endpoint.connectionGroup = native->connectionGroup;
						endpoint.connectTo = [song, render, guarded = QPointer<Track>(track), id = boundary.id](unsigned) {
							if (guarded && song->setNodeSourceMidiBoundary(id, *guarded))
							{
								render(render, guarded.data(), false);
							}
						};
						if (midiWire && boundary.track == trackId)
						{
							auto wire = *midiWire;
							wire.source = endpoint.position;
							wire.disconnect = [song, render, guarded = QPointer<Track>(track), id = boundary.id] {
								if (guarded && song->setNodeSourceMidiBoundary(id, *guarded, false))
								{
									render(render, guarded.data(), false);
								}
							};
							cables.append(std::move(wire));
						}
						return;
					}
					if (!boundary.output || boundary.signal != "audio")
					{
						return;
					}
					endpoint.connectionGroup = group;
					const auto item = std::find_if(reserved.cbegin(), reserved.cend(), [&boundary](const auto& port) {
						return port.id == boundary.id;
					});
					endpoint.channel = 2 + static_cast<unsigned>(item - reserved.cbegin());
					if (boundary.track.isEmpty())
					{
						return;
					}
					const auto native = std::find_if(ports.cbegin(), ports.cend(), [&](const auto& port) {
						return !port.output && port.connectionGroup == group && port.channel == boundary.channel;
					});
					if (native != ports.cend())
					{
						endpoint.label = native->label;
						for (auto& cable : cables)
						{
							if (cable.destination == native->position)
							{
								cable.destination = endpoint.position;
							}
						}
					}
				});
		};
		if (track->type() == Track::Type::Sample)
		{
			patternPanel->hide();
			auto* patterns = Engine::patternStore();
			const QPointer<Track> sampleTrack(track);
			const auto editClip = [this, sampleTrack, patterns] {
				if (!sampleTrack)
				{
					return;
				}
				QPointer<SampleClip> clip;
				const bool patternMember = sampleTrack->trackContainer() == patterns;
				const auto& clips = sampleTrack->getClips();
				if (patternMember)
				{
					clip = dynamic_cast<SampleClip*>(sampleTrack->getClip(patterns->currentPattern()));
				}
				else if (clips.size() == 1)
				{
					clip = static_cast<SampleClip*>(clips.front());
				}
				else if (!clips.empty())
				{
					QMenu menu(this);
					for (auto* item : clips)
					{
						auto* action = menu.addAction(item->name());
						connect(action, &QAction::triggered, &menu, [&clip, item = QPointer<Clip>(item)] {
							clip = static_cast<SampleClip*>(item.data());
						});
					}
					if (!menu.exec(QCursor::pos()) || !clip)
					{
						return;
					}
				}
				const auto file = FileDialog::openAudioFile();
				if (!sampleTrack || file.isEmpty())
				{
					return;
				}
				if (!clip && !patternMember && sampleTrack->getClips().empty())
				{
					clip = static_cast<SampleClip*>(sampleTrack->createClip(TimePos{}));
				}
				if (clip)
				{
					clip->setSampleFile(file);
				}
			};
			const auto audio = canvas->audioPortColor();
			const auto group = QStringLiteral("sample-output");
			const auto connections = song->nodeSourceConnections(*track);
			QList<NodeWorkflowCanvas::Node> nodes{
				{QRectF(30, 70, 200, 114), tr("Audio Input"), {}},
				{QRectF(320, 70, 200, 114), track->name(), editClip},
				{QRectF(650, 70, 200, 114), tr("Audio Output"), {}},
			};
			nodes[0].layoutKey = layoutPrefix + QStringLiteral("audio-input");
			nodes[1].layoutKey = layoutPrefix + QStringLiteral("sample");
			nodes[1].trackColor = trackColor.value_or(QColor{});
			nodes[2].layoutKey = layoutPrefix + QStringLiteral("output");
			QList<NodeWorkflowCanvas::Connection> cables;
			QList<NodeWorkflowCanvas::Port> ports;
			for (ch_cnt_t side = 0; side < 2; ++side)
			{
				const auto label = side == 0 ? tr("Audio L") : tr("Audio R");
				const QPointF source(520, 118 + 28 * side);
				ports.append({QPointF(230, 118 + 28 * side), audio, label, true, {}});
				ports.append({QPointF(320, 118 + 28 * side), audio, label, false, {}});
				ports.append({QPointF(650, 118 + 28 * side), audio, label, false, {}, group, side, {}});
				const auto change = [sampleTrack, song, render, side](std::optional<ch_cnt_t> target) {
					if (sampleTrack && song->setNodeSourceAudioOutput(*sampleTrack, side, target))
					{
						render(render, sampleTrack.data(), false);
					}
				};
				if (const auto output = connections.audioOutputs[side])
				{
					cables.append({source, QPointF(650, 118 + 28 * *output), trackColor.value_or(audio),
						[sampleTrack, side] {
							return sampleTrack && nodeWorkflowAudioBus(*sampleTrack)->nodeSignalActive(side);
						}, [change] { change(std::nullopt); }});
					cables.back().select = triggerSelection(TriggerKind::Audio);
				}
				ports.append({source, audio, trackPortLabel(side == 0 ? tr("Audio L") : tr("Audio R")), true,
					[this, change](const QPoint& screen) {
						QMenu menu(this);
						if (auto* disconnect = menu.addAction(tr("Disconnect")); menu.exec(screen) == disconnect)
						{
							change(std::nullopt);
						}
					}, group, side, [change](unsigned target) {
						change(static_cast<ch_cnt_t>(target));
					}});
				ports.back().labelBackground = trackColor.value_or(QColor{});
			}
			setInspector(tr("Sample Track"), tr("Uses the sample track clips and audio bus"),
				{{tr("Channels"), tr("2 × mono (L / R)")}},
				{{tr("Open Sample Clip"), editClip}});
			appendSourceEffects(nodes, ports);
			appendSourceBoundaryPorts(group, nodes, cables, ports);
			appendCustomChildren(layoutPrefix, nodes, ports, cables, redrawSource);
			canvas->setGraph(std::move(nodes), std::move(cables), std::move(ports));
			if (fitCanvas)
			{
				canvas->fitGraph();
			}
			return;
		}
		const bool singing = track->type() == Track::Type::SVS;
		const bool hasPrimary = singing || static_cast<InstrumentTrack*>(track)->instrument();
		auto* patterns = Engine::patternStore();
		const bool patternInstrument = !singing && track->trackContainer() == patterns;
		const auto current = patterns->currentPattern();
		auto* clip = patternInstrument && current >= 0 && current < patterns->numOfPatterns()
			? dynamic_cast<MidiClip*>(track->getClip(current)) : nullptr;
		const bool stepMode = clip && clip->type() == MidiClip::Type::BeatClip;
		const auto connections = song->nodeSourceConnections(*track);
		const auto midi = canvas->midiPortColor();
		const auto audio = canvas->audioPortColor();
		const auto voice = canvas->voicePortColor();
		const auto eventColor = stepMode ? canvas->stepPortColor() : (singing ? voice : midi);
		const auto eventLabel = stepMode ? tr("Step Events") : (singing ? tr("Voice Events") : tr("MIDI"));

		QList<NodeWorkflowCanvas::Node> nodes{
			{QRectF(20, 80, 165, 100), singing ? tr("External MIDI Input") : tr("MIDI Input"), {}},
			{QRectF(240, 80, 165, 100), singing ? tr("SVS Piano Roll") : tr("Piano Roll"),
				[this, track = QPointer<Track>(track), singing] {
				if (!track)
				{
					return;
				}
				auto* patterns = Engine::patternStore();
				auto* clip = track->trackContainer() == patterns
					? track->getClip(patterns->currentPattern())
					: (track->getClips().empty() ? track->createClip(TimePos{}) : track->getClip(0));
				if (!clip)
				{
					return;
				}
				if (singing)
				{
					(new SVSPianoRoll(static_cast<SVSClip*>(clip)))->openIn(this);
				}
				else
				{
					auto* roll = getGUI()->pianoRoll();
					roll->setCurrentMidiClip(static_cast<MidiClip*>(clip));
					roll->parentWidget()->show();
					roll->show();
					roll->setFocus();
				}
			}},
			{QRectF(460, 80, 165, 112),
				singing ? tr("SVS Instrument") : static_cast<InstrumentTrack*>(track)->instrumentName(), {}},
			{QRectF(680, 80, 165, 112), tr("Track Output"), {}},
		};
		if (stepMode)
		{
			nodes.removeFirst();
			nodes.front().title = tr("Step Sequencer");
			nodes.front().open = [patternPanel] {
				patternPanel->setFocus(Qt::MouseFocusReason);
			};
		}
		if (!stepMode)
		{
			nodes.front().layoutKey = layoutPrefix + QStringLiteral("midi-input");
			nodes.front().remove = [song, render, source = QPointer<Track>(track)] {
				if (source && song->removeDefaultNodeSourceMidiInput(*source))
				{
					render(render, source.data(), false);
				}
			};
			nodes.front().copy = [song, render, activeGraphScope, activeSource, name = nodes.front().title] {
				return std::function<void(const QPointF&)>{[song, render, activeGraphScope, activeSource, name](const QPointF& point) {
					if (!*activeSource)
					{
						return;
					}
					const auto scope = *activeGraphScope;
					const auto id = song->createNodeBoundaryPort(scope, QStringLiteral("midi"), false, false);
					if (!id.isEmpty())
					{
						song->renameNodeBoundaryPort(scope, id, name);
						song->setNodeLayoutPosition(scope + QStringLiteral("boundary/") + id, point);
						render(render, activeSource->data(), false);
					}
				}};
			};
			nodes[1].openPianoRoll = nodes[1].open;
		}
		nodes[stepMode ? 0 : 1].layoutKey = layoutPrefix + (stepMode ? QStringLiteral("step") : QStringLiteral("piano"));
		nodes[stepMode ? 1 : 2].layoutKey = layoutPrefix + QStringLiteral("plugin");
		nodes.back().layoutKey = layoutPrefix + QStringLiteral("output");
		const auto parentView = inPattern ? QStringLiteral("pattern") : QStringLiteral("song");
		const auto editor = stepMode ? nodes.front().open : nodes[1].openPianoRoll;
		auto& pluginNode = nodes[stepMode ? 1 : 2];
		const auto copyInstrument = [song, render, activeSource, owner = QPointer<Track>(track)](const QString& id) {
			const auto state = owner ? song->copyNodeInstrumentLayer(*owner, id) : QDomElement{};
			return std::function<void(const QPointF&)>{[song, render, activeSource, state](const QPointF& point) {
				const auto target = *activeSource;
				if (!target)
				{
					return;
				}
				const auto id = song->pasteNodeInstrumentLayer(*target, state);
				if (!id.isEmpty())
				{
					const auto prefix = QStringLiteral("source/")
						+ target->nodeWorkflowId().toString(QUuid::WithoutBraces);
					song->setNodeLayoutPosition(prefix + QStringLiteral("/layer/") + id, point);
					render(render, target.data(), false);
				}
			}};
		};
		if (!singing && hasPrimary)
		{
			pluginNode.copy = [copyInstrument] { return copyInstrument({}); };
			pluginNode.remove = [song, render, owner = QPointer<Track>(track)] {
				if (owner)
				{
					static_cast<InstrumentTrack*>(owner.data())->clearInstrument();
					song->setModified();
					render(render, owner.data(), false);
				}
			};
		}
		const auto openPlugin = [this, guarded = QPointer<Track>(track), inPattern, singing] {
			if (!guarded)
			{
				return;
			}
			if (singing)
			{
				auto* settings = createSVSPluginSettings(static_cast<SVSTrack*>(guarded.data()), this, true);
				settings->setAttribute(Qt::WA_DeleteOnClose);
				settings->show();
				return;
			}
			const auto& views = inPattern ? getGUI()->patternEditor()->m_editor->trackViews()
				: getGUI()->songEditor()->m_editor->trackViews();
			for (auto* view : views)
			{
				if (view->getTrack() == guarded.data())
				{
					auto* instrument = static_cast<InstrumentTrackView*>(view);
					instrument->getInstrumentTrackWindow()->toggleVisibility(true);
					break;
				}
			}
		};
		pluginNode.open = openPlugin;
		pluginNode.inspect = [this, setInspector, openPlugin, guarded = QPointer<Track>(track), singing] {
			if (!guarded)
			{
				return;
			}
			if (singing)
			{
				setInspector(tr("SVS Plugin"), tr("Input preserves lyrics, curves, and clip timing"),
					{{tr("Input Type"), tr("Voice Events")}, {tr("Audio Output"), tr("2 × mono (L / R)")}},
					{{tr("Open Voice Settings"), openPlugin}});
				return;
			}
			const auto plugin = static_cast<InstrumentTrack*>(guarded.data())->instrumentName();
			setInspector(tr("Instrument Plugin"), tr("Managed by the LMMS plugin host"),
				{{tr("Plugin Identifier"), plugin}, {tr("Instance Name"), guarded->name()}},
				{{tr("Open Plugin"), openPlugin}});
		};
		setInspector(singing ? tr("SVS Voice Track") : tr("Instrument Track"),
			singing ? tr("MIDI is only for note entry from an external device")
				: tr("Default sound source signal chain"),
			{{stepMode ? tr("Step Sequencer") : tr("Piano Roll"), track->name()},
				{tr("Track Output"), tr("Independent L / R ports")}},
			{{stepMode ? tr("Step Sequencer") : (singing ? tr("Open SVS Piano Roll") : tr("Open Piano Roll")),
				editor},
				{tr("Back to Parent"), [breadcrumb, parentView] { breadcrumb->linkActivated(parentView); }}});
		for (auto& node : nodes)
		{
			node.trackColor = trackColor.value_or(QColor{});
		}
		QList<NodeWorkflowCanvas::Connection> cables;
		if (!stepMode && connections.midiInput)
		{
			cables.append({QPointF(185, 130), QPointF(240, 130), trackColor.value_or(midi),
				[guarded = QPointer<Track>(track)] {
					return guarded && nodeWorkflowAudioBus(*guarded)->nodeEventActive(0);
				}, [song, render, guarded = QPointer<Track>(track)] {
					if (guarded)
					{
						song->setNodeSourceMidiInputConnection(*guarded, false);
						render(render, guarded.data(), false);
					}
				}});
		}
		if (stepMode ? connections.stepEvents : connections.events)
		{
			cables.append({QPointF(405, 130), QPointF(460, 130), trackColor.value_or(eventColor),
				[guarded = QPointer<Track>(track)] {
					return guarded && nodeWorkflowAudioBus(*guarded)->nodeEventActive(1);
				}, [song, render, guarded = QPointer<Track>(track), stepMode] {
					if (guarded)
					{
						if (stepMode)
						{
							song->setNodeSourceStepConnection(*guarded, false);
						}
						else
						{
							song->setNodeSourceEventConnection(*guarded, false);
						}
						render(render, guarded.data(), false);
					}
				}});
			if (patternInstrument)
			{
				cables.back().select = triggerSelection(stepMode ? TriggerKind::Step : TriggerKind::Note);
			}
		}
		for (unsigned side = 0; side < connections.audioOutputs.size(); ++side)
		{
			if (const auto output = connections.audioOutputs[side])
			{
				cables.append({QPointF(625, 130 + 30 * side), QPointF(680, 130 + 30 * *output),
					trackColor.value_or(audio),
					[guarded = QPointer<Track>(track), side] {
						return guarded && nodeWorkflowAudioBus(*guarded)->nodeSignalActive(side);
					}, [song, render, guarded = QPointer<Track>(track), side] {
						if (guarded)
						{
							song->setNodeSourceAudioOutput(*guarded, static_cast<ch_cnt_t>(side), std::nullopt);
							render(render, guarded.data(), false);
						}
					}});
				if (patternInstrument)
				{
					cables.back().select = triggerSelection(std::nullopt);
				}
			}
		}
		using Port = NodeWorkflowCanvas::Port;
		const auto isVstInstrument = [](Instrument* plugin) -> bool {
			auto* owner = dynamic_cast<vsthost::VstNodeOwner*>(plugin);
			return owner && owner->vstNodeProcessor() != nullptr;
		};
		bool hasVstInstrument = !singing && hasPrimary
			&& isVstInstrument(static_cast<InstrumentTrack*>(track)->instrument());
		const auto& layers = song->nodeInstrumentLayers(*track);
		for (const auto& layer : layers)
		{
			if (layer.instrument && isVstInstrument(layer.instrument->instrument()))
			{
				hasVstInstrument = true;
				break;
			}
		}
		const bool useStepToMidi = stepMode && hasVstInstrument;
		const QPointF stepToMidiOrigin(420, 220);
		const QPointF stepToMidiIn = stepToMidiOrigin + QPointF(0, 48);
		const QPointF stepToMidiOut = stepToMidiOrigin + QPointF(130, 48);
		bool stepToMidiInputConnected = false;
		QList<Port> ports{
			{QPointF(185, 130), midi, tr("MIDI"), true, {}},
			{QPointF(240, 130), midi, tr("MIDI"), false, {}},
			{QPointF(405, 130), eventColor, eventLabel, true, {}},
			{QPointF(460, 130), eventColor, eventLabel, false, {}},
		};
		QStringList layerIds;
		qreal layerY = 365;
		for (const auto& layer : layers)
		{
			layerIds.append(layer.id);
			const auto layoutKey = layoutPrefix + QStringLiteral("layer/") + layer.id;
			const QPointF origin(460, layerY);
			const QPointer<InstrumentTrack> instrument(layer.instrument);
			const auto open = [this, instrument] {
				if (!instrument || !instrument->instrument())
				{
					return;
				}
				auto* dialog = new QDialog(this);
				dialog->setAttribute(Qt::WA_DeleteOnClose);
				dialog->setWindowTitle(instrument->name());
				auto* layout = new QVBoxLayout(dialog);
				layout->addWidget(instrument->instrument()->createView(dialog));
				connect(instrument.data(), &QObject::destroyed, dialog, &QDialog::close);
				dialog->show();
			};
			nodes.append({QRectF(origin, QSizeF(165, 112)), instrument->name(), open});
			auto& node = nodes.back();
			node.layoutKey = layoutKey;
			node.trackColor = trackColor.value_or(QColor{});
			node.inspect = [this, setInspector, open, instrument] {
				if (instrument)
				{
					setInspector(tr("Instrument Plugin"), tr("Managed by the LMMS plugin host"),
						{{tr("Plugin Identifier"), instrument->instrumentName()},
							{tr("Instance Name"), instrument->name()}},
						{{tr("Open Plugin"), open}});
				}
			};
			node.remove = [song, render, owner = QPointer<Track>(track), id = layer.id] {
				if (owner)
				{
					song->removeNodeInstrumentLayer(*owner, id);
					render(render, owner.data(), false);
				}
			};
			node.copy = [copyInstrument, id = layer.id] { return copyInstrument(id); };
			const auto changeEvent = [song, render, owner = QPointer<Track>(track), id = layer.id,
				stepMode](bool connected) {
				if (owner && song->setNodeInstrumentLayerEvent(*owner, id, stepMode, connected))
				{
					render(render, owner.data(), false);
				}
			};
			const bool layerIsVst = instrument && isVstInstrument(instrument->instrument());
			if (layerIsVst)
			{
				const int portStart = ports.size();
				appendVstPorts(instrument->instrument(), QStringLiteral("source-layer/") + layer.id + '/',
					node, ports, 0, 0);
				std::optional<QPointF> vstMidiIn;
				QList<QPointF> vstAudioOuts;
				for (int i = portStart; i < ports.size(); ++i)
				{
					if (!ports[i].output && ports[i].graphSignal == QStringLiteral("midi") && !vstMidiIn)
					{
						vstMidiIn = ports[i].position;
					}
					else if (ports[i].output && ports[i].graphSignal == QStringLiteral("audio") && vstAudioOuts.size() < 2)
					{
						vstAudioOuts.append(ports[i].position);
					}
				}
				if ((stepMode ? layer.steps : layer.notes) && vstMidiIn)
				{
					if (useStepToMidi)
					{
						if (!stepToMidiInputConnected)
						{
							cables.append({QPointF(405, 130), stepToMidiIn, trackColor.value_or(eventColor),
								[instrument] { return instrument && instrument->audioBusHandle()->nodeEventActive(1); },
								[changeEvent] { changeEvent(false); }});
							stepToMidiInputConnected = true;
						}
						cables.append({stepToMidiOut, *vstMidiIn, trackColor.value_or(midi),
							[instrument] { return instrument && instrument->audioBusHandle()->nodeEventActive(1); },
							[changeEvent] { changeEvent(false); }});
					}
					else
					{
						cables.append({QPointF(405, 130), *vstMidiIn, trackColor.value_or(eventColor),
							[instrument] { return instrument && instrument->audioBusHandle()->nodeEventActive(1); },
							[changeEvent] { changeEvent(false); }});
					}
					if (patternInstrument)
					{
						cables.back().select = triggerSelection(stepMode ? TriggerKind::Step : TriggerKind::Note);
					}
				}
				for (ch_cnt_t side = 0; side < 2 && side < vstAudioOuts.size(); ++side)
				{
					const auto changeOutput = [song, render, owner = QPointer<Track>(track),
						id = layer.id, side](std::optional<ch_cnt_t> target) {
						if (owner && song->setNodeInstrumentLayerOutput(*owner, id, side, target))
						{
							render(render, owner.data(), false);
						}
					};
					if (const auto target = layer.outputs[side])
					{
						cables.append({vstAudioOuts[side], QPointF(680, 130 + 30 * *target), trackColor.value_or(audio),
							[instrument, side] { return instrument && instrument->audioBusHandle()->nodeSignalActive(side); },
							[changeOutput] { changeOutput(std::nullopt); }});
						if (patternInstrument)
						{
							cables.back().select = triggerSelection(std::nullopt);
						}
					}
				}
			}
			else
			{
				const QPointF input = origin + QPointF(0, 50);
				ports.append({input, eventColor, eventLabel, false, {}, "source-events",
					static_cast<unsigned>(layerIds.size()), {}});
				if (stepMode ? layer.steps : layer.notes)
				{
					cables.append({QPointF(405, 130), input, trackColor.value_or(eventColor),
						[instrument] { return instrument && instrument->audioBusHandle()->nodeEventActive(1); },
						[changeEvent] { changeEvent(false); }});
					if (patternInstrument)
					{
						cables.back().select = triggerSelection(stepMode ? TriggerKind::Step : TriggerKind::Note);
					}
				}
				for (ch_cnt_t side = 0; side < 2; ++side)
				{
					const auto source = origin + QPointF(165, 50 + 30 * side);
					const auto changeOutput = [song, render, owner = QPointer<Track>(track),
						id = layer.id, side](std::optional<ch_cnt_t> target) {
						if (owner && song->setNodeInstrumentLayerOutput(*owner, id, side, target))
						{
							render(render, owner.data(), false);
						}
					};
					ports.append({source, audio, side == 0 ? tr("Audio L") : tr("Audio R"), true,
						{}, "source-audio", side, [changeOutput](unsigned target) {
							changeOutput(static_cast<ch_cnt_t>(target));
						}});
					if (const auto target = layer.outputs[side])
					{
						cables.append({source, QPointF(680, 130 + 30 * *target), trackColor.value_or(audio),
							[instrument, side] { return instrument && instrument->audioBusHandle()->nodeSignalActive(side); },
							[changeOutput] { changeOutput(std::nullopt); }});
						if (patternInstrument)
						{
							cables.back().select = triggerSelection(std::nullopt);
						}
					}
				}
			}
			layerY += std::max(qreal(128), node.bounds.height() + 16);
		}
		for (const bool midiInput : {true, false})
		{
			const bool connected = midiInput ? connections.midiInput
				: (stepMode ? connections.stepEvents : connections.events);
			const auto index = midiInput ? 0 : 2;
			const auto group = midiInput ? QStringLiteral("source-midi-input")
				: (stepMode ? QStringLiteral("source-step-events") : QStringLiteral("source-events"));
			ports[index].connectionGroup = group;
			ports[index + 1].connectionGroup = group;
			ports[index].connectTo = [song, render, guarded = QPointer<Track>(track), midiInput, stepMode, layerIds]
				(unsigned target) {
				if (!guarded)
				{
					return;
				}
				if (midiInput)
				{
					song->setNodeSourceMidiBoundary({}, *guarded);
				}
				else if (target > 0 && target <= static_cast<unsigned>(layerIds.size()))
				{
					song->setNodeInstrumentLayerEvent(*guarded, layerIds.at(target - 1), stepMode, true);
				}
				else if (stepMode)
				{
					song->setNodeSourceStepConnection(*guarded, true);
				}
				else
				{
					song->setNodeSourceEventConnection(*guarded, true);
				}
				render(render, guarded.data(), false);
			};
			ports[index].edit =
				[this, song, render, guarded = QPointer<Track>(track), midiInput, connected, stepMode](const QPoint& screen) {
					QMenu menu(this);
					menu.addAction(connected ? tr("Disconnect") : tr("Connect"));
					if (!menu.exec(screen) || !guarded)
					{
						return;
					}
					if (midiInput)
					{
						song->setNodeSourceMidiInputConnection(*guarded, !connected);
					}
					else if (stepMode)
					{
						song->setNodeSourceStepConnection(*guarded, !connected);
					}
					else
					{
						song->setNodeSourceEventConnection(*guarded, !connected);
					}
					render(render, guarded.data(), false);
				};
		}
		if (stepMode)
		{
			ports.removeFirst();
			ports.removeFirst();
		}
		for (unsigned side = 0; side < connections.audioOutputs.size(); ++side)
		{
			const auto label = trackPortLabel(side == 0 ? tr("Audio L") : tr("Audio R"));
			ports.append({QPointF(680, 130 + 30 * side), audio, label, false, {}, "source-audio", side, {}});
			ports.append({QPointF(625, 130 + 30 * side), audio, label, true,
				[this, song, render, guarded = QPointer<Track>(track), side](const QPoint& screen) {
					QMenu menu(this);
					auto* left = menu.addAction(tr("Connect to L"));
					auto* right = menu.addAction(tr("Connect to R"));
					menu.addAction(tr("Disconnect"));
					auto* choice = menu.exec(screen);
					if (!choice || !guarded)
					{
						return;
					}
					std::optional<ch_cnt_t> destination;
					if (choice == left || choice == right)
					{
						destination = choice == left ? 0 : 1;
					}
					song->setNodeSourceAudioOutput(*guarded, static_cast<ch_cnt_t>(side), destination);
					render(render, guarded.data(), false);
				}, "source-audio", side, [song, render, guarded = QPointer<Track>(track), side](unsigned target) {
					if (guarded)
					{
						song->setNodeSourceAudioOutput(*guarded, static_cast<ch_cnt_t>(side),
							static_cast<ch_cnt_t>(target));
						render(render, guarded.data(), false);
					}
				}});
		}
		for (auto& port : ports)
		{
			port.labelBackground = trackColor.value_or(QColor{});
		}
		if (!singing && hasPrimary)
		{
			auto* primaryInstrument = static_cast<InstrumentTrack*>(track)->instrument();
			if (isVstInstrument(primaryInstrument))
			{
				const auto prefix = QStringLiteral("source-plugin/") + track->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/';
				const int portStart = ports.size();
				appendVstPorts(primaryInstrument, prefix, nodes[stepMode ? 1 : 2], ports, 0, 0);
				std::optional<QPointF> vstMidiIn;
				QList<QPointF> vstAudioOuts;
				for (int i = portStart; i < ports.size(); ++i)
				{
					if (!ports[i].output && ports[i].graphSignal == QStringLiteral("midi") && !vstMidiIn)
					{
						vstMidiIn = ports[i].position;
					}
					else if (ports[i].output && ports[i].graphSignal == QStringLiteral("audio") && vstAudioOuts.size() < 2)
					{
						vstAudioOuts.append(ports[i].position);
					}
				}
				if (vstMidiIn)
				{
					for (int i = 0; i < cables.size(); ++i)
					{
						if (cables[i].source == QPointF(405, 130) && cables[i].destination == QPointF(460, 130))
						{
							if (useStepToMidi)
							{
								cables[i].destination = stepToMidiIn;
								stepToMidiInputConnected = true;
								cables.append({stepToMidiOut, *vstMidiIn, trackColor.value_or(midi),
									cables[i].active, cables[i].disconnect});
							}
							else
							{
								cables[i].destination = *vstMidiIn;
							}
							break;
						}
					}
				}
				for (int i = 0; i < cables.size(); ++i)
				{
					for (int side = 0; side < 2 && side < vstAudioOuts.size(); ++side)
					{
						if (cables[i].source == QPointF(625, 130 + 30 * side))
						{
							cables[i].source = vstAudioOuts[side];
							break;
						}
					}
				}
				ports.removeIf([](const Port& port) {
					return port.position == QPointF(460, 130)
						|| port.position == QPointF(625, 130)
						|| port.position == QPointF(625, 160);
				});
			}
		}
		if (useStepToMidi)
		{
			nodes.append({QRectF(stepToMidiOrigin, QSizeF(130, 84)), tr("Step to MIDI"), {}});
			nodes.back().layoutKey = layoutPrefix + QStringLiteral("step-to-midi");
			nodes.back().trackColor = trackColor.value_or(QColor{});
			ports.append({stepToMidiIn, eventColor, eventLabel, false, {}});
			ports.append({stepToMidiOut, midi, tr("MIDI"), true, {}});
		}
		appendSourceEffects(nodes, ports);
		appendSourceBoundaryPorts(QStringLiteral("source-audio"), nodes, cables, ports);
		if (!stepMode && !connections.defaultMidiInput)
		{
			const QPointF defaultMidiOutput(185, 130);
			nodes.removeFirst();
			ports.removeIf([&](const Port& port) { return port.position == defaultMidiOutput; });
			cables.removeIf([&](const auto& cable) { return cable.source == defaultMidiOutput; });
		}
		if (!hasPrimary)
		{
			const auto primaryPort = [](const QPointF& point) {
				return point == QPointF(460, 130) || point == QPointF(625, 130) || point == QPointF(625, 160);
			};
			nodes.removeAt(stepMode || !connections.defaultMidiInput ? 1 : 2);
			ports.removeIf([&](const Port& port) { return primaryPort(port.position); });
			cables.removeIf([&](const auto& cable) {
				return primaryPort(cable.source) || primaryPort(cable.destination);
			});
		}
		appendCustomChildren(layoutPrefix, nodes, ports, cables, redrawSource, {});
		canvas->setGraph(std::move(nodes), std::move(cables), std::move(ports));
		while (auto* item = patternLayout->takeAt(0))
		{
			if (auto* widget = item->widget())
			{
				widget->hide();
				widget->deleteLater();
			}
			delete item;
		}
		patternPanel->setVisible(patternInstrument);
		if (patternInstrument)
		{
			auto* selector = new QComboBox(patternPanel);
			selector->setToolTip(tr("Pattern"));
			for (int index = 0; index < patterns->numOfPatterns(); ++index)
			{
				const auto* entry = PatternTrack::findPatternTrack(index);
				selector->addItem(entry ? entry->name() : tr("Pattern %1").arg(index));
			}
			selector->setCurrentIndex(patterns->currentPattern());
			selector->setFixedWidth(199);
			connect(selector, qOverload<int>(&QComboBox::activated), patternPanel,
				[patterns](int index) { patterns->setCurrentPattern(index); });
			if (clip && clip->type() == MidiClip::Type::BeatClip)
			{
				auto* scroll = new QScrollArea(patternPanel);
				scroll->setFrameShape(QFrame::NoFrame);
				scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
				scroll->setWidget(MidiClipView::createPatternStepWidget(clip, scroll));
				patternLayout->addWidget(scroll, 1);
			}
			else if (clip)
			{
				auto* editorButton = new QPushButton(tr("Open Piano Roll"), patternPanel);
				patternLayout->addWidget(editorButton);
				connect(editorButton, &QPushButton::clicked, clip, [clip] {
					auto* roll = getGUI()->pianoRoll();
					roll->setCurrentMidiClip(clip);
					roll->parentWidget()->show();
					roll->show();
					roll->setFocus();
				});
				auto* scroll = new QScrollArea(patternPanel);
				scroll->setFrameShape(QFrame::NoFrame);
				scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
				scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
				patternLayout->addWidget(scroll, 1);
				auto* preview = new QLabel;
				preview->setObjectName("nodeWorkflowPatternPreview");
				scroll->setWidget(preview);
				const auto updatePreview = [clip, preview, canvas] {
					const auto length = std::max(1, int(clip->length()));
					const auto previewWidth = std::max(1, qRound(460.0 * length / TimePos::ticksPerBar()));
					QPixmap image(previewWidth, 17);
					image.fill(Qt::transparent);
					QPainter painter(&image);
					int low = NumKeys - 1;
					int high = 0;
					for (const auto* note : clip->notes())
					{
						if (int(note->length()) > 0)
						{
							low = std::min(low, note->key());
							high = std::max(high, note->key());
						}
					}
					for (const auto* note : clip->notes())
					{
						if (int(note->length()) > 0)
						{
							const qreal x = qreal(previewWidth) * int(note->pos()) / length;
							const qreal width = std::max(1.0, qreal(previewWidth) * int(note->length()) / length);
							const qreal y = high == low ? 8 : 14 - 12.0 * (note->key() - low) / (high - low);
							painter.fillRect(QRectF(x, y, width, 2),
								canvas->audioPortColor());
						}
					}
					preview->setPixmap(image);
					preview->resize(image.size());
				};
				connect(clip, &MidiClip::dataChanged, preview, updatePreview);
				updatePreview();
			}
			patternLayout->addWidget(selector);
		}
		if (fitCanvas)
		{
			canvas->fitGraph();
		}
	};
	const auto showTracks = [this, canvas, showSource, song, activeTracks, activeSource, patternPanel, add, footerStatus,
		clearTriggers, inspectTriggers, activeGraphScope, customPath, appendCustomChildren,
		setBreadcrumb, setInspector, breadcrumb, appendBoundaryModules, appendBoundaryProjection, bindProjectedOutput,
		appendVstPorts](const auto& render,
		const TrackContainer* container, bool fitCanvas) -> void
	{
		if (container == Engine::patternStore())
		{
			inspectTriggers(nullptr, std::nullopt, false, true);
		}
		else
		{
			clearTriggers();
		}
		customPath->clear();
		*activeGraphScope = container == song ? QStringLiteral("song/") : QStringLiteral("pattern/");
		*activeTracks = container;
		canvas->setClipboardScope(container == song ? NodeWorkflowCanvas::ClipboardScope::Song
			: NodeWorkflowCanvas::ClipboardScope::Pattern);
		setBreadcrumb({{"root", tr("Project")}, {"root", tr("Root Graph")},
			{{}, container == song ? tr("Multitrack") : tr("Pattern")}});
		footerStatus->setText(container == song
			? tr("Logical nodes · External device integration is planned separately")
			: tr("Audio ports connect one channel at a time"));
		const auto returnToRoot = [breadcrumb] { breadcrumb->linkActivated(QStringLiteral("root")); };
		if (container == song)
		{
			const auto inputs = song->nodeLogicalInputs();
			const auto midiInputs = std::count_if(inputs.cbegin(), inputs.cend(), [](const auto& input) {
				return input.signalType == QStringLiteral("midi");
			});
			std::function<void()> openSample;
			for (auto* track : song->tracks())
			{
				if (track->type() == Track::Type::Sample)
				{
					openSample = [showSource, selected = QPointer<Track>(track)] {
						if (selected)
						{
							showSource(showSource, selected.data(), true);
						}
					};
					break;
				}
			}
			setInspector(tr("Multitrack"), tr("Multiple MIDI and per-channel audio inputs"),
				{{tr("MIDI Inputs"), QString::number(midiInputs)},
					{tr("Audio Inputs"), QString::number(inputs.size() - midiInputs)}},
				{{tr("Open Sample Track"), openSample}, {tr("Back to Root Graph"), returnToRoot}});
		}
		else
		{
			setInspector(tr("Pattern"), tr("Pattern columns follow the arrangement timeline"),
				{{tr("Pattern Column"), QString::number(Engine::patternStore()->currentPattern() + 1)},
					{tr("Trigger Source"), tr("Pattern clips in the arrangement")}},
				{{tr("Open Pattern Editor"), [this] { togglePatternEditorWin(true); }},
					{tr("Back to Root Graph"), returnToRoot}});
		}
		activeSource->clear();
		canvas->setAcceptDrops(true);
		add->setEnabled(true);
		patternPanel->hide();
		using Port = NodeWorkflowCanvas::Port;
		QList<NodeWorkflowCanvas::Node> nodes;
		QList<NodeWorkflowCanvas::Connection> cables;
		QList<Port> ports;
		const auto audio = canvas->audioPortColor();
		const auto midi = canvas->midiPortColor();
		const auto group = QStringLiteral("track-mixer");
		const auto layoutPrefix = container == song ? QStringLiteral("song/") : QStringLiteral("pattern/");
		const bool inPattern = container == Engine::patternStore();
		const auto effectIsLocal = [song, inPattern](MixerChannel* channel) {
			return channel->m_nodeEffect && song->nodeCustomEffectScope(channel->nodeWorkflowId()).isEmpty()
				&& song->nodeEffectInPattern(channel->nodeWorkflowId()) == inPattern;
		};
		const qreal outputX = 800;
		qreal outputY = 88;
		const auto firstBoundaryPort = static_cast<unsigned>(Engine::mixer()->numChannels()) * 2;
		auto nextBoundaryPort = firstBoundaryPort;
		const auto reservedOutputs = song->nodeBoundaryPorts(layoutPrefix);
		const auto reservedPortBase = firstBoundaryPort
			+ 2 * static_cast<unsigned>(container->tracks().size()) + firstBoundaryPort;
		QHash<QString, QPointF> boundAudioDestinations;
		struct InputTarget
		{
			QPointer<Track> track;
			QString trackId;
			QString port;
			QPointF point;
			bool legacyMidi = false;
		};
		QList<InputTarget> inputTargets;
		const auto inputMatches = [](const auto& destinations, InputTarget& target) {
			if (destinations.contains(qMakePair(target.trackId, target.port)))
			{
				return true;
			}
			if (target.legacyMidi && destinations.contains(qMakePair(target.trackId, QStringLiteral("midi-in"))))
			{
				target.port = QStringLiteral("midi-in");
				return true;
			}
			return false;
		};
		auto* mixer = Engine::mixer();
		QList<QPointF> channelOrigins;
		qreal channelY = 40;
		for (mix_ch_t channel = 0; channel < mixer->numChannels(); ++channel)
		{
			const auto* target = mixer->mixerChannel(channel);
			channelOrigins.append(target->m_nodeEffectPosition.value_or(QPointF(515, channelY)));
			channelY += std::max<qreal>(120, 103 + 22.0 * target->m_sends.size());
		}
		const auto channelOrigin = [&channelOrigins](mix_ch_t channel) {
			return channelOrigins.at(channel);
		};
		QList<QUuid> channelIds;
		for (mix_ch_t channel = 0; channel < mixer->numChannels(); ++channel)
		{
			channelIds.append(mixer->mixerChannel(channel)->nodeWorkflowId());
		}
		const auto findChannel = [](const QUuid& id) -> std::optional<mix_ch_t> {
			auto* current = Engine::mixer();
			for (mix_ch_t channel = 0; channel < current->numChannels(); ++channel)
			{
				if (current->mixerChannel(channel)->nodeWorkflowId() == id)
				{
					return channel;
				}
			}
			return std::nullopt;
		};
		for (mix_ch_t channel = 0; channel < mixer->numChannels(); ++channel)
		{
			auto* target = mixer->mixerChannel(channel);
			if (!effectIsLocal(target))
			{
				continue;
			}
			const auto origin = channelOrigin(channel);
			nodes.append({QRectF(origin, QSizeF(190, 103)), target->m_name,
				target->m_nodeEffect ? std::function<void()>([this] { toggleMixerWin(); }) : std::function<void()>{}});
			nodes.back().layoutKey = layoutPrefix + QStringLiteral("mixer/")
				+ channelIds.at(channel).toString(QUuid::WithoutBraces);
			if (channel > 0)
			{
				const auto id = channelIds.at(channel);
				nodes.back().copy = [id, findChannel, song, activeTracks, render] {
					const auto index = findChannel(id);
					if (!index)
					{
						return std::function<void(const QPointF&)>{};
					}
					auto* source = Engine::mixer()->mixerChannel(*index);
					QDomDocument document;
					auto state = document.createElement("channel");
					document.appendChild(state);
					source->m_fxChain.saveState(document, state);
					const auto name = source->m_name;
					const auto volume = source->m_volumeModel.value();
					const bool effect = source->m_nodeEffect;
					return std::function<void(const QPointF&)>{[song, activeTracks, render, state, name, volume, effect]
						(const QPointF& position) {
						if (!song->isNodeProject() || !*activeTracks)
						{
							return;
						}
						const auto guard = Engine::audioEngine()->requestChangesGuard();
						auto* mixer = Engine::mixer();
						const auto index = mixer->createChannel();
						auto* copy = mixer->mixerChannel(index);
						copy->m_name = name;
						copy->m_volumeModel.setValue(volume);
						copy->m_fxChain.restoreState(state.firstChildElement());
						copy->m_nodeEffect = effect;
						if (effect)
						{
							copy->m_nodeEffectPosition = position;
							const bool pattern = *activeTracks == Engine::patternStore();
							copy->setNodePatternEffect(pattern);
							song->setNodeEffectInPattern(copy->nodeWorkflowId(), pattern);
							mixer->deleteChannelSend(index, 0);
						}
						song->setModified();
						render(render, *activeTracks, false);
					}};
				};
				nodes.back().remove = [this, id, findChannel, song, title = target->m_name] {
					if (QMessageBox::question(this, tr("Delete Node"), tr("Delete %1?").arg(title),
						QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
					{
						if (const auto index = findChannel(id))
						{
							Engine::mixer()->deleteChannel(*index);
							song->setModified();
						}
					}
				};
			}
			nodes.back().inspect = [this, song, setInspector, findChannel, id = channelIds.at(channel),
				remove = nodes.back().remove] {
				const auto index = findChannel(id);
				if (!index)
				{
					return;
				}
				auto* mixer = Engine::mixer();
				auto* selected = mixer->mixerChannel(*index);
				if (selected->m_nodeEffect)
				{
					const QPointer<Effect> effect(selected->m_fxChain.effectAt(0));
					std::function<void()> toggleBypass;
					if (effect)
					{
						toggleBypass = [song, effect] {
							if (effect)
							{
								effect->enabledModel()->setValue(!effect->isEnabled());
								song->setModified();
							}
						};
					}
					const auto fields = setInspector(selected->m_name, tr("Audio Effect"),
						{{tr("Input / Output"), tr("2 inputs / 2 outputs")},
							{tr("Bypass"), effect && effect->isEnabled() ? tr("Off") : tr("On")}},
						{{tr("Open Effect Rack"), [this] { toggleWindow(getGUI()->mixerView(), true); }},
							{tr("Toggle Bypass"), toggleBypass}});
					if (effect)
					{
						auto* bypass = fields.at(1);
						connect(effect->enabledModel(), &AutomatableModel::dataChanged, bypass,
							[this, effect, bypass] {
								if (effect)
								{
									bypass->setText(effect->isEnabled() ? tr("Off") : tr("On"));
								}
							});
					}
					return;
				}
				QStringList destinations;
				QStringList amounts;
				for (auto* send : selected->m_sends)
				{
					destinations.append(send->receiver()->m_name);
					amounts.append(QStringLiteral("%1%").arg(qRound(send->amount()->value() * 100)));
				}
				QList<InspectorAction> actions{
					{tr("Open Mixer"), [this] { toggleWindow(getGUI()->mixerView(), true); }},
					{tr("Add Mixer Channel"), [song] {
						Engine::mixer()->createChannel();
						song->setModified();
					}}};
				if (remove)
				{
					actions.append({tr("Remove %1").arg(selected->m_name), remove});
				}
				setInspector(tr("Mixer"), tr("Dynamic channels and sends"),
					{{tr("Selected Channel"), selected->m_name},
						{tr("Send Target"), destinations.join(QStringLiteral(" / "))},
						{tr("Send Amount"), amounts.join(QStringLiteral(" / "))}},
					actions);
			};
			ports.append({origin + QPointF(0, 48), audio, tr("Audio L"), false, {}, group,
				static_cast<unsigned>(channel) * 2, {}});
			const auto endpointId = channelIds.at(channel).toString(QUuid::WithoutBraces);
			const auto mixerEndpoint = QStringLiteral("mixer/") + endpointId;
			ports.back().graphEndpoint = mixerEndpoint + QStringLiteral("/in-L");
			ports.back().graphSignal = QStringLiteral("audio");
			ports.append({origin + QPointF(0, 76), audio, tr("Audio R"), false, {}, group,
				static_cast<unsigned>(channel) * 2 + 1, {}});
			ports.back().graphEndpoint = mixerEndpoint + QStringLiteral("/in-R");
			ports.back().graphSignal = QStringLiteral("audio");
			if (!target->m_nodeEffect)
			{
				continue;
			}
			for (ch_cnt_t side = 0; side < 2; ++side)
			{
				const auto source = origin + QPointF(190, 48 + 28 * side);
				const QPointF boundaryPoint(outputX, outputY);
				const auto boundaryPort = nextBoundaryPort++;
				const auto boundaryLabel = tr("%1 %2").arg(target->m_name, side == 0 ? "L" : "R");
				ports.append({boundaryPoint, audio, boundaryLabel, false, {}, group, boundaryPort, {}});
				outputY += 28;
				const auto id = channelIds.at(channel);
				const auto setBoundary = [id, side, findChannel](bool connected) {
					if (const auto index = findChannel(id))
					{
						Engine::mixer()->setNodeEffectBoundaryOutput(*index, side, connected);
					}
				};
				const bool boundaryConnected = target->nodeBoundaryOutputConnected(side);
				if (boundaryConnected)
				{
					cables.append({source, boundaryPoint, audio,
						[id = channelIds.at(channel), side, findChannel, effectIsLocal] {
							const auto index = findChannel(id);
							if (!index)
							{
								return false;
							}
							for (const auto* send : Engine::mixer()->mixerChannel(*index)->m_sends)
							{
								if (!effectIsLocal(send->receiver())
									&& (send->nodeSignalActive(side, 0) || send->nodeSignalActive(side, 1)))
								{
									return true;
								}
							}
							return false;
						}, [setBoundary] { setBoundary(false); }});
				}
				QList<QPair<QUuid, ch_cnt_t>> connected;
				for (auto* route : target->m_sends)
				{
					for (ch_cnt_t destination = 0; destination < 2; ++destination)
					{
						if ((route->channelPairs() & (1u << (side * 2 + destination))) != 0)
						{
							const auto receiver = route->receiverIndex();
							if (!effectIsLocal(route->receiver()))
							{
								continue;
							}
							connected.append(qMakePair(channelIds.at(receiver), destination));
							const QPointer<MixerRoute> guardedRoute(route);
							cables.append({source, channelOrigin(receiver) + QPointF(0, 48 + 28 * destination),
								audio, [guardedRoute, side, destination] {
									return guardedRoute && guardedRoute->nodeSignalActive(side, destination);
								}, [id, side, receiverId = channelIds.at(receiver), destination, findChannel] {
									const auto from = findChannel(id);
									const auto to = findChannel(receiverId);
									if (from && to)
									{
										Engine::mixer()->setNodeChannelSend(*from, side, *to, destination, false);
									}
								}});
						}
					}
				}
				ports.append({source, audio, side == 0 ? tr("Audio L") : tr("Audio R"), true,
					[this, id, side, connected, findChannel, boundaryConnected, setBoundary](const QPoint& screen) {
						QMenu menu(this);
						if (boundaryConnected)
						{
							menu.addAction(tr("Disconnect"), &menu, [setBoundary] { setBoundary(false); });
						}
						for (const auto& [receiverId, receiverSide] : connected)
						{
							const auto receiver = findChannel(receiverId);
							if (!receiver)
							{
								continue;
							}
							const auto label = Engine::mixer()->mixerChannel(*receiver)->m_name;
							auto* action = menu.addAction(tr("Disconnect %1 %2")
								.arg(label, receiverSide == 0 ? "L" : "R"));
							connect(action, &QAction::triggered, &menu,
								[id, side, receiverId, receiverSide, findChannel] {
									const auto from = findChannel(id);
									const auto to = findChannel(receiverId);
									if (from && to)
									{
										Engine::mixer()->setNodeChannelSend(*from, side, *to, receiverSide, false);
									}
								});
						}
						if (!menu.isEmpty())
						{
							menu.exec(screen);
						}
					}, group, 0, [id, side, channelIds, findChannel, boundaryPort, setBoundary](unsigned index) {
						if (index == boundaryPort)
						{
							setBoundary(true);
							return;
						}
						if (index / 2 >= static_cast<unsigned>(channelIds.size()))
						{
							return;
						}
						const auto from = findChannel(id);
						const auto to = findChannel(channelIds.at(index / 2));
						if (from && to)
						{
							Engine::mixer()->setNodeChannelSend(*from, side, *to, static_cast<ch_cnt_t>(index % 2), true);
						}
					}});
			}
			appendVstPorts(target->m_fxChain.effectAt(0), QStringLiteral("effect/") + endpointId + '/',
				nodes.back(), ports, 2, 2);
		}
		qreal y = 40;
		for (auto* track : container->tracks())
		{
			const auto type = track->type();
			if (type == Track::Type::Pattern || type == Track::Type::Automation)
			{
				continue;
			}
			const auto origin = song->nodeTrackPosition(*track).value_or(QPointF(35, y));
			NodeWorkflowCanvas::Node node{QRectF(origin, QSizeF(200, 103)), track->name(), {}};
			const auto trackColor = track->color();
			node.trackColor = trackColor.value_or(QColor{});
			const QPointer<Track> guarded(track);
			node.copy = [guarded, song, activeTracks, render] {
				if (!guarded)
				{
					return std::function<void(const QPointF&)>{};
				}
				QDomDocument document;
				auto state = document.createElement("clonedtrack");
				document.appendChild(state);
				guarded->saveState(document, state);
				QHash<QUuid, QDomElement> patternMembers;
				if (auto* pattern = dynamic_cast<PatternTrack*>(guarded.data()))
				{
					auto storedTrack = state.firstChildElement("track");
					storedTrack.firstChildElement("patterntrack").removeAttribute("sourcepattern");
					const auto column = pattern->patternIndex();
					for (auto* member : Engine::patternStore()->tracks())
					{
						if (column >= 0 && column < member->numOfClips())
						{
							auto snapshot = document.createElement("pattern-member");
							member->getClip(column)->saveState(document, snapshot);
							patternMembers.insert(member->nodeWorkflowId(), snapshot.firstChildElement());
						}
					}
				}
				auto* destination = guarded->trackContainer();
				return std::function<void(const QPointF&)>{[state, patternMembers, song, activeTracks, render, destination]
					(const QPointF& position) {
					if (!song->isNodeProject() || *activeTracks != destination)
					{
						return;
					}
					const auto guard = Engine::audioEngine()->requestChangesGuard();
					auto* copy = Track::create(state.firstChildElement(), destination);
					if (!copy)
					{
						return;
					}
					if (auto* pattern = dynamic_cast<PatternTrack*>(copy))
					{
						for (auto* member : Engine::patternStore()->tracks())
						{
							const auto snapshot = patternMembers.value(member->nodeWorkflowId());
							if (!snapshot.isNull())
							{
								auto* clip = member->getClip(pattern->patternIndex());
								const auto position = clip->startPosition();
								clip->restoreState(snapshot);
								clip->movePosition(position);
							}
						}
					}
					AutomationClip::resolveAllIDs();
					song->setNodeTrackPosition(*copy, position);
					song->setModified();
					render(render, destination, false);
				}};
			};
			node.remove = [this, guarded, song] {
				if (!guarded || QMessageBox::question(this, tr("Delete Node"),
					tr("Delete %1 and its clips?").arg(guarded->name()),
					QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
				{
					return;
				}
				if (guarded)
				{
					const auto guard = Engine::audioEngine()->requestChangesGuard();
					delete guarded.data();
					song->setModified();
				}
			};
			auto* patterns = Engine::patternStore();
			const auto column = patterns->currentPattern();
			const auto* patternClip = container == patterns && column >= 0 && column < track->numOfClips()
				? dynamic_cast<MidiClip*>(track->getClip(column)) : nullptr;
			const bool hasPianoRoll = track->type() == Track::Type::SVS
				|| (track->type() == Track::Type::Instrument && container == song)
				|| (patternClip && patternClip->type() == MidiClip::Type::MelodyClip);
			if (hasPianoRoll)
			{
				node.openPianoRoll = [this, guarded] {
					if (!guarded)
					{
						return;
					}
					auto* patterns = Engine::patternStore();
					const auto column = patterns->currentPattern();
					if (guarded->trackContainer() == patterns && (column < 0 || column >= guarded->numOfClips()))
					{
						return;
					}
					auto* clip = guarded->trackContainer() == patterns
						? guarded->getClip(column)
						: (guarded->getClips().empty() ? guarded->createClip(TimePos{}) : guarded->getClip(0));
					if (auto* voice = dynamic_cast<SVSClip*>(clip))
					{
						(new SVSPianoRoll(voice))->openIn(this);
					}
					else if (auto* midi = dynamic_cast<MidiClip*>(clip))
					{
						auto* roll = getGUI()->pianoRoll();
						roll->setCurrentMidiClip(midi);
						roll->parentWidget()->show();
						roll->show();
						roll->setFocus();
					}
				};
			}
			const auto trackPortLabel = [this, track](const QString& label) {
				return tr("%1 · %2").arg(track->name(), label);
			};
			if (track->type() == Track::Type::Instrument || track->type() == Track::Type::SVS
				|| track->type() == Track::Type::Sample)
			{
				node.open = [track = QPointer<Track>(track), showSource] {
					if (track)
					{
						showSource(showSource, track.data(), true);
					}
				};
			}
			if (track->type() == Track::Type::Pattern)
			{
				node.open = [render, track = QPointer<PatternTrack>(static_cast<PatternTrack*>(track))] {
					if (track && track->patternIndex() >= 0)
					{
						auto* patterns = Engine::patternStore();
						patterns->setCurrentPattern(track->patternIndex());
						render(render, patterns, true);
					}
				};
			}
			else if (track->type() == Track::Type::SVC)
			{
				const QPointer<Track> voiceTrack(track);
				node.open = [voiceTrack] {
					if (!voiceTrack)
					{
						return;
					}
					for (auto* view : getGUI()->songEditor()->m_editor->trackViews())
					{
						if (view->getTrack() == voiceTrack.data())
						{
							static_cast<SVCTrackView*>(view)->openWindow();
							return;
						}
					}
				};
				node.inspect = [this, song, voiceTrack, setInspector, open = node.open] {
					if (!voiceTrack)
					{
						return;
					}
					QStringList targets;
					for (ch_cnt_t side = 0; side < 2; ++side)
					{
						const auto route = song->nodeTrackMixerEndpoint(*voiceTrack, side);
						targets.append(route ? Engine::mixer()->mixerChannel(route->channel)->m_name : tr("Offline"));
					}
					targets.removeDuplicates();
					setInspector(tr("SVC Voice Track"), tr("Plays converted audio from its clips"),
						{{tr("Audio Source"), tr("Clip conversion result")},
							{tr("Mixer Target"), targets.join(QStringLiteral(" / "))}},
						{{tr("Open Conversion Settings"), open}});
				};
			}
			else if (track->type() == Track::Type::Automation)
			{
				const QPointer<Track> automationTrack(track);
				node.open = [automationTrack] {
					if (automationTrack && !automationTrack->getClips().empty())
					{
						auto* clip = static_cast<AutomationClip*>(automationTrack->getClips().front());
						getGUI()->automationEditor()->open(clip);
					}
				};
				node.inspect = [this, automationTrack, setInspector, open = node.open] {
					if (!automationTrack)
					{
						return;
					}
					QStringList parameters;
					QStringList clips;
					for (const auto* clip : automationTrack->getClips())
					{
						clips.append(clip->name());
						for (const auto& object : static_cast<const AutomationClip*>(clip)->objects())
						{
							if (object)
							{
								parameters.append(object->displayName());
							}
						}
					}
					parameters.removeDuplicates();
					setInspector(automationTrack->name(), tr("Automation curves control bound parameters"),
						{{tr("Bound Parameters"), parameters.join(QStringLiteral(" / "))},
							{tr("Automation Clips"), clips.join(QStringLiteral(" / "))}},
						{{tr("Open Automation Editor"), clips.isEmpty() ? std::function<void()>{} : open}});
				};
			}
			const auto trackId = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
			node.layoutKey = layoutPrefix + QStringLiteral("track/") + trackId;
			QHash<unsigned, qsizetype> projectedOutputs;
			const auto sourceScope = QStringLiteral("source/") + trackId + '/';
			const auto boundaryPorts = song->nodeBoundaryPorts(sourceScope);
			auto legacyMidi = std::find_if(boundaryPorts.cbegin(), boundaryPorts.cend(), [](const auto& boundary) {
				return !boundary.output && boundary.signal == "midi" && !boundary.track.isEmpty();
			});
			if (legacyMidi == boundaryPorts.cend())
			{
				legacyMidi = std::find_if(boundaryPorts.cbegin(), boundaryPorts.cend(), [](const auto& boundary) {
					return !boundary.output && boundary.signal == "midi";
				});
			}
			appendBoundaryProjection(sourceScope, node, ports,
				[&](const Song::NodeBoundaryPort& boundary, NodeWorkflowCanvas::Port& endpoint) {
					endpoint.labelBackground = trackColor.value_or(QColor{});
					if (boundary.output)
					{
						if (boundary.signal == "audio" && boundary.track == trackId)
						{
							projectedOutputs.insert(boundary.channel, ports.size() - 1);
						}
						return;
					}
					QString targetPort;
					if (boundary.signal == "midi"
						&& (track->type() == Track::Type::Instrument || track->type() == Track::Type::SVS))
					{
						targetPort = QStringLiteral("midi-in/") + boundary.id;
					}
					else if (boundary.signal == "audio" && track->type() == Track::Type::Sample)
					{
						targetPort = boundary.stereoSide == 1 ? QStringLiteral("audio-in:R")
							: QStringLiteral("audio-in:L");
					}
					if (!targetPort.isEmpty())
					{
						endpoint.connectionGroup = boundary.signal;
						endpoint.channel = static_cast<unsigned>(inputTargets.size());
						inputTargets.append({track, trackId, targetPort, endpoint.position,
							legacyMidi != boundaryPorts.cend() && legacyMidi->id == boundary.id});
					}
				});
			const auto extraHeight = node.bounds.height() - 103;
			nodes.append(std::move(node));
			switch (track->type())
			{
				case Track::Type::Instrument:
				case Track::Type::Sample:
				case Track::Type::SVS:
				case Track::Type::SVC:
					for (ch_cnt_t side = 0; side < 2; ++side)
					{
						const auto source = origin + QPointF(200, 48 + 28 * side);
						const QPointF boundaryPoint(outputX, outputY);
						const auto boundaryPort = nextBoundaryPort++;
						const auto trackId = track->nodeWorkflowId().toString(QUuid::WithoutBraces);
						const auto reservedOutput = std::find_if(reservedOutputs.cbegin(), reservedOutputs.cend(),
							[&trackId, side](const auto& port) {
								return port.output && port.signal == "audio" && port.track == trackId && port.channel == side;
							});
						const auto channelLabel = side == 0 ? tr("Audio L") : tr("Audio R");
						const auto boundaryLabel = tr("%1 %2").arg(track->name(), side == 0 ? "L" : "R");
						const bool hasReservedOutput = reservedOutput != reservedOutputs.cend();
						if (hasReservedOutput)
						{
							boundAudioDestinations.insert(reservedOutput->id, boundaryPoint);
						}
						else
						{
							ports.append({boundaryPoint, audio, boundaryLabel, false, {}, group, boundaryPort, {}});
							ports.back().labelBackground = trackColor.value_or(QColor{});
						}
						outputY += 28;
						const auto route = song->nodeTrackMixerEndpoint(*track, side);
						const bool internalEffect = route && effectIsLocal(mixer->mixerChannel(route->channel));
						const bool connected = song->nodeTrackBoundaryConnected(*track, side);
						const auto disconnect = [song, render, container, guarded = QPointer<Track>(track), side,
							internalEffect] {
							if (guarded)
							{
								if (internalEffect)
								{
									song->setNodeTrackAudioRoute(*guarded, side, std::nullopt, 0);
								}
								song->setNodeTrackBoundaryConnected(*guarded, side, false);
								render(render, container, false);
							}
						};
						if (connected)
						{
							const auto outputs = song->nodeSourceConnections(*track).audioOutputs;
							const auto destination = internalEffect
								? channelOrigin(route->channel) + QPointF(0, 48 + 28 * route->side)
								: boundaryPoint;
							cables.append({source, destination,
								trackColor.value_or(audio),
								[guarded = QPointer<Track>(track), side, outputs] {
									if (!guarded)
									{
										return false;
									}
									const auto* bus = nodeWorkflowAudioBus(*guarded);
									return (outputs[0] == side && bus->nodeSignalActive(0))
										|| (outputs[1] == side && bus->nodeSignalActive(1));
								}, disconnect});
						}
						ports.append({source, audio, trackPortLabel(channelLabel), true,
							[this, disconnect, connected](const QPoint& screen) {
								QMenu menu(this);
								auto* action = menu.addAction(tr("Disconnect"));
								action->setEnabled(connected);
								if (menu.exec(screen) == action)
								{
									disconnect();
								}
							},
							group, 0, [song, render, container, guarded = QPointer<Track>(track), side,
								channelIds, firstBoundaryPort, boundaryPort, internalEffect, effectIsLocal,
								layoutPrefix, reservedOutputs, reservedPortBase](unsigned target) {
								if (!guarded)
								{
									return;
								}
								if (target >= reservedPortBase)
								{
									const auto index = target - reservedPortBase;
									if (index < static_cast<unsigned>(reservedOutputs.size())
										&& song->setNodeBoundaryAudioSource(layoutPrefix, reservedOutputs.at(index).id,
											*guarded, side, true))
									{
										if (internalEffect)
										{
											song->setNodeTrackAudioRoute(*guarded, side, std::nullopt, 0);
										}
										render(render, container, false);
									}
									return;
								}
								if (target == boundaryPort)
								{
									if (internalEffect)
									{
										song->setNodeTrackAudioRoute(*guarded, side, std::nullopt, 0);
									}
									song->setNodeTrackBoundaryConnected(*guarded, side, true);
									render(render, container, false);
									return;
								}
								if (target >= firstBoundaryPort)
								{
									return;
								}
								const auto id = channelIds.at(target / 2);
								auto* current = Engine::mixer();
								for (mix_ch_t channel = 0; channel < current->numChannels(); ++channel)
								{
									if (current->mixerChannel(channel)->nodeWorkflowId() == id
										&& effectIsLocal(current->mixerChannel(channel)))
									{
										song->setNodeTrackAudioRoute(*guarded, side, channel,
											static_cast<ch_cnt_t>(target % 2));
										song->setNodeTrackBoundaryConnected(*guarded, side, true);
										render(render, container, false);
										return;
									}
								}
							}});
						ports.back().labelBackground = trackColor.value_or(QColor{});
						if (const auto projected = projectedOutputs.constFind(side); projected != projectedOutputs.cend())
						{
							bindProjectedOutput(ports.back(), ports[*projected], cables);
						}
						else
						{
							ports.back().graphEndpoint = QStringLiteral("track-signal/") + trackId
									+ (side == 0 ? QStringLiteral("/out-L") : QStringLiteral("/out-R"));
							ports.back().graphSignal = QStringLiteral("audio");
						}
					}
					break;
				default:
					break;
			}
			y += 120 + extraHeight;
		}
		if (outputY > 88)
		{
			nodes.append({QRectF(outputX, 40, 190, outputY - 40), tr("Audio Output"), {}});
			nodes.back().layoutKey = layoutPrefix + QStringLiteral("audio-output");
		}
		if (container == song)
		{
			qreal inputY = 40;
			int midiNumber = 0;
			int audioNumber = 0;
			for (const auto& input : song->nodeLogicalInputs())
			{
				const bool isMidi = input.signalType == "midi";
				const auto color = isMidi ? midi : audio;
				const auto name = isMidi ? tr("MIDI Input %1").arg(++midiNumber)
					: tr("Audio Input %1").arg(++audioNumber);
				const QPointF source(-70, inputY + 48);
				nodes.append({QRectF(-255, inputY, 185, 80), name, {}});
				nodes.back().layoutKey = layoutPrefix + QStringLiteral("input/") + input.id;
				nodes.back().inspect = [this, setInspector, name, isMidi, count = input.destinations.size()] {
					setInspector(name, tr("Logical input for graph routing"),
						{{tr("Signal Type"), isMidi ? tr("MIDI") : tr("Mono")},
							{tr("Connected Targets"), QString::number(count)},
							{tr("Device Binding"), tr("Offline")}},
						{});
				};
				QList<InputTarget> connected;
				for (auto target : inputTargets)
				{
					if (inputMatches(input.destinations, target))
					{
						connected.append(target);
						cables.append({source, target.point,
							target.track ? target.track->color().value_or(color) : color, {},
							[song, render, input, target] {
								if (target.track)
								{
									song->setNodeLogicalInputTarget(input.id, input.signalType,
										*target.track, target.port, false);
									render(render, song, false);
								}
							}});
					}
				}
				ports.append({source, color, isMidi ? tr("MIDI") : tr("Mono"), true,
					[this, song, render, input, connected](const QPoint& screen) {
						QMenu menu(this);
						for (const auto& target : connected)
						{
							if (!target.track)
							{
								continue;
							}
							auto* action = menu.addAction(tr("Disconnect %1 · %2")
								.arg(target.track->name(), target.port));
							connect(action, &QAction::triggered, &menu, [song, render, input, target] {
								if (target.track)
								{
									song->setNodeLogicalInputTarget(input.id, input.signalType,
										*target.track, target.port, false);
									render(render, song, false);
								}
							});
						}
						if (!menu.isEmpty())
						{
							menu.exec(screen);
						}
					}, input.signalType, 0, [song, render, input, inputTargets](unsigned index) {
						if (index >= static_cast<unsigned>(inputTargets.size()))
						{
							return;
						}
						const auto& target = inputTargets.at(index);
						if (target.track)
						{
							song->setNodeLogicalInputTarget(input.id, input.signalType,
								*target.track, target.port, true);
							render(render, song, false);
						}
					}});
				inputY += 95;
			}
		}
		appendBoundaryModules(layoutPrefix, nodes, ports,
			[render, activeTracks, container] {
				if (*activeTracks == container)
				{
					render(render, container, false);
				}
			},
			[&](const Song::NodeBoundaryPort& port, Port& endpoint) {
				if (!port.output)
				{
					const auto route = [song, render, container, id = port.id, signal = port.signal](
						const InputTarget& target, bool connected) {
						if (target.track && song->setNodeLogicalInputTarget(id, signal, *target.track, target.port, connected))
						{
							render(render, container, false);
						}
					};
					endpoint.connectionGroup = port.signal;
					endpoint.connectTo = [route, inputTargets](unsigned index) {
						if (index < static_cast<unsigned>(inputTargets.size()))
						{
							route(inputTargets.at(index), true);
						}
					};
					for (auto target : inputTargets)
					{
						if (inputMatches(port.destinations, target))
						{
							cables.append({endpoint.position, target.point, endpoint.color, {},
								[route, target] { route(target, false); }});
						}
					}
					return;
				}
				if (!port.output || port.signal != "audio")
				{
					return;
				}
				const auto index = std::find_if(reservedOutputs.cbegin(), reservedOutputs.cend(),
					[&port](const auto& candidate) { return candidate.id == port.id; }) - reservedOutputs.cbegin();
				endpoint.connectionGroup = group;
				endpoint.channel = reservedPortBase + static_cast<unsigned>(index);
				const auto oldPoint = boundAudioDestinations.constFind(port.id);
				if (oldPoint != boundAudioDestinations.cend())
				{
					for (auto& cable : cables)
					{
						if (cable.destination == *oldPoint)
						{
							cable.destination = endpoint.position;
						}
					}
				}
			});
		appendCustomChildren(layoutPrefix, nodes, ports, cables, [render, container] { render(render, container, false); });
		canvas->setGraph(std::move(nodes), std::move(cables), std::move(ports));
		if (fitCanvas)
		{
			canvas->fitGraph();
		}
	};
	const auto showRoot = [this, canvas, song, showTracks, activeTracks, activeSource, patternPanel, add, footerStatus,
		clearTriggers, activeGraphScope, customPath, appendCustomChildren,
		setBreadcrumb, setInspector, appendBoundaryProjection, bindProjectedOutput](
		const auto& render, bool fitCanvas) -> void
	{
		clearTriggers();
		customPath->clear();
		*activeGraphScope = QStringLiteral("root/");
		*activeTracks = nullptr;
		setBreadcrumb({{"root", tr("Project")}, {{}, tr("Root Graph")}});
		canvas->setClipboardScope(NodeWorkflowCanvas::ClipboardScope::Root);
		footerStatus->setText(tr("Workspace  /  Root Graph"));
		const auto inputs = song->nodeLogicalInputs();
		const auto openMultitrack = [song, showTracks] { showTracks(showTracks, song, true); };
		const auto& songTracks = song->tracks();
		const auto& patternTracks = Engine::patternStore()->tracks();
		const auto& meter = song->getTimeSigModel();
		const bool hasDefaultTracks = songTracks.size() == 4 && patternTracks.size() == 1
			&& songTracks[0]->type() == Track::Type::Instrument
			&& songTracks[1]->type() == Track::Type::Sample
			&& songTracks[2]->type() == Track::Type::Pattern
			&& songTracks[3]->type() == Track::Type::Automation
			&& songTracks[0]->name() == QStringLiteral("TripleOscillator")
			&& patternTracks.front()->name() == QStringLiteral("Kicker");
		if (hasDefaultTracks && inputs.isEmpty() && Engine::mixer()->numChannels() == 1)
		{
			setInspector(tr("Default Project Layout"), tr("LMMS default template tracks"),
				{{tr("Tempo and Meter"), tr("%1 BPM · %2/%3")
						.arg(song->getTempo()).arg(meter.getNumerator()).arg(meter.getDenominator())},
					{tr("Instrument Track"), songTracks[0]->name()},
					{tr("Other Tracks"), QStringList{songTracks[1]->name(), songTracks[2]->name(),
						songTracks[3]->name()}.join(QStringLiteral(" / "))},
					{tr("Pattern Member"), patternTracks.front()->name()}},
				{{tr("Open Multitrack Graph"), openMultitrack},
					{tr("Open Song Editor"), [this] { toggleWindow(getGUI()->songEditor(), true); }}});
		}
		else
		{
			setInspector(tr("Root Graph"), tr("Project audio, MIDI, and logical device connections"),
				{{tr("Logical Inputs"), QString::number(inputs.size())},
					{tr("Master Output"), tr("Independent L / R ports")}},
				{{tr("Open Multitrack Graph"), openMultitrack},
					{tr("Open Audio Settings"), [this] { showSettingsDialog(); }}});
		}
		activeSource->clear();
		canvas->setAcceptDrops(true);
		add->setEnabled(true);
		patternPanel->hide();
		const auto audio = canvas->audioPortColor();
		const auto midi = canvas->midiPortColor();
		using Port = NodeWorkflowCanvas::Port;
		QList<Port> ports;
		QList<NodeWorkflowCanvas::Connection> cables;
		const auto addStandaloneActions = [this, song, activeTracks, activeSource, render]
			(NodeWorkflowCanvas::Node& node, const QString& kind, const QString& id) {
			node.copy = [song, activeTracks, activeSource, render, kind, id] {
				const auto state = song->copyStandaloneNode(kind, id);
				return std::function<void(const QPointF&)>{[song, activeTracks, activeSource, render, state]
					(const QPointF&) {
					if (!*activeTracks && activeSource->isNull() && song->pasteStandaloneNode(state))
					{
						render(render, false);
					}
				}};
			};
			node.remove = [this, song, render, kind, id, title = node.title] {
				if (QMessageBox::question(this, tr("Delete Node"), tr("Delete %1?").arg(title),
					QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes
					&& song->removeStandaloneNode(kind, id))
				{
					render(render, false);
				}
			};
		};
		QList<NodeWorkflowCanvas::Node> nodes{
			{QRectF(750, 80, 150, 130), tr("Master Output"), {}},
		};
		nodes.front().layoutKey = QStringLiteral("root/output");
		nodes.front().inspect = [this, setInspector] {
			setInspector(tr("Master Output"), tr("Master connects to the configured audio output"),
				{{tr("Channels"), tr("Independent L / R ports")}},
				{{tr("Open Audio Settings"), [this] { showSettingsDialog(); }}});
		};
		auto* mixer = Engine::mixer();
		const auto channelLabel = [this, mixer](mix_ch_t channel, ch_cnt_t side) {
			return tr("%1 %2").arg(mixer->mixerChannel(channel)->m_name, side == 0 ? "L" : "R");
		};
		for (mix_ch_t channel = 0; channel < mixer->numChannels(); ++channel)
		{
			for (ch_cnt_t side = 0; side < 2; ++side)
			{
				const qreal inputY = 132 + 48.0 * channel + 22 * side;
				ports.append({QPointF(395, inputY), audio, channelLabel(channel, side), false,
					{}, QStringLiteral("root-track-mixer"), static_cast<unsigned>(channel) * 2 + side, {}});
				const auto id = mixer->mixerChannel(channel)->nodeWorkflowId();
				if (song->nodeCustomEffectScope(id).isEmpty())
				{
					ports.back().graphEndpoint = QStringLiteral("mixer/")
						+ id.toString(QUuid::WithoutBraces)
						+ (side == 0 ? QStringLiteral("/in-L") : QStringLiteral("/in-R"));
					ports.back().graphSignal = QStringLiteral("audio");
				}
			}
		}
		nodes.append({QRectF(395, 80, 205, 72 + 48.0 * mixer->numChannels()), tr("Mixer"),
			[this] { toggleMixerWin(); }});
		nodes.back().layoutKey = QStringLiteral("root/mixer");

		qreal top = 30;
		const auto stepPatterns = song->nodeStepPatterns();
		const auto pianoRolls = song->nodePianoRolls();
		QList<QPair<QString, QPointF>> stepBoundaryTargets;
		QList<QPair<QString, QPointF>> midiBoundaryTargets;
		QList<QPair<QString, QPointF>> voiceBoundaryTargets;
		constexpr unsigned stepBoundaryBase = 1u << 31;
		const auto stepGroup = QStringLiteral("standalone-step-events");
		const auto midiGroup = QStringLiteral("standalone-midi-events");
		const auto voiceGroup = QStringLiteral("standalone-voice-events");
		for (auto* container : {static_cast<TrackContainer*>(song), static_cast<TrackContainer*>(Engine::patternStore())})
		{
			const bool multiTrack = container == song;
			const auto logicalInputs = multiTrack ? song->nodeLogicalInputs() : QList<Song::NodeLogicalInput>{};
			qreal wireY = top + 48;
			for (mix_ch_t channel = 1; channel < mixer->numChannels(); ++channel)
			{
				auto* effect = mixer->mixerChannel(channel);
				if (!effect->m_nodeEffect || !song->nodeCustomEffectScope(effect->nodeWorkflowId()).isEmpty()
					|| song->nodeEffectInPattern(effect->nodeWorkflowId()) == multiTrack)
				{
					continue;
				}
				for (ch_cnt_t side = 0; side < 2; ++side)
				{
					const QPointF source(225, wireY);
					ports.append({source, audio, channelLabel(channel, side), true, {},
						QStringLiteral("root-track-mixer"), 0, [song, channel, side, multiTrack](unsigned target) {
							auto* current = Engine::mixer();
							const auto index = static_cast<mix_ch_t>(target / 2);
							if (index >= current->numChannels())
							{
								return;
							}
							auto* receiver = current->mixerChannel(index);
							if (!receiver->m_nodeEffect
								|| song->nodeEffectInPattern(receiver->nodeWorkflowId()) == multiTrack)
							{
								current->setNodeChannelSend(channel, side, index, static_cast<ch_cnt_t>(target % 2), true);
							}
						}});
					for (auto* route : effect->m_sends)
					{
						auto* receiver = route->receiver();
						if (receiver->m_nodeEffect
							&& song->nodeEffectInPattern(receiver->nodeWorkflowId()) != multiTrack)
						{
							continue;
						}
						const auto target = route->receiverIndex();
						for (ch_cnt_t destination = 0; destination < 2; ++destination)
						{
							if ((route->channelPairs() & (1u << (side * 2 + destination))) == 0)
							{
								continue;
							}
							cables.append({source, QPointF(395, 132 + 48.0 * target + 22 * destination), audio,
								[guarded = QPointer<MixerRoute>(route), side, destination] {
									return guarded && guarded->nodeSignalActive(side, destination);
								}, [channel, side, target, destination] {
									Engine::mixer()->setNodeChannelSend(channel, side, target, destination, false);
								}});
						}
					}
					wireY += 22;
				}
			}
			QHash<QPair<QString, unsigned>, qsizetype> requiredOutputs;
			for (auto* track : container->tracks())
			{
				if (!nodeWorkflowAudioBus(*track))
				{
					continue;
				}
				const auto outputs = song->nodeSourceConnections(*track).audioOutputs;
				for (ch_cnt_t side = 0; side < 2; ++side)
				{
					const QPointF source(225, wireY);
					const auto updateRoute = [song, guarded = QPointer<Track>(track), side, render](
						std::optional<mix_ch_t> channel, ch_cnt_t input) {
						if (guarded && song->setNodeTrackAudioRoute(*guarded, side, channel, input))
						{
							render(render, false);
						}
					};
					ports.append({source, audio, tr("%1 %2").arg(track->name(), side == 0 ? "L" : "R"), true,
						[this, updateRoute, connected = song->nodeTrackMixerEndpoint(*track, side).has_value()](
							const QPoint& screen) {
							QMenu menu(this);
							auto* disconnect = menu.addAction(tr("Disconnect"));
							disconnect->setEnabled(connected);
							if (menu.exec(screen) == disconnect)
							{
								updateRoute(std::nullopt, 0);
							}
						}, QStringLiteral("root-track-mixer"), 0, [song, updateRoute, multiTrack](unsigned target) {
							auto* current = Engine::mixer();
							const auto index = static_cast<mix_ch_t>(target / 2);
							if (index >= current->numChannels())
							{
								return;
							}
							auto* receiver = current->mixerChannel(index);
							if (!receiver->m_nodeEffect
								|| song->nodeEffectInPattern(receiver->nodeWorkflowId()) == multiTrack)
							{
								updateRoute(index, static_cast<ch_cnt_t>(target % 2));
							}
						}});
					ports.back().labelBackground = track->color().value_or(QColor{});
					requiredOutputs.insert(qMakePair(track->nodeWorkflowId().toString(QUuid::WithoutBraces),
						static_cast<unsigned>(side)), ports.size() - 1);
					if (const auto route = song->nodeTrackMixerEndpoint(*track, side);
						route && (!mixer->mixerChannel(route->channel)->m_nodeEffect
							|| song->nodeEffectInPattern(mixer->mixerChannel(route->channel)->nodeWorkflowId()) == multiTrack))
					{
						cables.append({source, QPointF(395, 132 + 48.0 * route->channel + 22 * route->side),
							track->color().value_or(audio),
							[song, guarded = QPointer<Track>(track), side, outputs] {
								if (!guarded || !song->nodeTrackBoundaryConnected(*guarded, side))
								{
									return false;
								}
								const auto* bus = nodeWorkflowAudioBus(*guarded);
								return (outputs[0] == side && bus->nodeSignalActive(0))
									|| (outputs[1] == side && bus->nodeSignalActive(1));
							}, [updateRoute] { updateRoute(std::nullopt, 0); }});
					}
					wireY += 22;
				}
			}
			qreal inputY = top + 48;
			int midiIndex = 0;
			int audioIndex = 0;
			for (const auto& input : logicalInputs)
			{
				const bool isMidi = input.signalType == "midi";
				const auto color = isMidi ? midi : audio;
				const auto name = isMidi ? tr("MIDI Input %1").arg(++midiIndex)
					: tr("Audio Input %1").arg(++audioIndex);
				nodes.append({QRectF(-190, inputY - 36, 160, 62), name, {}});
				nodes.back().layoutKey = QStringLiteral("root/input/") + input.id;
				addStandaloneActions(nodes.back(), QStringLiteral("aggregate-input"), input.id);
				nodes.back().inspect = [this, setInspector, openMultitrack, name, isMidi,
					count = input.destinations.size()] {
					setInspector(name, tr("Logical input for graph routing"),
						{{tr("Signal Type"), isMidi ? tr("MIDI") : tr("Mono")},
							{tr("Connected Targets"), QString::number(count)},
							{tr("Device Binding"), tr("Offline")}},
						{{tr("Open Multitrack Graph"), openMultitrack}});
				};
				ports.append({QPointF(-30, inputY), color, isMidi ? tr("MIDI") : tr("Mono"), true, {}});
				if (isMidi)
				{
					ports.back().graphEndpoint = QStringLiteral("logical-midi/") + input.id;
					ports.back().connectionGroup = QStringLiteral("custom:midi");
				}
				ports.append({QPointF(20, inputY), color, name, false, {}});
				cables.append({QPointF(-30, inputY), QPointF(20, inputY), color});
				inputY += 76;
			}
			const auto bottom = std::max({wireY + 12, inputY - 38, top + 90});
			nodes.append({QRectF(20, top, 205, bottom - top),
				multiTrack ? tr("Song Editor", "node title") : tr("Pattern Editor", "node title"),
				[container, showTracks] { showTracks(showTracks, container, true); }});
			nodes.back().layoutKey = multiTrack ? QStringLiteral("root/song") : QStringLiteral("root/pattern");
			nodes.back().inspect = [this, setInspector, container, multiTrack, open = nodes.back().open] {
				setInspector(multiTrack ? tr("Multitrack") : tr("Pattern"),
					tr("Audio ports connect one channel at a time"),
					{{tr("Connected Tracks"), QString::number(container->tracks().size())}},
					{{multiTrack ? tr("Open Multitrack Graph") : tr("Open Pattern Editor"), open}});
			};
			const auto scope = multiTrack ? QStringLiteral("song/") : QStringLiteral("pattern/");
			auto firstProjection = ports.size();
			appendBoundaryProjection(scope, nodes.back(), ports);
			for (const auto& boundary : song->nodeBoundaryPorts(scope))
			{
				auto& endpoint = ports[firstProjection++];
				if (boundary.output)
				{
					const auto required = requiredOutputs.constFind(qMakePair(boundary.track, boundary.channel));
					if (boundary.signal == "audio" && required != requiredOutputs.cend())
					{
						bindProjectedOutput(ports[*required], endpoint, cables);
					}
					continue;
				}
				if (!multiTrack && boundary.signal == "step")
				{
					endpoint.connectionGroup = stepGroup;
					endpoint.channel = stepBoundaryBase + static_cast<unsigned>(stepBoundaryTargets.size());
					stepBoundaryTargets.append(qMakePair(boundary.id, endpoint.position));
				}
				else if (boundary.signal == "midi" || (multiTrack && boundary.signal == "voice"))
				{
					const bool voice = boundary.signal == "voice";
					auto& targets = voice ? voiceBoundaryTargets : midiBoundaryTargets;
					endpoint.connectionGroup = voice ? voiceGroup : midiGroup;
					endpoint.channel = stepBoundaryBase + static_cast<unsigned>(targets.size());
					targets.append(qMakePair(boundary.id, endpoint.position));
				}
			}
			top = nodes.back().bounds.bottom() + 45;
		}
		qreal stepY = 30 + 76 * song->nodeLogicalInputs().size();
		for (const auto& pattern : stepPatterns)
		{
			const QRectF bounds(-260, stepY, 205, 92);
			nodes.append({bounds, tr("Empty Step Sequencer"), [this, song, id = pattern.id] {
				const auto patterns = song->nodeStepPatterns();
				const auto found = std::find_if(patterns.cbegin(), patterns.cend(), [&id](const auto& item) {
					return item.id == id;
				});
				if (found == patterns.cend())
				{
					return;
				}
				QDialog editor(this);
				editor.setWindowTitle(tr("Step Sequencer"));
				auto* row = new QHBoxLayout(&editor);
				row->setSpacing(3);
				for (int index = 0; index < Song::NodeStepPattern::StepCount; ++index)
				{
					auto* step = new QToolButton(&editor);
					step->setCheckable(true);
					step->setChecked((found->activeSteps & (1u << index)) != 0);
					step->setAccessibleName(tr("Step %1").arg(index + 1));
					step->setIconSize(QSize(19, 24));
					step->setFixedSize(19, 24);
					const auto updateIcon = [step, index](bool enabled) {
						step->setIcon(embed::getIconPixmap(enabled ? "pattern_step_active"
							: (index % 4 == 0 ? "pattern_step_accent" : "pattern_step_inactive"), 19, 24));
					};
					updateIcon(step->isChecked());
					connect(step, &QToolButton::toggled, &editor, [song, id, index, updateIcon](bool enabled) {
						if (song->setNodeStep(id, index, enabled))
						{
							updateIcon(enabled);
						}
					});
					row->addWidget(step);
				}
				editor.exec();
			}});
			addStandaloneActions(nodes.back(), QStringLiteral("step-pattern"), pattern.id);
			nodes.back().layoutKey = QStringLiteral("root/step/") + pattern.id;
			nodes.back().inspect = [this, song, setInspector, id = pattern.id, open = nodes.back().open] {
				for (const auto& current : song->nodeStepPatterns())
				{
					if (current.id != id)
					{
						continue;
					}
					int active = 0;
					for (int step = 0; step < Song::NodeStepPattern::StepCount; ++step)
					{
						active += (current.activeSteps & (1u << step)) != 0;
					}
					setInspector(tr("Step Sequencer"), tr("Independent Pattern step data"),
						{{tr("Active Steps"), QStringLiteral("%1 / %2").arg(active).arg(Song::NodeStepPattern::StepCount)},
							{tr("Connected Tracks"), QString::number(current.targets.size())}},
						{{tr("Open Step Sequencer"), open}});
					break;
				}
			};
			const QPointF source(bounds.right(), stepY + 50);
			const auto connectBoundary = [song, render, id = pattern.id](const QString& target, bool connected) {
				if (song->setNodeStepBoundaryTarget(id, target, connected))
				{
					render(render, false);
				}
			};
			for (const auto& [id, destination] : stepBoundaryTargets)
			{
				if (pattern.boundaryTargets.contains(id))
				{
					cables.append({source, destination, canvas->stepPortColor(), {},
						[connectBoundary, id] { connectBoundary(id, false); }});
				}
			}
			ports.append({source, canvas->stepPortColor(), tr("Step Events"), true, {}, stepGroup, 0,
				[connectBoundary, stepBoundaryTargets](unsigned target) {
					if (target >= stepBoundaryBase
						&& target - stepBoundaryBase < static_cast<unsigned>(stepBoundaryTargets.size()))
					{
						connectBoundary(stepBoundaryTargets.at(target - stepBoundaryBase).first, true);
					}
				}});
			stepY += 122;
		}
		for (const auto& roll : pianoRolls)
		{
			const QRectF bounds(-260, stepY, 205, 92);
			nodes.append({bounds, roll.singingVoice ? tr("Empty SVS Piano Roll") : tr("Empty MIDI Piano Roll"),
				[this, song, roll] {
					auto* clip = song->nodePianoRollClip(roll.id);
					if (!clip)
					{
						return;
					}
					if (roll.singingVoice)
					{
						auto* editor = new SVSPianoRoll(static_cast<SVSClip*>(clip));
						editor->openIn(this);
						const auto closeEditor = [editor] {
							if (auto* frame = qobject_cast<SubWindow*>(editor->parentWidget()))
							{
								frame->hide();
								frame->deleteLater();
							}
						};
						connect(song, &Song::projectAboutToReset, editor, closeEditor);
						connect(clip, &Clip::destroyedClip, editor, closeEditor);
					}
					else
					{
						auto* editor = getGUI()->pianoRoll();
						editor->setCurrentMidiClip(static_cast<MidiClip*>(clip));
						editor->parentWidget()->show();
						editor->show();
						editor->setFocus();
					}
				}});
			nodes.back().openPianoRoll = nodes.back().open;
			addStandaloneActions(nodes.back(), QStringLiteral("piano-roll"), roll.id);
			nodes.back().layoutKey = QStringLiteral("root/piano/") + roll.id;
			nodes.back().inspect = [this, song, setInspector, roll, open = nodes.back().open] {
				const auto* clip = song->nodePianoRollClip(roll.id);
				if (!clip)
				{
					return;
				}
				const auto notes = roll.singingVoice ? static_cast<qsizetype>(static_cast<const SVSClip*>(clip)->notes().size())
					: static_cast<qsizetype>(static_cast<const MidiClip*>(clip)->notes().size());
				setInspector(roll.singingVoice ? tr("Empty SVS Piano Roll") : tr("Empty MIDI Piano Roll"),
					roll.singingVoice ? tr("Supports notes, lyrics, and voice curves")
						: tr("The same clip can be edited from the graph or track"),
					{{roll.singingVoice ? tr("Voice Notes") : tr("Notes"), QString::number(notes)},
						{tr("Placement"), tr("Standalone Node")}},
					{{roll.singingVoice ? tr("Open SVS Piano Roll") : tr("Open Piano Roll"), open}});
			};
			const QPointF source(bounds.right(), stepY + 50);
			const auto& boundaryTargets = roll.singingVoice ? voiceBoundaryTargets : midiBoundaryTargets;
			const auto& group = roll.singingVoice ? voiceGroup : midiGroup;
			const auto eventColor = roll.singingVoice ? canvas->voicePortColor() : midi;
			const auto eventLabel = roll.singingVoice ? tr("Voice Events") : tr("MIDI");
			{
				const auto connectBoundary = [song, render, id = roll.id](const QString& port, bool connected) {
					if (song->setNodePianoRollBoundaryTarget(id, port, connected))
					{
						render(render, false);
					}
				};
				for (const auto& [id, destination] : boundaryTargets)
				{
					if (roll.boundaryTargets.contains(id))
					{
						cables.append({source, destination, eventColor, {},
							[connectBoundary, id] { connectBoundary(id, false); }});
					}
				}
				ports.append({source, eventColor, eventLabel, true, {}, group, 0,
					[connectBoundary, boundaryTargets](unsigned target) {
						if (target >= stepBoundaryBase
							&& target - stepBoundaryBase < static_cast<unsigned>(boundaryTargets.size()))
						{
							connectBoundary(boundaryTargets.at(target - stepBoundaryBase).first, true);
						}
					}});
			}
			stepY += 122;
		}
		for (unsigned side = 0; side < 2; ++side)
		{
			const auto label = side == 0 ? tr("Audio L") : tr("Audio R");
			const qreal y = 133 + 30 * side;
			const bool connected = (mixer->nodeMasterOutputMask() & (1u << side)) != 0;
			const auto group = side == 0 ? QStringLiteral("master-out-left") : QStringLiteral("master-out-right");
			ports.append({QPointF(600, y), audio, label, true,
				[this, song, render, side, connected](const QPoint& screen) {
					QMenu menu(this);
					menu.addAction(connected ? tr("Disconnect") : tr("Connect"));
					if (menu.exec(screen))
					{
						song->setNodeMasterOutputConnected(side, !connected);
						render(render, false);
					}
				}, group, side, [song, render, side](unsigned) {
					song->setNodeMasterOutputConnected(side, true);
					render(render, false);
				}});
			ports.append({QPointF(750, y), audio, label, false, {}, group, side, {}});
			if (connected)
			{
				cables.append({QPointF(600, y), QPointF(750, y), audio,
					[side] { return Engine::mixer()->nodeMasterSignalActive(side); }, [song, render, side] {
						song->setNodeMasterOutputConnected(side, false);
						render(render, false);
					}});
			}
		}
		appendCustomChildren(*activeGraphScope, nodes, ports, cables, [render] { render(render, false); });
		canvas->setGraph(std::move(nodes), std::move(cables), std::move(ports));
		if (fitCanvas)
		{
			canvas->fitGraph();
		}
	};
	connect(back, &QToolButton::clicked, canvas, [showRoot, showCustom, customPath, returnFromCustom] {
		if (customPath->size() > 1)
		{
			customPath->removeLast();
			showCustom(showCustom, true);
		}
		else if (!customPath->isEmpty() && *returnFromCustom)
		{
			const auto redraw = *returnFromCustom;
			*returnFromCustom = {};
			redraw();
		}
		else
		{
			showRoot(showRoot, true);
		}
	});
	connect(breadcrumb, &QLabel::linkActivated, canvas, [song, showRoot, showTracks, showCustom, customPath](const QString& destination) {
		if (destination.startsWith("custom:"))
		{
			bool valid = false;
			const auto index = destination.mid(7).toInt(&valid);
			if (valid && index >= 0 && index < customPath->size())
			{
				customPath->resize(index + 1);
				showCustom(showCustom, true);
			}
		}
		else if (destination == "root")
		{
			showRoot(showRoot, true);
		}
		else if (destination == "song")
		{
			showTracks(showTracks, song, true);
		}
		else if (destination == "pattern")
		{
			showTracks(showTracks, Engine::patternStore(), true);
		}
	});
	connect(fit, &QToolButton::clicked, canvas, &NodeWorkflowCanvas::fitGraph);
	const auto addSource = [song, showTracks, activeTracks](const QString& type, const QString& value,
		std::optional<QPointF> position, const QDomElement& preset = {}) {
		if (*activeTracks != song)
		{
			return false;
		}
		auto* container = static_cast<TrackContainer*>(song);
		Track* track = nullptr;
		if (type == "presetfile" && !preset.isNull())
		{
			auto* instrument = static_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, container));
			instrument->loadPreset(preset);
			if (!vsthost::nativeVstPresetRestored(instrument->instrument()))
			{
				delete instrument;
				return false;
			}
			track = instrument;
		}
		else if (type == "svsvoice")
		{
			const auto split = value.lastIndexOf('/');
			if (split <= 0 || split + 1 == value.size())
			{
				return false;
			}
			auto* voice = static_cast<SVSTrack*>(Track::create(Track::Type::SVS, container));
			voice->bindVoice(value.left(split), value.mid(split + 1));
			track = voice;
		}
		else if (type == "svcselection")
		{
			const auto selection = QJsonDocument::fromJson(value.toUtf8()).object();
			if (selection.value("engine_id").toString().isEmpty()
				|| selection.value("model_id").toString().isEmpty())
			{
				return false;
			}
			auto* voice = static_cast<SVCTrack*>(Track::create(Track::Type::SVC, container));
			if (!voice->setSelection(selection))
			{
				delete voice;
				return false;
			}
			track = voice;
		}
		else if ((type == "instrument" || type == "vstpluginfile") && !value.isEmpty())
		{
			auto* instrument = static_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, container));
			auto* plugin = instrument->loadInstrument(type == "vstpluginfile" ? QStringLiteral("vestige") : value,
				nullptr, position.has_value());
			if (!plugin)
			{
				delete instrument;
				return false;
			}
			if (type == "vstpluginfile")
			{
				plugin->loadFile(value);
			}
			track = instrument;
		}
		if (!track)
		{
			return false;
		}
		if (position)
		{
			song->setNodeTrackPosition(*track, *position);
		}
		song->setNodeSourceEventConnection(*track, false);
		song->setNodeTrackAudioRoute(*track, 0, std::nullopt, 0);
		song->setNodeTrackAudioRoute(*track, 1, std::nullopt, 1);
		showTracks(showTracks, container, false);
		return true;
	};
	const auto addEffectNode = [song, showTracks, showSource, activeTracks, activeSource,
		confirmSourceGraphExtension, customPath, activeGraphScope, showCustom](
		const QString& payload, std::optional<QPointF> point, const vsthost::NativeVstPresetDrop* preset = nullptr) {
		QDomDocument document;
		if (!document.setContent(payload))
		{
			return false;
		}
		const auto element = document.documentElement();
		const auto descriptorName = element.attribute("plugin");
		const auto effects = getPluginFactory()->descriptors(Plugin::Type::Effect);
		const auto descriptor = std::find_if(effects.cbegin(), effects.cend(), [&](const auto* candidate) {
			return QString::fromUtf8(candidate->name) == descriptorName;
		});
		if (element.tagName() != "key" || descriptor == effects.cend())
		{
			return false;
		}
		Plugin::Descriptor::SubPluginFeatures::Key key(element);
		key.desc = *descriptor;
		if (const auto track = *activeSource; track && customPath->isEmpty())
		{
			if (!confirmSourceGraphExtension() || !track)
			{
				return false;
			}
			auto* bus = nodeWorkflowAudioBus(*track);
			auto* chain = bus ? bus->effects() : nullptr;
			if (!chain)
			{
				return false;
			}
			auto* effect = preset ? vsthost::createNativeVstPresetEffect(*preset, chain).release()
				: Effect::instantiate(descriptorName, chain, &key);
			if (!effect || !effect->isOkay())
			{
				delete effect;
				return false;
			}
			const auto guard = Engine::audioEngine()->requestChangesGuard();
			effect->setProperty("nodeWorkflowSourceEffect", QUuid::createUuid().toString(QUuid::WithoutBraces));
			chain->appendEffect(effect);
			song->setModified();
			showSource(showSource, track.data(), !point.has_value());
			return true;
		}
		auto* mixer = Engine::mixer();
		const auto index = mixer->createChannel();
		auto* channel = mixer->mixerChannel(index);
		auto* effect = preset ? vsthost::createNativeVstPresetEffect(*preset, &channel->m_fxChain).release()
			: Effect::instantiate(descriptorName, &channel->m_fxChain, &key);
		if (!effect || !effect->isOkay())
		{
			delete effect;
			mixer->deleteChannel(index);
			return false;
		}
		channel->m_fxChain.appendEffect(effect);
		channel->m_name = effect->displayName();
		channel->m_nodeEffect = true;
		channel->m_nodeEffectPosition = point;
		if (!customPath->isEmpty())
		{
			const auto id = channel->nodeWorkflowId();
			song->setNodeCustomEffectScope(id, *activeGraphScope);
			mixer->deleteChannelSend(index, 0);
			if (point)
			{
				song->setNodeLayoutPosition(*activeGraphScope + QStringLiteral("effect/")
					+ id.toString(QUuid::WithoutBraces) + '/', *point);
			}
			showCustom(showCustom, !point.has_value());
			return true;
		}
		const bool pattern = *activeTracks == Engine::patternStore();
		channel->setNodePatternEffect(pattern);
		song->setNodeEffectInPattern(channel->nodeWorkflowId(), pattern);
		mixer->deleteChannelSend(index, 0);
		song->setModified();
		const auto* container = *activeTracks ? *activeTracks : static_cast<TrackContainer*>(song);
		showTracks(showTracks, container, !point.has_value());
		return true;
	};
	const auto addInstrumentLayer = [song, activeSource, showSource, confirmSourceGraphExtension]
		(const QString& plugin, std::optional<QPointF> point, const QString& file = {}, const QDomElement& preset = {}) {
		const auto owner = *activeSource;
		if (!owner || owner->type() != Track::Type::Instrument
			|| !confirmSourceGraphExtension() || !owner)
		{
			return false;
		}
		auto* layer = song->addNodeInstrumentLayer(*owner, plugin, point.has_value(), preset);
		if (!layer)
		{
			return false;
		}
		if (!file.isEmpty() && layer->instrument())
		{
			layer->instrument()->loadFile(file);
		}
		if (point)
		{
			const auto id = song->nodeInstrumentLayers(*owner).back().id;
			const auto scope = QStringLiteral("source/") + owner->nodeWorkflowId().toString(QUuid::WithoutBraces);
			song->setNodeLayoutPosition(scope + QStringLiteral("/layer/") + id, *point);
		}
		showSource(showSource, owner.data(), !point.has_value());
		return true;
	};
	const auto customInstrumentsAllowed = [activeSource] {
		const auto* source = activeSource->data();
		return !source || source->type() == Track::Type::Instrument;
	};
	const auto addCustomInstrument = [song, customPath, activeGraphScope, showCustom, customInstrumentsAllowed](
		const QString& plugin, const QString& file, std::optional<QPointF> point, const QDomElement& preset = {}) {
		if (customPath->isEmpty() || !customInstrumentsAllowed())
		{
			return false;
		}
		const auto scope = *activeGraphScope;
		const auto id = song->addNodeCustomInstrument(scope, plugin, file, point.has_value() && file.isEmpty(), preset);
		if (id.isEmpty())
		{
			return false;
		}
		if (point)
		{
			song->setNodeLayoutPosition(scope + QStringLiteral("instrument/") + id + '/', *point);
		}
		showCustom(showCustom, !point.has_value());
		return true;
	};
	canvas->setPluginDropHandler([song, activeSource, addSource, addEffectNode, addInstrumentLayer, addCustomInstrument, customPath]
		(const QString& type, const QString& value, const QPointF& point) {
		if (type == "presetfile")
		{
			DataFile file(value);
			const auto preset = vsthost::readNativeVstPresetDrop(file);
			if (!preset.hasAvailableIdentity())
			{
				song->collectError(tr("The VST preset or the selected plug-in could not be loaded."));
				return false;
			}
			if (preset.instrument)
			{
				if (!customPath->isEmpty())
				{
					return addCustomInstrument("native_vst_instrument", {}, point, file.content());
				}
				if (*activeSource)
				{
					return addInstrumentLayer("native_vst_instrument", point, {}, file.content());
				}
				return addSource(type, value, point, file.content());
			}
			QDomDocument keyDocument;
			auto key = preset.key.saveXML(keyDocument);
			key.setAttribute("plugin", "native_vst_effect");
			keyDocument.appendChild(key);
			return addEffectNode(keyDocument.toString(), point, &preset);
		}
		if (!customPath->isEmpty() && type != "nodeeffect")
		{
			if (type == "vstpluginfile")
			{
				return addCustomInstrument(QStringLiteral("vestige"), value, point);
			}
			return type == "instrument" && addCustomInstrument(value, {}, point);
		}
		if (*activeSource && type != "nodeeffect")
		{
			if (type == "vstpluginfile")
			{
				return addInstrumentLayer(QStringLiteral("vestige"), point, value);
			}
			return type == "instrument" && addInstrumentLayer(value, point);
		}
		return type == "nodeeffect" ? addEffectNode(value, point) : addSource(type, value, point);
	});
	connect(add, &QToolButton::clicked, canvas,
		[this, add, canvas, song, showRoot, showTracks, showSource, activeTracks, activeSource,
			addSource, addEffectNode, addInstrumentLayer, boundaryLabel, confirmSourceGraphExtension,
			activeGraphScope, customPath, showCustom, addCustomInstrument, customInstrumentsAllowed] {
		QMenu menu(this);
		menu.setObjectName("nodeWorkflowAddMenu");
		const auto redraw = [song, showSource, showTracks, showRoot, showCustom, customPath, activeSource, activeTracks] {
			if (!customPath->isEmpty()) { showCustom(showCustom, true); }
			else if (*activeSource) { showSource(showSource, activeSource->data(), true); }
			else if (*activeTracks) { showTracks(showTracks, *activeTracks, true); }
			else { showRoot(showRoot, true); }
		};
		if (!*activeSource || (*activeSource)->type() != Track::Type::SVS)
		{
			menu.addAction(tr("Add Custom Subgraph"), canvas,
				[this, song, activeGraphScope, confirmSourceGraphExtension, redraw] {
					if (!confirmSourceGraphExtension())
					{
						return;
					}
					bool accepted = false;
					const auto name = QInputDialog::getText(this, tr("Custom Subgraph"), tr("Name"),
						QLineEdit::Normal, tr("Custom Subgraph"), &accepted);
					if (accepted && !song->addNodeCustomGraph(*activeGraphScope, name).isEmpty())
					{
						redraw();
					}
				});
		}
		if (*activeTracks || *activeSource || !customPath->isEmpty())
		{
			for (const bool output : {false, true})
			{
				auto* direction = menu.addMenu(output ? tr("Output Port") : tr("Input Port"));
				for (const auto* kind : {"audio", "stereo", "midi"})
				{
					const bool stereo = qstrcmp(kind, "stereo") == 0;
					const QString signal = stereo ? QStringLiteral("audio") : QString::fromLatin1(kind);
					const auto label = stereo ? tr("Stereo Audio") : boundaryLabel(signal);
					direction->addAction(label, canvas, [song, activeTracks, activeSource,
						showSource, showTracks, signal, output, stereo, confirmSourceGraphExtension, customPath, activeGraphScope, redraw] {
						if (!customPath->isEmpty())
						{
							song->createNodeBoundaryPort(*activeGraphScope, signal, output, stereo);
							redraw();
						}
						else if (*activeSource)
						{
							if (!confirmSourceGraphExtension() || !*activeSource)
							{
								return;
							}
							const auto graph = QStringLiteral("source/")
								+ (*activeSource)->nodeWorkflowId().toString(QUuid::WithoutBraces) + '/';
							song->createNodeBoundaryPort(graph, signal, output, stereo);
							showSource(showSource, activeSource->data(), true);
						}
						else if (*activeTracks)
						{
							song->createNodeBoundaryPort(*activeTracks == song ? QStringLiteral("song/")
								: QStringLiteral("pattern/"), signal, output, stereo);
							showTracks(showTracks, *activeTracks, true);
						}
					});
				}
			}
			menu.addSeparator();
		}
		if (!customPath->isEmpty())
		{
			if (customInstrumentsAllowed())
			{
				auto* instruments = menu.addMenu(tr("Instrument Plugins"));
				for (const auto* plugin : getPluginFactory()->descriptors(Plugin::Type::Instrument))
				{
					instruments->addAction(QString::fromUtf8(plugin->displayName), canvas,
						[addCustomInstrument, name = QString::fromUtf8(plugin->name)] {
							addCustomInstrument(name, {}, std::nullopt);
						});
				}
				instruments->addAction(tr("Open VST Instrument…"), canvas, [this, addCustomInstrument] {
					const auto path = QFileDialog::getOpenFileName(this, tr("Open VST Instrument"), {},
						tr("VST files (*.dll *.vst3)"));
					if (!path.isEmpty())
					{
						addCustomInstrument(QStringLiteral("vestige"), path, std::nullopt);
					}
				});
			}
			menu.addAction(tr("Effect Plugin…"), canvas, [canvas, addEffectNode] {
				auto* selector = new EffectSelectDialog(canvas);
				selector->setAttribute(Qt::WA_DeleteOnClose);
				selector->enableNodeDrag();
				QObject::connect(selector, &QDialog::accepted, canvas, [selector, addEffectNode] {
					addEffectNode(selector->nodeEffectKey(), std::nullopt);
				});
			});
			menu.exec(add->mapToGlobal(QPoint(0, add->height())));
			return;
		}
		if (*activeSource)
		{
			if ((*activeSource)->type() == Track::Type::Instrument)
			{
				auto* instruments = menu.addMenu(tr("Instrument Plugins"));
				auto descriptors = getPluginFactory()->descriptors(Plugin::Type::Instrument);
				std::sort(descriptors.begin(), descriptors.end(), [](const auto* left, const auto* right) {
					return QString::fromUtf8(left->displayName).localeAwareCompare(
						QString::fromUtf8(right->displayName)) < 0;
				});
				for (const auto* plugin : descriptors)
				{
					instruments->addAction(QString::fromUtf8(plugin->displayName), canvas,
						[addInstrumentLayer, name = QString::fromUtf8(plugin->name)] {
							addInstrumentLayer(name, std::nullopt);
						});
				}
				instruments->addAction(tr("Open VST Instrument…"), canvas, [this, addInstrumentLayer] {
					const auto path = QFileDialog::getOpenFileName(this, tr("Open VST Instrument"), {},
						tr("VST files (*.dll *.vst3)"));
					if (!path.isEmpty())
					{
						addInstrumentLayer(QStringLiteral("vestige"), std::nullopt, path);
					}
				});
			}
			auto* effectAction = menu.addAction(tr("Effect Plugin…"));
			if (menu.exec(add->mapToGlobal(QPoint(0, add->height()))) == effectAction)
			{
				auto* selector = new EffectSelectDialog(canvas);
				selector->setAttribute(Qt::WA_DeleteOnClose);
				selector->enableNodeDrag();
				connect(selector, &QDialog::accepted, canvas, [selector, addEffectNode] {
					addEffectNode(selector->nodeEffectKey(), std::nullopt);
				});
			}
			return;
		}
		menu.addAction(tr("Effect Plugin…"))->setData(QStringList{"effect", QString{}});
		if (*activeTracks == song)
		{
			auto* instruments = menu.addMenu(tr("Instrument Plugins"));
			auto descriptors = getPluginFactory()->descriptors(Plugin::Type::Instrument);
			std::sort(descriptors.begin(), descriptors.end(), [](const auto* left, const auto* right) {
				return QString::fromUtf8(left->displayName).localeAwareCompare(QString::fromUtf8(right->displayName)) < 0;
			});
			for (const auto* plugin : descriptors)
			{
				instruments->addAction(QString::fromUtf8(plugin->displayName))
					->setData(QStringList{"instrument", QString::fromUtf8(plugin->name)});
			}
			instruments->addAction(tr("Open VST Instrument…"), canvas, [this, addSource] {
				const auto path = QFileDialog::getOpenFileName(this, tr("Open VST Instrument"), {},
					tr("VST files (*.dll *.vst3)"));
				if (!path.isEmpty())
				{
					addSource(QStringLiteral("vstpluginfile"), path, std::nullopt);
				}
			});
			auto* voices = menu.addMenu(tr("SVS Voices"));
			for (const auto& voice : svs::Registry::instance().voices())
			{
				voices->addAction(voice.name)->setData(QStringList{"svsvoice", voice.pluginId + '/' + voice.id});
			}
			voices->setEnabled(!voices->isEmpty());
			auto* svcModels = menu.addMenu(tr("SVC Models"));
			for (const auto& engine : svc::Catalog::instance().engines())
			{
				if (!engine.api)
				{
					continue;
				}
				for (const auto& value : engine.capabilities.value("models").toArray())
				{
					const auto model = value.toObject();
					const auto id = model.value("id").toString();
					if (id.isEmpty() || !model.value("available").toBool(true))
					{
						continue;
					}
					const auto title = model.value("name").toString(id);
					const auto weights = model.value("weights").toArray();
					const auto speakers = model.value("speakers").toArray();
					const bool multipleSpeakers = model.value("multi_speaker").toBool(speakers.size() > 1);
					const auto addModel = [&](const QString& label, const QJsonObject& speaker) {
						QJsonObject selection{{"engine_id", engine.id}, {"model_id", id}};
						if (!weights.isEmpty() && (weights.size() == 1 || !model.contains("parameters")))
						{
							selection.insert("weight_id", weights.first().toObject().value("id"));
						}
						if (!speaker.isEmpty())
						{
							selection.insert("speaker_id", speaker.value("id"));
						}
						const auto payload = QString::fromUtf8(QJsonDocument(selection).toJson(QJsonDocument::Compact));
						svcModels->addAction(label)->setData(QStringList{"svcselection", payload});
					};
					if (multipleSpeakers)
					{
						for (const auto& entry : speakers)
						{
							const auto speaker = entry.toObject();
							if (speaker.value("available").toBool(true))
							{
								addModel(title + QStringLiteral(" / ") + speaker.value("name").toString(
									speaker.value("id").toString()), speaker);
							}
						}
					}
					else
					{
						addModel(title, speakers.isEmpty() ? QJsonObject{} : speakers.first().toObject());
					}
				}
			}
			svcModels->setEnabled(!svcModels->isEmpty());
		}
		if (auto* action = menu.exec(add->mapToGlobal(QPoint(0, add->height()))))
		{
			const auto selection = action->data().toStringList();
			if (selection.size() == 2 && selection.at(0) == "effect")
			{
				auto* selector = new EffectSelectDialog(canvas);
				selector->setAttribute(Qt::WA_DeleteOnClose);
				selector->enableNodeDrag();
				connect(selector, &QDialog::accepted, canvas, [selector, addEffectNode] {
					addEffectNode(selector->nodeEffectKey(), std::nullopt);
				});
			}
			else if (selection.size() == 2)
			{
				addSource(selection.at(0), selection.at(1), std::nullopt);
			}
		}
	});
	const auto refreshRoutes = [showRoot, showTracks, showSource, showCustom, activeTracks, activeSource, customPath] {
		if (!customPath->isEmpty())
		{
			showCustom(showCustom, false);
		}
		else if (*activeSource)
		{
			showSource(showSource, activeSource->data(), false);
		}
		else if (*activeTracks)
		{
			showTracks(showTracks, *activeTracks, false);
		}
		else
		{
			showRoot(showRoot, false);
		}
	};
	connect(song, &Song::nodeTrackRoutingChanged, canvas, refreshRoutes, Qt::QueuedConnection);
	connect(Engine::mixer(), &Mixer::channelTopologyChanged, canvas, refreshRoutes, Qt::QueuedConnection);
	connect(Engine::patternStore(), &PatternStore::currentPatternChanged, canvas, [activeSource, showSource, customPath] {
		if (customPath->isEmpty() && *activeSource && (*activeSource)->trackContainer() == Engine::patternStore())
		{
			showSource(showSource, activeSource->data(), false);
		}
	}, Qt::QueuedConnection);
	for (auto* container : {static_cast<TrackContainer*>(song), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		const auto refresh = [container, refreshRoutes, activeTracks] {
			if (!*activeTracks || *activeTracks == container)
			{
				refreshRoutes();
			}
		};
		const auto watchTrack = [canvas, refresh, activeSource, showSource, customPath](Track* track) {
			const auto refreshTrack = [refresh, activeSource, showSource, customPath, track = QPointer<Track>(track)] {
				if (customPath->isEmpty() && track && activeSource->data() == track.data())
				{
					showSource(showSource, track.data(), false);
				}
				else
				{
					refresh();
				}
			};
			connect(track, &Track::nameChanged, canvas, refreshTrack, Qt::QueuedConnection);
			connect(track, &Track::colorChanged, canvas, refreshTrack, Qt::QueuedConnection);
		};
		for (auto* track : container->tracks())
		{
			watchTrack(track);
		}
		connect(container, &TrackContainer::trackAdded, canvas, watchTrack);
		connect(container, &TrackContainer::trackAdded, canvas, refresh, Qt::QueuedConnection);
		connect(container, &TrackContainer::trackRemoved, canvas, [canvas, showRoot, activeSource, refresh] {
			if (!canvas->acceptDrops() && activeSource->isNull())
			{
				showRoot(showRoot, true);
			}
			else
			{
				refresh();
			}
		}, Qt::QueuedConnection);
	}
	auto* window = addWindowedWidget(content);
	window->setObjectName("nodeWorkflowEditor");
	window->installEventFilter(this);
	window->setWindowTitle(tr("Node Editor"));
	const auto workspaceSize = m_workspace->viewport()->size();
	window->resize(QSize(1280, 780).boundedTo(workspaceSize));
	connect(song, &Song::projectAboutToReset, window, [window] {
		window->setObjectName({});
		window->hide();
		window->deleteLater();
	});
	connect(triggerLcd, &QObject::destroyed, [triggerTrack, triggerAll] {
		if ((*triggerAll && NodePatternTriggers::watchingAll())
			|| (*triggerTrack && !NodePatternTriggers::watchingAll() && NodePatternTriggers::watches(triggerTrack->data())))
		{
			NodePatternTriggers::watch(nullptr);
		}
	});
	window->show();
	window->move(
		std::clamp(window->x(), 0, std::max(0, workspaceSize.width() - window->width())),
		std::clamp(window->y(), 0, std::max(0, workspaceSize.height() - window->height())));
	m_workspace->setActiveSubWindow(window);
	showRoot(showRoot, true);
}

SubWindow* MainWindow::addWindowedWidget(QWidget *w, Qt::WindowFlags windowFlags)
{
	// wrap the widget in our own *custom* window that patches some errors in QMdiSubWindow
	auto win = new SubWindow(m_workspace->viewport(), windowFlags);
	connect(this, &MainWindow::detachAllSubWindows, win, &SubWindow::setDetached);
	win->setWidget(w);

	if (w)
	{
		// TODO: somehow make this work on any setWidget
		connect(w, &QWidget::destroyed, win, &SubWindow::deleteLater);

		if (w->sizeHint().isValid())
		{
			auto titleBarHeight = win->titleBarHeight();
			auto frameWidth = win->frameWidth();
			QSize delta(2* frameWidth, titleBarHeight + frameWidth);
			win->resize(delta + w->sizeHint());
		}
	}

	m_workspace->addSubWindow(win);
	return win;
}



void MainWindow::setAllSubWindowsDetached(bool detached)
{
	emit detachAllSubWindows(detached);
}


void MainWindow::resetWindowTitle()
{
	QString title(tr( "Untitled" ));

	if( Engine::getSong()->projectFileName() != "" )
	{
		title = QFileInfo( Engine::getSong()->projectFileName()
							).completeBaseName();
	}

	if( Engine::getSong()->isModified() )
	{
		title += '*';
	}

	if( getSession() == SessionState::Recover )
	{
		title += " - " + tr( "Recover session. Please save your work!" );
	}

	setWindowTitle( title + " - " + tr( "LMMS %1" ).arg( LMMS_VERSION ) );
}




bool MainWindow::mayChangeProject(bool stopPlayback)
{
	if( stopPlayback )
	{
		Engine::getSong()->stop();
	}

	if( !Engine::getSong()->isModified() && getSession() != SessionState::Recover )
	{
		return( true );
	}

	// Separate message strings for modified and recovered files
	QString messageTitleRecovered = tr( "Recovered project not saved" );
	QString messageRecovered = tr( "This project was recovered from the "
					"previous session. It is currently "
					"unsaved and will be lost if you don't "
					"save it. Do you want to save it now?" );

	QString messageTitleUnsaved = tr( "Project not saved" );
	QString messageUnsaved = tr( "The current project was modified since "
					"last saving. Do you want to save it "
								"now?" );

	const auto& title = getSession() == SessionState::Recover
		? messageTitleRecovered
		: messageTitleUnsaved;

	const auto& text = getSession() == SessionState::Recover
		? messageRecovered
		: messageUnsaved;

	const auto buttons = QMessageBox::Save
		| QMessageBox::Discard
		| QMessageBox::Cancel;

	auto mb = QMessageBox{QMessageBox::Question, title, text, buttons, this};
	int answer = mb.exec();

	if( answer == QMessageBox::Save )
	{
		return( saveProject() );
	}
	else if( answer == QMessageBox::Discard )
	{
		if( getSession() == SessionState::Recover )
		{
			sessionCleanup();
		}
		return( true );
	}

	return( false );
}




void MainWindow::clearKeyModifiers()
{
	m_keyMods.m_ctrl = false;
	m_keyMods.m_shift = false;
	m_keyMods.m_alt = false;
}




void MainWindow::saveWidgetState(QWidget* w, QDomElement& de)
{
	// TODO: Only use one of these
	auto win = qobject_cast<SubWindow*>(w);
	if (!win)
	{
		// Fall back on parent
		win = qobject_cast<SubWindow*>(w->parentWidget());
		if (!win)
		{
			// Still could not find the window - soft fail
			return;
		}
	}

	de.setAttribute("visible", bool{win->widget() && win->widget()->isVisible()});
	de.setAttribute("maximized", win->isMaximized());

	QRect normalGeometry = win->getTrueNormalGeometry();
	de.setAttribute("x", normalGeometry.x());
	de.setAttribute("y", normalGeometry.y() );
	de.setAttribute("width", normalGeometry.width());
	de.setAttribute("height", normalGeometry.height());
}

void MainWindow::restoreWidgetState(QWidget* w, const QDomElement& de)
{
	// TODO: Only use one of these
	auto win = qobject_cast<SubWindow*>(w);
	if (!win)
	{
		// Fall back on parent
		win = qobject_cast<SubWindow*>(w->parentWidget());

		// Still could not find the window - soft fail
		if (!win) { return; }
	}

	const auto normalGeometry = QRect{
		de.attribute("x").toInt(),
		de.attribute("y").toInt(),
		de.attribute("width").toInt(),
		de.attribute("height").toInt(),
	};

	// First restore the window, as attempting to resize a maximized window can cause graphical glitches.
	win->setWindowState(win->windowState() & ~(Qt::WindowMaximized | Qt::WindowMinimized));

	// Then resize it. `QWidget::setGeometry` adjusts the size if it is invalid / under the minimum size.
	win->setGeometry(normalGeometry);

	// Set the window to its correct "maximized?" state.
	auto winState = win->windowState();
	winState.setFlag(Qt::WindowMaximized, de.attribute("maximized").toInt());
	win->setWindowState(winState);

	if (const auto visible = de.attribute("visible"); !visible.isEmpty())
	{
		win->setVisible(visible.toInt());
	}
}

void MainWindow::emptySlot()
{
}



void MainWindow::createNewProject()
{
	if( mayChangeProject(true) )
	{
		Engine::getSong()->createNewProject();
	}
}




void MainWindow::openProject()
{
	if( mayChangeProject(false) )
	{
		FileDialog ofd( this, tr( "Open Project" ), "", tr( "LMMS (*.mmp *.mmpn *.mmpnz *.mmpz)" ) );

		ofd.setDirectory( ConfigManager::inst()->userProjectsDir() );
		ofd.setFileMode( FileDialog::ExistingFiles );
		if( ofd.exec () == QDialog::Accepted &&
						!ofd.selectedFiles().isEmpty() )
		{
			Song *song = Engine::getSong();

			song->stop();
			setCursor( Qt::WaitCursor );
			song->loadProject( ofd.selectedFiles()[0] );
			setCursor( Qt::ArrowCursor );
		}
	}
}




bool MainWindow::saveProject()
{
	const auto* song = Engine::getSong();
	const auto fileName = song->projectFileName();
	const auto extension = QFileInfo(fileName).suffix().toLower();
	if (fileName.isEmpty() || (song->isNodeProject() && extension != "mmpn" && extension != "mmpnz"))
	{
		return( saveProjectAs() );
	}
	else if( this->guiSaveProject() )
	{
		if( getSession() == SessionState::Recover )
		{
			sessionCleanup();
		}
		return true;
	}
	return false;
}




bool MainWindow::saveProjectAs()
{
	auto optionsWidget = new SaveOptionsWidget(Engine::getSong()->getSaveOptions());
	VersionedSaveDialog sfd( this, optionsWidget, tr( "Save Project" ), "",
			Engine::getSong()->isNodeProject() ? tr("LMMS Node Workflow Project (*.mmpnz *.mmpn)") :
			tr( "LMMS Project" ) + " (*.mmpz *.mmp);;" +
				tr( "LMMS Project Template" ) + " (*.mpt)" );
	QString f = Engine::getSong()->projectFileName();
	if( f != "" )
	{
		sfd.setDirectory( QFileInfo( f ).absolutePath() );
		sfd.selectFile( QFileInfo( f ).fileName() );
	}
	else
	{
		sfd.setDirectory( ConfigManager::inst()->userProjectsDir() );
	}

	// Don't write over file with suffix if no suffix is provided.
	QString suffix = ConfigManager::inst()->value( "app",
							"nommpz" ).toInt() == 0
						? "mmpz"
						: "mmp" ;
	if (Engine::getSong()->isNodeProject())
	{
		suffix = suffix == "mmpz" ? "mmpnz" : "mmpn";
	}
	sfd.setDefaultSuffix( suffix );

	if( sfd.exec () == FileDialog::Accepted &&
		!sfd.selectedFiles().isEmpty() && sfd.selectedFiles()[0] != "" )
	{
		QString fname = sfd.selectedFiles()[0] ;
		if( sfd.selectedNameFilter().contains( "(*.mpt)" ) )
		{
			// Remove the default suffix
			fname.remove( "." + suffix );
			if( !sfd.selectedFiles()[0].endsWith( ".mpt" ) )
			{
				if( VersionedSaveDialog::fileExistsQuery( fname + ".mpt",
						tr( "Save project template" ) ) )
				{
					fname += ".mpt";
				}
			}
		}
		if( this->guiSaveProjectAs( fname ) )
		{
			if( getSession() == SessionState::Recover )
			{
				sessionCleanup();
			}
			return true;
		}
	}
	return false;
}




bool MainWindow::saveProjectAsNewVersion()
{
	QString fileName = Engine::getSong()->projectFileName();
	if( fileName == "" )
	{
		return saveProjectAs();
	}
	else
	{
		do 		VersionedSaveDialog::changeFileNameVersion( fileName, true );
		while 	( QFile( fileName ).exists() );

		return this->guiSaveProjectAs( fileName );
	}
}




void MainWindow::saveProjectAsDefaultTemplate()
{
	QString defaultTemplate = ConfigManager::inst()->userTemplateDir() + "default.mpt";

	QFileInfo fileInfo(defaultTemplate);
	if (fileInfo.exists())
	{
		if (QMessageBox::warning(this,
					 tr("Overwrite default template?"),
					 tr("This will overwrite your current default template."),
					 QMessageBox::Ok,
					 QMessageBox::Cancel) != QMessageBox::Ok)
		{
			return;
		}
	}

	Engine::getSong()->saveProjectFile( defaultTemplate );
}




void MainWindow::showSettingsDialog()
{
	SetupDialog sd;
	sd.exec();
}




void MainWindow::aboutLMMS()
{
	AboutDialog(this).exec();
}




void MainWindow::help()
{
	QMessageBox::information( this, tr( "Help not available" ),
				  tr( "Currently there's no help "
						  "available in LMMS.\n"
						  "Please visit "
						  "http://lmms.sf.net/wiki "
						  "for documentation on LMMS." ),
				  QMessageBox::Ok );
}




void MainWindow::toggleWindow( QWidget *window, bool forceShow )
{
	// All "windows" should be inside a SubWindow, because the use of activeSubWindow() depends on it
	auto parent = dynamic_cast<QMdiSubWindow*>(window->parentWidget());
	if (parent == nullptr) { return; }

	if( forceShow ||
		m_workspace->activeSubWindow() != parent ||
		parent->isHidden() )
	{
		parent->show();
		window->show();
		if (window->isEnabled()) { window->setFocus(); }
		else { m_workspace->setActiveSubWindow(parent); }
	}
	else
	{
		parent->hide();
		refocus();
	}
}



void MainWindow::toggleFullscreen()
{
	if ( !isFullScreen() )
	{
		maximized = isMaximized();
		showFullScreen();
	}
	else
	{
		maximized ? showMaximized() : showNormal();
	}
}



/*
 * When an editor window with focus is toggled off, attempt to set focus
 * to the next visible editor window, or if none are visible, set focus
 * to the parent window.
 */
void MainWindow::refocus()
{
	const auto gui = getGUI();

	// Attempt to set the focus on the first of these editors that is not hidden...
	for (auto editorParent : { gui->songEditor()->parentWidget(), gui->patternEditor()->parentWidget(),
		gui->pianoRoll()->parentWidget(), gui->automationEditor()->parentWidget() })
	{
		if (!editorParent->isHidden())
		{
			editorParent->setFocus();
			return;
		}
	}

	// ... otherwise set the focus on the main window.
	this->setFocus();
}




void MainWindow::togglePatternEditorWin( bool forceShow )
{
	toggleWindow( getGUI()->patternEditor(), forceShow );
}




void MainWindow::toggleSongEditorWin()
{
	toggleWindow( getGUI()->songEditor() );
}




void MainWindow::toggleProjectNotesWin()
{
	toggleWindow( getGUI()->getProjectNotes() );
}




void MainWindow::togglePianoRollWin()
{
	toggleWindow( getGUI()->pianoRoll() );
}




void MainWindow::toggleAutomationEditorWin()
{
	toggleWindow( getGUI()->automationEditor() );
}




void MainWindow::toggleMixerWin()
{
	toggleWindow( getGUI()->mixerView() );
}



void MainWindow::toggleMicrotunerWin()
{
	toggleWindow( getGUI()->getMicrotunerConfig() );
}




void MainWindow::updateNodeEditorAction()
{
	const auto* editor = m_workspace->findChild<SubWindow*>("nodeWorkflowEditor");
	const auto label = editor && editor->isVisible() ? tr("Hide Node Editor") : tr("Show Node Editor");
	m_nodeEditorAction->setText(label);
	m_nodeEditorAction->setToolTip(label);
	m_nodeEditorAction->setEnabled(Engine::getSong()->isNodeProject());
}

void MainWindow::updateViewMenu()
{
	m_viewMenu->clear();
	// TODO: get current visibility for these and indicate in menu?
	// Not that it's straight visible <-> invisible, more like
	// not on top -> top <-> invisible
	m_viewMenu->addAction(embed::getIconPixmap( "songeditor" ),
			      tr( "Song Editor" ) + "\tCtrl+1",
			      this, SLOT(toggleSongEditorWin())
		);
	m_viewMenu->addAction(embed::getIconPixmap("pattern_track"),
					tr("Pattern Editor") + "\tCtrl+2",
					this, SLOT(togglePatternEditorWin())
		);
	m_viewMenu->addAction(embed::getIconPixmap( "piano" ),
			      tr( "Piano Roll" ) + "\tCtrl+3",
			      this, SLOT(togglePianoRollWin())
		);
	m_viewMenu->addAction(embed::getIconPixmap( "automation" ),
			      tr( "Automation Editor" ) + "\tCtrl+4",
			      this,
			      SLOT(toggleAutomationEditorWin())
		);
	m_viewMenu->addAction(embed::getIconPixmap( "mixer" ),
			      tr( "Mixer" ) + "\tCtrl+5",
			      this, SLOT(toggleMixerWin())
		);
	m_viewMenu->addAction(embed::getIconPixmap( "controller" ),
			      tr( "Controller Rack" ) + "\tCtrl+6",
			      this, SLOT(toggleControllerRack())
		);
	m_viewMenu->addAction(embed::getIconPixmap( "project_notes" ),
			      tr( "Project Notes" ) + "\tCtrl+7",
			      this, SLOT(toggleProjectNotesWin())
		);
	updateNodeEditorAction();
	m_viewMenu->addAction(m_nodeEditorAction);

	m_viewMenu->addSeparator();
	
	m_viewMenu->addAction(embed::getIconPixmap( "fullscreen" ),
				tr( "Fullscreen" ) + "\tF11",
				this, SLOT(toggleFullscreen())
		);

	m_viewMenu->addSeparator();

	auto detachAllAction = m_viewMenu->addAction(embed::getIconPixmap("detach"),
		tr("Detach all subwindows"),
		this, [this](){ setAllSubWindowsDetached(true); },
		QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_D}
	);
	auto attachAllAction = m_viewMenu->addAction(embed::getIconPixmap("detach"),
		tr("Attach all subwindows"),
		this, [this](){ setAllSubWindowsDetached(false); },
		QKeySequence{Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_D}
	);

	detachAllAction->setShortcutContext(Qt::ApplicationShortcut);
	attachAllAction->setShortcutContext(Qt::ApplicationShortcut);

	m_viewMenu->addSeparator();

	// Here we should put all look&feel -stuff from configmanager
	// that is safe to change on the fly. There is probably some
	// more elegant way to do this.
	auto qa = new QAction(tr("Smooth scroll"), this);
	qa->setData("smoothscroll");
	qa->setCheckable( true );
	qa->setChecked( ConfigManager::inst()->value( "ui", "smoothscroll" ).toInt() );
	m_viewMenu->addAction(qa);

	// Not yet.
	/* qa = new QAction(tr( "One instrument track window" ), this);
	qa->setData("oneinstrument");
	qa->setCheckable( true );
	qa->setChecked( ConfigManager::inst()->value( "ui", "oneinstrumenttrackwindow" ).toInt() );
	m_viewMenu->addAction(qa);
	*/

	qa = new QAction(tr( "Enable note labels in piano roll" ), this);
	qa->setData("printnotelabels");
	qa->setCheckable( true );
	qa->setChecked( ConfigManager::inst()->value( "ui", "printnotelabels" ).toInt() );
	m_viewMenu->addAction(qa);

}




void MainWindow::updateConfig( QAction * _who )
{
	QString tag = _who->data().toString();
	bool checked = _who->isChecked();

	if (tag == "tooltips")
	{
		ConfigManager::inst()->setValue( "tooltips", "disabled",
						 QString::number(!checked) );

		if (checked) { qApp->removeEventFilter(this); }
		else { qApp->installEventFilter(this); }

	}
	else if ( tag == "smoothscroll" )
	{
		ConfigManager::inst()->setValue( "ui", "smoothscroll",
						 QString::number(checked) );
	}
	else if ( tag == "oneinstrument" )
	{
		ConfigManager::inst()->setValue( "ui", "oneinstrumenttrackwindow",
						 QString::number(checked) );
	}
	else if ( tag == "printnotelabels" )
	{
		ConfigManager::inst()->setValue( "ui", "printnotelabels",
						 QString::number(checked) );
	}
}



void MainWindow::onToggleMetronome()
{
	Engine::getSong()->metronome().setActive(m_metronomeToggle->isChecked());
}




void MainWindow::toggleControllerRack()
{
	toggleWindow( getGUI()->getControllerRackView() );
}




void MainWindow::updatePlayPauseIcons()
{
	getGUI()->songEditor()->setPauseIcon( false );
	getGUI()->automationEditor()->setPauseIcon( false );
	getGUI()->patternEditor()->setPauseIcon( false );
	getGUI()->pianoRoll()->setPauseIcon( false );

	if( Engine::getSong()->isPlaying() )
	{
		switch( Engine::getSong()->playMode() )
		{
			case Song::PlayMode::Song:
				getGUI()->songEditor()->setPauseIcon( true );
				break;

			case Song::PlayMode::AutomationClip:
				getGUI()->automationEditor()->setPauseIcon( true );
				break;

			case Song::PlayMode::Pattern:
				getGUI()->patternEditor()->setPauseIcon( true );
				break;

			case Song::PlayMode::MidiClip:
				getGUI()->pianoRoll()->setPauseIcon( true );
				break;

			default:
				break;
		}
	}
}


void MainWindow::updateUndoRedoButtons()
{
	// when the edit menu is shown, grey out the undo/redo buttons if there's nothing to undo/redo
	// else, un-grey them
	m_undoAction->setEnabled(Engine::projectJournal()->canUndo());
	m_redoAction->setEnabled(Engine::projectJournal()->canRedo());
}



void MainWindow::undo()
{
	Engine::projectJournal()->undo();
}




void MainWindow::redo()
{
	Engine::projectJournal()->redo();
}




void MainWindow::closeEvent( QCloseEvent * _ce )
{
	if( mayChangeProject(true) )
	{
		// delete recovery file
		if( ConfigManager::inst()->
				value( "ui", "enableautosave" ).toInt() )
		{
			sessionCleanup();
		}
		_ce->accept();
		QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
	}
	else
	{
		_ce->ignore();
	}
}




void MainWindow::sessionCleanup()
{
	// delete recover session files
	QFile::remove( ConfigManager::inst()->recoveryFile() );
	setSession( SessionState::Normal );
}




bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched->objectName() == "nodeWorkflowEditor"
		&& (event->type() == QEvent::Show || event->type() == QEvent::Hide))
	{
		QTimer::singleShot(0, this, &MainWindow::updateNodeEditorAction);
	}
	const auto isWinDeactivate = [](QEvent* event) -> bool
	{
		if (event->type() == QEvent::WindowDeactivate) { return true; }

		if (event->type() == QEvent::FocusOut)
		{
			auto* fe = static_cast<QFocusEvent*>(event);
			switch (fe->reason())
			{
			case Qt::ActiveWindowFocusReason: [[fallthrough]];
			case Qt::PopupFocusReason: [[fallthrough]];
			case Qt::OtherFocusReason:
				return true;
			default:
				break;
			}
		}

		return false;
	};

	// Clear modifiers when the window has been unfocused (install event filter on the window itself)
	if (dynamic_cast<MainWindow*>(watched) != nullptr && isWinDeactivate(event))
	{
		clearKeyModifiers();
		return QObject::eventFilter(watched, event);
	}

	// Block tooltips globally (install event filter on QApplication instance)
	if (event->type() == QEvent::ToolTip) { return true; }

	return QObject::eventFilter(watched, event);
}




void MainWindow::keyPressEvent( QKeyEvent * _ke )
{
	switch( _ke->key() )
	{
		case Qt::Key_Control: m_keyMods.m_ctrl = true; break;
		case Qt::Key_Shift: m_keyMods.m_shift = true; break;
		case Qt::Key_Alt: m_keyMods.m_alt = true; break;
		case Qt::Key_Space:
		{
			if (Editor::lastPlayedEditor() != nullptr)
			{
				if (m_keyMods.m_shift) { Editor::lastPlayedEditor()->togglePause(); }
				else { Editor::lastPlayedEditor()->togglePlayStop(); }
			}
			break;
		}
		default:
		{
			InstrumentTrackWindow * w =
						InstrumentTrackView::topLevelInstrumentTrackWindow();
			if( w )
			{
				w->pianoView()->keyPressEvent( _ke );
			}
			if( !_ke->isAccepted() )
			{
				QMainWindow::keyPressEvent( _ke );
			}
		}
	}
}




void MainWindow::keyReleaseEvent( QKeyEvent * _ke )
{
	switch( _ke->key() )
	{
		case Qt::Key_Control: m_keyMods.m_ctrl = false; break;
		case Qt::Key_Shift: m_keyMods.m_shift = false; break;
		case Qt::Key_Alt: m_keyMods.m_alt = false; break;
		default:
			if( InstrumentTrackView::topLevelInstrumentTrackWindow() )
			{
				InstrumentTrackView::topLevelInstrumentTrackWindow()->
					pianoView()->keyReleaseEvent( _ke );
			}
			if( !_ke->isAccepted() )
			{
				QMainWindow::keyReleaseEvent( _ke );
			}
	}
}




void MainWindow::timerEvent( QTimerEvent * _te)
{
	emit periodicUpdate();
}





void MainWindow::showTool( QAction * _idx )
{
	PluginView * p = m_tools[m_toolsMenu->actions().indexOf( _idx )];
	p->show();
	p->parentWidget()->show();
	p->setFocus();
}




void MainWindow::browseHelp()
{
	// file:// alternative for offline help
	QString url = "https://lmms.io/documentation/";
	QDesktopServices::openUrl( url );
	// TODO: Handle error
}




void MainWindow::autoSave()
{
	if( !Engine::getSong()->isExporting() &&
		!Engine::getSong()->isLoadingProject() &&
		!RemotePluginBase::isMainThreadWaiting() &&
		!QApplication::mouseButtons() &&
		( ConfigManager::inst()->value( "ui",
				"enablerunningautosave" ).toInt() ||
			! Engine::getSong()->isPlaying() ) )
	{
		Engine::getSong()->saveProjectFile(ConfigManager::inst()->recoveryFile());
		autoSaveTimerReset();  // Reset timer
	}
	else
	{
		// try again in 10 seconds
		if( getAutoSaveTimerInterval() != m_autoSaveShortTime )
		{
			autoSaveTimerReset( m_autoSaveShortTime );
		}
	}
}

void MainWindow::onExportProjectMidi()
{
	FileDialog efd( this );

	efd.setFileMode( FileDialog::AnyFile );

	QStringList types;
	types << tr("MIDI File (*.mid)");
	efd.setNameFilters( types );
	QString base_filename;
	QString const & projectFileName = Engine::getSong()->projectFileName();
	if( !projectFileName.isEmpty() )
	{
		efd.setDirectory( QFileInfo( projectFileName ).absolutePath() );
		base_filename = QFileInfo( projectFileName ).completeBaseName();
	}
	else
	{
		efd.setDirectory( ConfigManager::inst()->userProjectsDir() );
		base_filename = tr( "untitled" );
	}
	efd.selectFile( base_filename + ".mid" );
	efd.setDefaultSuffix( "mid");
	efd.setWindowTitle( tr( "Select file for project-export..." ) );

	efd.setAcceptMode( FileDialog::AcceptSave );


	if( efd.exec() == QDialog::Accepted && !efd.selectedFiles().isEmpty() && !efd.selectedFiles()[0].isEmpty() )
	{
		const QString suffix = ".mid";

		QString export_filename = efd.selectedFiles()[0];
		if (!export_filename.endsWith(suffix)) export_filename += suffix;

		Engine::getSong()->exportProjectMidi(PathUtil::stringToPath(export_filename));
	}
}

void MainWindow::exportProject(bool multiExport)
{
	QString const & projectFileName = Engine::getSong()->projectFileName();

	FileDialog efd( getGUI()->mainWindow() );

	if ( multiExport )
	{
		efd.setFileMode( FileDialog::Directory);
		efd.setWindowTitle( tr( "Select directory for writing exported tracks..." ) );
		if( !projectFileName.isEmpty() )
		{
			efd.setDirectory( QFileInfo( projectFileName ).absolutePath() );
		}
	}
	else
	{
		efd.setFileMode( FileDialog::AnyFile );
		int idx = 0;
		QStringList types;
		while( ProjectRenderer::fileEncodeDevices[idx].m_fileFormat != ProjectRenderer::ExportFileFormat::Count)
		{
			if(ProjectRenderer::fileEncodeDevices[idx].isAvailable()) {
				types << tr(ProjectRenderer::fileEncodeDevices[idx].m_description);
			}
			++idx;
		}
		efd.setNameFilters( types );
		QString baseFilename;
		if( !projectFileName.isEmpty() )
		{
			efd.setDirectory( QFileInfo( projectFileName ).absolutePath() );
			baseFilename = QFileInfo( projectFileName ).completeBaseName();
		}
		else
		{
			efd.setDirectory( ConfigManager::inst()->userProjectsDir() );
			baseFilename = tr( "untitled" );
		}
		efd.selectFile( baseFilename + ProjectRenderer::fileEncodeDevices[0].m_extension );
		efd.setWindowTitle( tr( "Select file for project-export..." ) );
	}

	QString suffix = "wav";
	efd.setDefaultSuffix( suffix );
	efd.setAcceptMode( FileDialog::AcceptSave );

	if( efd.exec() == QDialog::Accepted && !efd.selectedFiles().isEmpty() &&
					 !efd.selectedFiles()[0].isEmpty() )
	{

		QString exportFileName = efd.selectedFiles()[0];
		if ( !multiExport )
		{
			int stx = efd.selectedNameFilter().indexOf( "(*." );
			int etx = efd.selectedNameFilter().indexOf( ")" );

			if ( stx > 0 && etx > stx )
			{
				// Get first extension from selected dropdown.
				// i.e. ".wav" from "WAV-File (*.wav), Dummy-File (*.dum)"
				suffix = efd.selectedNameFilter().mid( stx + 2, etx - stx - 2 ).split( " " )[0].trimmed();

				Qt::CaseSensitivity cs = Qt::CaseSensitive;
#if defined(LMMS_BUILD_APPLE) || defined(LMMS_BUILD_WIN32)
				cs = Qt::CaseInsensitive;
#endif
				exportFileName.remove( "." + suffix, cs );
				if ( efd.selectedFiles()[0].endsWith( suffix ) )
				{
					if( VersionedSaveDialog::fileExistsQuery( exportFileName + suffix,
							tr( "Save project" ) ) )
					{
						exportFileName += suffix;
					}
				}
			}
		}

		ExportProjectDialog epd(exportFileName,
			multiExport ? ExportProjectDialog::Mode::ExportTracks : ExportProjectDialog::Mode::ExportProject,
			getGUI()->mainWindow());
		epd.exec();
	}
}

void MainWindow::handleSaveResult(QString const & filename, bool songSavedSuccessfully)
{
	if (songSavedSuccessfully)
	{
		TextFloat::displayMessage( tr( "Project saved" ), tr( "The project %1 is now saved.").arg( filename ),
				embed::getIconPixmap( "project_save", 24, 24 ), 2000 );
		ConfigManager::inst()->addRecentlyOpenedProject(filename);
		resetWindowTitle();
	}
	else
	{
		TextFloat::displayMessage( tr( "Project NOT saved." ), tr( "The project %1 was not saved!" ).arg(filename),
				embed::getIconPixmap( "error" ), 4000 );
	}
}

bool MainWindow::guiSaveProject()
{
	Song * song = Engine::getSong();
	bool const songSaveResult = song->guiSaveProject();
	handleSaveResult(song->projectFileName(), songSaveResult);

	return songSaveResult;
}

bool MainWindow::guiSaveProjectAs( const QString & filename )
{
	Song * song = Engine::getSong();
	bool const songSaveResult = song->guiSaveProjectAs(filename);
	handleSaveResult(filename, songSaveResult);

	return songSaveResult;
}

void MainWindow::onExportProject()
{
	this->exportProject();
}

void MainWindow::onExportProjectTracks()
{
	this->exportProject(true);
}

void MainWindow::onImportProject()
{
	Song * song = Engine::getSong();

	if (song)
	{
		FileDialog ofd( nullptr, tr( "Import file" ),
				ConfigManager::inst()->userProjectsDir(),
				tr("MIDI sequences") +
				" (*.mid *.midi *.rmi);;" +
				tr("Hydrogen projects") +
				" (*.h2song);;" +
				tr("All file types") +
				" (*.*)");

		ofd.setFileMode( FileDialog::ExistingFiles );
		if( ofd.exec () == QDialog::Accepted && !ofd.selectedFiles().isEmpty() )
		{
			ImportFilter::import( ofd.selectedFiles()[0], song );
		}

		song->setLoadOnLaunch(false);
	}
}

void MainWindow::onSongModified()
{
	// Only update the window title if the code is executed from the GUI main thread.
	// The assumption seems to be that the Song can also be set as modified from other
	// threads. This is not a good design! Copied from the original implementation of
	// Song::setModified.
	if(QThread::currentThread() == this->thread())
	{
		this->resetWindowTitle();
	}
}

void MainWindow::onProjectFileNameChanged()
{
	this->resetWindowTitle();
}


MainWindow::MovableQMdiArea::MovableQMdiArea(QWidget* parent) :
	QMdiArea(parent),
	m_isBeingMoved(false),
	m_lastX(0),
	m_lastY(0)
{}

void MainWindow::MovableQMdiArea::mousePressEvent(QMouseEvent* event)
{
	const auto pos = position(event);
	m_lastX = pos.x();
	m_lastY = pos.y();
	m_isBeingMoved = true;
	setCursor(Qt::ClosedHandCursor);
}

void MainWindow::MovableQMdiArea::mouseMoveEvent(QMouseEvent* event)
{
	if (m_isBeingMoved == false) { return; }

	int minXBoundary = window()->width() - 100;
	int maxXBoundary = 100;
	int minYBoundary = window()->height() - 100;
	int maxYBoundary = 100;

	int minX = minXBoundary;
	int maxX = maxXBoundary;
	int minY = minYBoundary;
	int maxY = maxYBoundary;

	auto subWindows = subWindowList();
	for (auto* curWindow : subWindows)
	{
		if (curWindow->isVisible())
		{
			minX = std::min(minX, curWindow->x());
			maxX = std::max(maxX, curWindow->x() + curWindow->width());
			minY = std::min(minY, curWindow->y());
			maxY = std::max(maxY, curWindow->y() + curWindow->height());
		}
	}

	const auto pos = position(event);
	int scrollX = m_lastX - pos.x();
	int scrollY = m_lastY - pos.y();

	scrollX = scrollX < 0 && minX >= minXBoundary ? 0 : scrollX;
	scrollX = scrollX > 0 && maxX <= maxXBoundary ? 0 : scrollX;
	scrollY = scrollY < 0 && minY >= minYBoundary ? 0 : scrollY;
	scrollY = scrollY > 0 && maxY <= maxYBoundary ? 0 : scrollY;

	for (auto* curWindow : subWindows)
	{
		// if widgets are maximized, then they shouldn't be moved
		// moving a maximized window's normalGeometry is not implemented because of difficulties
		if (curWindow->isMaximized() == false)
		{
			curWindow->move(curWindow->x() - scrollX, curWindow->y() - scrollY);
		}
	}

	m_lastX = pos.x();
	m_lastY = pos.y();
}

void MainWindow::MovableQMdiArea::mouseReleaseEvent(QMouseEvent* event)
{
	setCursor(Qt::ArrowCursor);
	m_isBeingMoved = false;
}

} // namespace lmms::gui
