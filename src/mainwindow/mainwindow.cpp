/************************************************************************
**
** Authors:   Ulf Hermann <ulfonk_mennhar@gmx.de> (Alve),
**            Marek Krejza <krejza@gmail.com> (Caligor),
**            Nils Schimmelmann <nschimme@gmail.com> (Jahara)
**
** This file is part of the MMapper project.
** Maintained by Nils Schimmelmann <nschimme@gmail.com>
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the:
** Free Software Foundation, Inc.
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
************************************************************************/

#include "mainwindow.h"

#include <memory>
#include <stdexcept>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHostAddress>
#include <QIcon>
#include <QProgressDialog>
#include <QSize>
#include <QString>
#include <QTextBrowser>
#include <QtWidgets>

#include "../client/clientwidget.h"
#include "../clock/mumeclock.h"
#include "../clock/mumeclockwidget.h"
#include "../configuration/configuration.h"
#include "../display/MapCanvasData.h"
#include "../display/connectionselection.h"
#include "../display/mapcanvas.h"
#include "../display/mapwindow.h"
#include "../display/prespammedpath.h"
#include "../expandoracommon/coordinate.h"
#include "../expandoracommon/parseevent.h"
#include "../expandoracommon/room.h"
#include "../global/DirectionType.h"
#include "../global/roomid.h"
#include "../mapdata/ExitDirection.h"
#include "../mapdata/customaction.h"
#include "../mapdata/mapdata.h"
#include "../mapdata/roomselection.h"
#include "../mapfrontend/mapaction.h"
#include "../mapfrontend/mapfrontend.h"
#include "../mapstorage/abstractmapstorage.h"
#include "../mapstorage/filesaver.h"
#include "../mapstorage/jsonmapstorage.h"
#include "../mapstorage/mapstorage.h"
#include "../mapstorage/progresscounter.h"
#include "../pandoragroup/groupwidget.h"
#include "../pandoragroup/mmapper2group.h"
#include "../parser/DoorAction.h"
#include "../parser/abstractparser.h"
#include "../pathmachine/mmapper2pathmachine.h"
#include "../pathmachine/pathmachine.h"
#include "../preferences/configdialog.h"
#include "../proxy/connectionlistener.h"
#include "../proxy/telnetfilter.h"
#include "aboutdialog.h"
#include "findroomsdlg.h"
#include "roomeditattrdlg.h"
#include "welcomewidget.h"

class RoomRecipient;

DockWidget::DockWidget(const QString &title, QWidget *parent, Qt::WindowFlags flags)
    : QDockWidget(title, parent, flags)
{}

QSize MainWindow::minimumSizeHint() const
{
    return {200, 200};
}

QSize MainWindow::sizeHint() const
{
    return {500, 800};
}

QSize DockWidget::minimumSizeHint() const
{
    return {200, 0};
}

QSize DockWidget::sizeHint() const
{
    return {500, 130};
}

MainWindow::MainWindow(QWidget *parent, Qt::WindowFlags flags)
    : QMainWindow(parent, flags)
{
    setObjectName("MainWindow");
    setWindowTitle("MMapper: MUME Mapper");
    setWindowIcon(QIcon(":/icons/m.png"));

    qRegisterMetaType<RoomId>("RoomId");
    qRegisterMetaType<IncomingData>("IncomingData");
    qRegisterMetaType<CommandQueue>("CommandQueue");
    qRegisterMetaType<DirectionType>("DirectionType");
    qRegisterMetaType<DoorActionType>("DoorActionType");
    qRegisterMetaType<GroupManagerState>("GroupManagerState");
    qRegisterMetaType<SigParseEvent>("SigParseEvent");

    m_roomSelection = nullptr;
    m_connectionSelection = nullptr;

    m_mapData = new MapData();
    m_mapData->setObjectName("MapData");
    m_mapData->start();

    m_prespammedPath = new PrespammedPath(this);

    m_groupManager = new Mmapper2Group();
    m_groupManager->setObjectName("GroupManager");
    m_groupManager->start();
    m_groupWidget = new GroupWidget(m_groupManager, m_mapData, this);

    m_mapWindow = new MapWindow(m_mapData, m_prespammedPath, m_groupManager, this);
    setCentralWidget(m_mapWindow);

    m_pathMachine = new Mmapper2PathMachine();
    m_pathMachine->setObjectName("Mmapper2PathMachine");
    m_pathMachine->start();

    m_client = new ClientWidget(this);
    m_client->setObjectName("MMapper2Client");

    m_welcomeWidget = new WelcomeWidget(this);
    m_welcomeWidget->setObjectName("WelcomeWidget");
    m_dockWelcome = new DockWidget("Launch Panel", this);
    m_dockWelcome->setObjectName("DockWelcome");
    m_dockWelcome->setAllowedAreas(Qt::LeftDockWidgetArea);
    m_dockWelcome->setFeatures(QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockWelcome);
    m_dockWelcome->setWidget(m_welcomeWidget);

    m_dockDialogLog = new DockWidget(tr("Log View"), this);
    m_dockDialogLog->setObjectName("DockWidgetLog");
    m_dockDialogLog->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_dockDialogLog->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                                 | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockDialogLog);

    logWindow = new QTextBrowser(m_dockDialogLog);
    logWindow->setObjectName("LogWindow");
    m_dockDialogLog->setWidget(logWindow);
    m_dockDialogLog->hide();

    m_dockDialogGroup = new DockWidget(tr("Group Manager"), this);
    m_dockDialogGroup->setObjectName("DockWidgetGroup");
    m_dockDialogGroup->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_dockDialogGroup->setFeatures(QDockWidget::DockWidgetMovable
                                   | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::TopDockWidgetArea, m_dockDialogGroup);
    m_dockDialogGroup->setWidget(m_groupWidget);
    m_dockDialogGroup->hide();

    m_findRoomsDlg = new FindRoomsDlg(m_mapData, this);
    m_findRoomsDlg->setObjectName("FindRoomsDlg");

    m_mumeClock = new MumeClock(Config().mumeClock.startEpoch);

    createActions();
    setupToolBars();
    setupMenuBar();
    setupStatusBar();

    setCorner(Qt::TopLeftCorner, Qt::TopDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::TopDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    m_listener = new ConnectionListener(m_mapData,
                                        m_pathMachine,
                                        m_commandEvaluator,
                                        m_prespammedPath,
                                        m_groupManager,
                                        m_mumeClock,
                                        this);
    m_listener->setMaxPendingConnections(1);

    const auto port = Config().connection.localPort;
    if (!m_listener->listen(QHostAddress::Any, port)) {
        QMessageBox::critical(this,
                              tr("MMapper2"),
                              tr("Unable to start the server (switching to offline mode): %1.")
                                  .arg(m_listener->errorString()));
    } else {
        log("ConnectionListener", tr("Server bound on localhost to port: %2.").arg(port));
    }

    //update connections
    wireConnections();
    readSettings();
    if (Config().general.noLaunchPanel) {
        m_welcomeWidget->hide();
        m_dockWelcome->hide();
    } else {
        m_dockWelcome->show();
    }

    switch (Config().general.mapMode) {
    case MapMode::PLAY:
        mapperMode.playModeAct->setChecked(true);
        onPlayMode();
        break;
    case MapMode::MAP:
        mapperMode.mapModeAct->setChecked(true);
        onMapMode();
        break;
    case MapMode::OFFLINE:
        mapperMode.offlineModeAct->setChecked(true);
        onOfflineMode();
        break;
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::readSettings()
{
    const auto &settings = Config().general;
    resize(settings.windowSize);
    move(settings.windowPosition);
    restoreState(settings.windowState);
    alwaysOnTopAct->setChecked(settings.alwaysOnTop);
    if (settings.alwaysOnTop) {
        alwaysOnTop();
    }
    auto position = pos();
    if (position.x() < 0) {
        position.setX(0);
    }
    if (position.y() < 0) {
        position.setY(0);
    }
    move(position);
}

void MainWindow::writeSettings()
{
    Config().setWindowPosition(pos());
    Config().setWindowSize(size());
    Config().setWindowState(saveState());
    Config().setAlwaysOnTop(static_cast<bool>(windowFlags() & Qt::WindowStaysOnTopHint));
}

void MainWindow::wireConnections()
{
    connect(m_pathMachine, &Mmapper2PathMachine::log, this, &MainWindow::log);

    connect(m_pathMachine,
            SIGNAL(lookingForRooms(RoomRecipient &, const Coordinate &)),
            m_mapData,
            SLOT(lookingForRooms(RoomRecipient &, const Coordinate &)));
    connect(m_pathMachine,
            static_cast<void (Mmapper2PathMachine::*)(RoomRecipient &, const SigParseEvent &)>(
                &Mmapper2PathMachine::lookingForRooms),
            m_mapData,
            static_cast<void (MapData::*)(RoomRecipient &, const SigParseEvent &)>(
                &MapData::lookingForRooms));
    connect(m_pathMachine,
            SIGNAL(lookingForRooms(RoomRecipient &, RoomId)),
            m_mapData,
            SLOT(lookingForRooms(RoomRecipient &, RoomId)));
    connect(m_mapData, &MapFrontend::clearingMap, m_pathMachine, &PathMachine::releaseAllPaths);
    connect(m_pathMachine,
            &Mmapper2PathMachine::playerMoved,
            m_mapWindow->getCanvas(),
            &MapCanvas::moveMarker);

    connect(m_mapWindow->getCanvas(),
            SIGNAL(setCurrentRoom(RoomId)),
            m_pathMachine,
            SLOT(setCurrentRoom(RoomId)));
    connect(m_mapWindow->getCanvas(),
            &MapCanvas::charMovedEvent,
            m_pathMachine,
            &Mmapper2PathMachine::event);

    //moved to mapwindow
    connect(m_mapData, &MapData::mapSizeChanged, m_mapWindow, &MapWindow::setScrollBars);
    connect(m_mapWindow->getCanvas(),
            &MapCanvas::roomPositionChanged,
            m_pathMachine,
            &Mmapper2PathMachine::retry);

    connect(m_prespammedPath, SIGNAL(update()), m_mapWindow->getCanvas(), SLOT(update()));

    connect(m_mapData, &MapData::log, this, &MainWindow::log);
    connect(m_mapWindow->getCanvas(), &MapCanvas::log, this, &MainWindow::log);

    connect(m_mapData, &MapData::onDataLoaded, m_mapWindow->getCanvas(), &MapCanvas::dataLoaded);

    connect(zoomInAct, &QAction::triggered, m_mapWindow->getCanvas(), &MapCanvas::zoomIn);
    connect(zoomOutAct, &QAction::triggered, m_mapWindow->getCanvas(), &MapCanvas::zoomOut);

    connect(m_mapWindow->getCanvas(),
            &MapCanvas::newRoomSelection,
            this,
            &MainWindow::newRoomSelection);
    connect(m_mapWindow->getCanvas(),
            &MapCanvas::newConnectionSelection,
            this,
            &MainWindow::newConnectionSelection);
    connect(m_mapWindow->getCanvas(),
            &QWidget::customContextMenuRequested,
            this,
            &MainWindow::showContextMenu);

    // Group
    connect(m_groupManager, &Mmapper2Group::log, this, &MainWindow::log);
    connect(m_pathMachine,
            &PathMachine::setCharPosition,
            m_groupManager,
            &Mmapper2Group::setCharPosition,
            Qt::QueuedConnection);
    connect(m_groupManager,
            SIGNAL(drawCharacters()),
            m_mapWindow->getCanvas(),
            SLOT(update()),
            Qt::QueuedConnection);
    connect(this,
            &MainWindow::setGroupManagerType,
            m_groupManager,
            &Mmapper2Group::setType,
            Qt::QueuedConnection);
    connect(m_groupManager,
            &Mmapper2Group::groupManagerOff,
            this,
            &MainWindow::groupManagerOff,
            Qt::QueuedConnection);

    connect(m_mumeClock, &MumeClock::log, this, &MainWindow::log);

    connect(m_welcomeWidget, &WelcomeWidget::playMumeClicked, this, &MainWindow::onLaunchClient);
    connect(m_listener,
            &ConnectionListener::clientSuccessfullyConnected,
            m_welcomeWidget,
            &QWidget::hide);
    connect(m_listener,
            &ConnectionListener::clientSuccessfullyConnected,
            m_dockWelcome,
            &QWidget::hide);
}

void MainWindow::log(const QString &module, const QString &message)
{
    logWindow->append("[" + module + "] " + message);
    logWindow->ensureCursorVisible();
    logWindow->update();
}

void MainWindow::createActions()
{
    newAct = new QAction(QIcon::fromTheme("document-new", QIcon(":/icons/new.png")),
                         tr("&New"),
                         this);
    newAct->setShortcut(tr("Ctrl+N"));
    newAct->setStatusTip(tr("Create a new file"));
    connect(newAct, &QAction::triggered, this, &MainWindow::newFile);

    openAct = new QAction(QIcon::fromTheme("document-open", QIcon(":/icons/open.png")),
                          tr("&Open..."),
                          this);
    openAct->setShortcut(tr("Ctrl+O"));
    openAct->setStatusTip(tr("Open an existing file"));
    connect(openAct, &QAction::triggered, this, &MainWindow::open);

    reloadAct = new QAction(QIcon::fromTheme("document-open-recent", QIcon(":/icons/reload.png")),
                            tr("&Reload"),
                            this);
    reloadAct->setShortcut(tr("Ctrl+R"));
    reloadAct->setStatusTip(tr("Reload the current map"));
    connect(reloadAct, &QAction::triggered, this, &MainWindow::reload);

    saveAct = new QAction(QIcon::fromTheme("document-save", QIcon(":/icons/save.png")),
                          tr("&Save"),
                          this);
    saveAct->setShortcut(tr("Ctrl+S"));
    saveAct->setStatusTip(tr("Save the document to disk"));
    connect(saveAct, &QAction::triggered, this, &MainWindow::save);

    saveAsAct = new QAction(QIcon::fromTheme("document-save-as"), tr("Save &As..."), this);
    saveAsAct->setStatusTip(tr("Save the document under a new name"));
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveAs);

    exportBaseMapAct = new QAction(QIcon::fromTheme("document-send"),
                                   tr("Export &Base Map As..."),
                                   this);
    exportBaseMapAct->setStatusTip(tr("Save a copy of the map with no secrets"));
    connect(exportBaseMapAct, &QAction::triggered, this, &MainWindow::exportBaseMap);

    exportWebMapAct = new QAction(tr("Export &Web Map As..."), this);
    exportWebMapAct->setStatusTip(tr("Save a copy of the map for webclients"));
    connect(exportWebMapAct, &QAction::triggered, this, &MainWindow::exportWebMap);

    mergeAct = new QAction(QIcon(":/icons/merge.png"), tr("&Merge..."), this);
    //mergeAct->setShortcut(tr("Ctrl+M"));
    mergeAct->setStatusTip(tr("Merge an existing file into current map"));
    connect(mergeAct, &QAction::triggered, this, &MainWindow::merge);

    exitAct = new QAction(QIcon::fromTheme("application-exit"), tr("E&xit"), this);
    exitAct->setShortcut(tr("Ctrl+Q"));
    exitAct->setStatusTip(tr("Exit the application"));
    connect(exitAct, &QAction::triggered, this, &QWidget::close);

    /*
    cutAct = new QAction(QIcon(":/icons/cut.png"), tr("Cu&t"), this);
    cutAct->setShortcut(tr("Ctrl+X"));
    cutAct->setStatusTip(tr("Cut the current selection's contents to the "
                            "clipboard"));

    copyAct = new QAction(QIcon(":/icons/copy.png"), tr("&Copy"), this);
    copyAct->setShortcut(tr("Ctrl+C"));
    copyAct->setStatusTip(tr("Copy the current selection's contents to the "
                             "clipboard"));

    pasteAct = new QAction(QIcon(":/icons/paste.png"), tr("&Paste"), this);
    pasteAct->setShortcut(tr("Ctrl+V"));
    pasteAct->setStatusTip(tr("Paste the clipboard's contents into the current "
                              "selection"));
    //connect(pasteAct, SIGNAL(triggered()), textEdit, SLOT(paste()));
    */

    preferencesAct = new QAction(QIcon::fromTheme("preferences-desktop",
                                                  QIcon(":/icons/preferences.png")),
                                 tr("&Preferences"),
                                 this);
    preferencesAct->setShortcut(tr("Ctrl+P"));
    preferencesAct->setStatusTip(tr("MMapper2 configuration"));
    connect(preferencesAct, &QAction::triggered, this, &MainWindow::onPreferences);

    mmapperCheckForUpdateAct = new QAction(QIcon(":/icons/m.png"), tr("Check for &update"), this);
    connect(mmapperCheckForUpdateAct, &QAction::triggered, this, &MainWindow::onCheckForUpdate);
    mumeWebsiteAct = new QAction(tr("&Website"), this);
    connect(mumeWebsiteAct, &QAction::triggered, this, &MainWindow::openMumeWebsite);
    voteAct = new QAction(QIcon::fromTheme("applications-games"), tr("V&ote for Mume"), this);
    voteAct->setStatusTip(tr("Please vote for MUME on \"The Mud Connector\""));
    connect(voteAct, &QAction::triggered, this, &MainWindow::voteForMUMEOnTMC);
    mumeWebsiteAct = new QAction(tr("&Website"), this);
    connect(mumeWebsiteAct, &QAction::triggered, this, &MainWindow::openMumeWebsite);
    mumeForumAct = new QAction(tr("&Forum"), this);
    connect(mumeForumAct, &QAction::triggered, this, &MainWindow::openMumeForum);
    mumeWikiAct = new QAction(tr("W&iki"), this);
    connect(mumeWikiAct, &QAction::triggered, this, &MainWindow::openMumeWiki);
    settingUpMmapperAct = new QAction(tr("&Setting up MMapper"), this);
    connect(settingUpMmapperAct, &QAction::triggered, this, &MainWindow::openSettingUpMmapper);
    newbieAct = new QAction(tr("&Information for Newcomers"), this);
    newbieAct->setStatusTip("Newbie help on the MUME website");
    connect(newbieAct, &QAction::triggered, this, &MainWindow::openNewbieHelp);
    aboutAct = new QAction(QIcon::fromTheme("help-about"), tr("About &MMapper"), this);
    aboutAct->setStatusTip(tr("Show the application's About box"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);
    aboutQtAct = new QAction(tr("About &Qt"), this);
    aboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
    connect(aboutQtAct, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    /*
        nextWindowAct = new QAction(tr("Ne&xt Window"), this);
        nextWindowAct->setStatusTip(tr("Show the Next Window"));
        connect(nextWindowAct, SIGNAL(triggered()), this, SLOT(nextWindow()));

        prevWindowAct = new QAction(tr("Prev&ious Window"), this);
        prevWindowAct->setStatusTip(tr("Show the Previous Window"));
        connect(prevWindowAct, SIGNAL(triggered()), this, SLOT(prevWindow()));
    */
    zoomInAct = new QAction(QIcon::fromTheme("zoom-in", QIcon(":/icons/viewmag+.png")),
                            tr("Zoom In"),
                            this);
    alwaysOnTopAct = new QAction(tr("Always on top"), this);
    alwaysOnTopAct->setCheckable(true);
    connect(alwaysOnTopAct, &QAction::triggered, this, &MainWindow::alwaysOnTop);

    zoomInAct->setStatusTip(tr("Zooms In current map"));
    zoomInAct->setShortcut(tr("Ctrl++"));
    zoomOutAct = new QAction(QIcon::fromTheme("zoom-out", QIcon(":/icons/viewmag-.png")),
                             tr("Zoom Out"),
                             this);
    zoomOutAct->setShortcut(tr("Ctrl+-"));
    zoomOutAct->setStatusTip(tr("Zooms Out current map"));
    layerUpAct = new QAction(QIcon::fromTheme("go-up", QIcon(":/icons/layerup.png")),
                             tr("Layer Up"),
                             this);
#ifdef __APPLE__
    layerUpAct->setShortcut(tr("Meta+Tab"));
#else
    layerUpAct->setShortcut(tr("Ctrl+Tab"));
#endif
    layerUpAct->setStatusTip(tr("Layer Up"));
    connect(layerUpAct, &QAction::triggered, this, &MainWindow::onLayerUp);
    layerDownAct = new QAction(QIcon::fromTheme("go-down", QIcon(":/icons/layerdown.png")),
                               tr("Layer Down"),
                               this);
#ifdef __APPLE__
    layerDownAct->setShortcut(tr("Meta+Shift+Tab"));
#else
    layerDownAct->setShortcut(tr("Ctrl+Shift+Tab"));
#endif
    layerDownAct->setStatusTip(tr("Layer Down"));
    connect(layerDownAct, &QAction::triggered, this, &MainWindow::onLayerDown);

    mouseMode.modeConnectionSelectAct = new QAction(QIcon(":/icons/connectionselection.png"),
                                          tr("Select Connection"),
                                          this);
    mouseMode.modeConnectionSelectAct->setStatusTip(tr("Select Connection"));
    mouseMode.modeConnectionSelectAct->setCheckable(true);
    connect(mouseMode.modeConnectionSelectAct, &QAction::triggered, this, &MainWindow::onModeConnectionSelect);
    mouseMode.modeRoomSelectAct = new QAction(QIcon(":/icons/roomselection.png"), tr("Select Rooms"), this);
    mouseMode.modeRoomSelectAct->setStatusTip(tr("Select Rooms"));
    mouseMode.modeRoomSelectAct->setCheckable(true);
    connect(mouseMode.modeRoomSelectAct, &QAction::triggered, this, &MainWindow::onModeRoomSelect);
    mouseMode.modeMoveSelectAct = new QAction(QIcon(":/icons/mapmove.png"), tr("Move map"), this);
    mouseMode.modeMoveSelectAct->setStatusTip(tr("Move Map"));
    mouseMode.modeMoveSelectAct->setCheckable(true);
    connect(mouseMode.modeMoveSelectAct, &QAction::triggered, this, &MainWindow::onModeMoveSelect);
    mouseMode.modeInfoMarkEditAct = new QAction(QIcon(":/icons/infomarksedit.png"),
                                      tr("Edit Info Marks"),
                                      this);
    mouseMode.modeInfoMarkEditAct->setStatusTip(tr("Edit Info Marks"));
    mouseMode.modeInfoMarkEditAct->setCheckable(true);
    connect(mouseMode.modeInfoMarkEditAct, &QAction::triggered, this, &MainWindow::onModeInfoMarkEdit);

    mouseMode.modeCreateRoomAct = new QAction(QIcon(":/icons/roomcreate.png"), tr("Create New Rooms"), this);
    mouseMode.modeCreateRoomAct->setStatusTip(tr("Create New Rooms"));
    mouseMode.modeCreateRoomAct->setCheckable(true);
    connect(mouseMode.modeCreateRoomAct, &QAction::triggered, this, &MainWindow::onModeCreateRoomSelect);

    mouseMode.modeCreateConnectionAct = new QAction(QIcon(":/icons/connectioncreate.png"),
                                          tr("Create New Connection"),
                                          this);
    mouseMode.modeCreateConnectionAct->setStatusTip(tr("Create New Connection"));
    mouseMode.modeCreateConnectionAct->setCheckable(true);
    connect(mouseMode.modeCreateConnectionAct,
            &QAction::triggered,
            this,
            &MainWindow::onModeCreateConnectionSelect);

    mouseMode.modeCreateOnewayConnectionAct = new QAction(QIcon(":/icons/onewayconnectioncreate.png"),
                                                tr("Create New Oneway Connection"),
                                                this);
    mouseMode.modeCreateOnewayConnectionAct->setStatusTip(tr("Create New Oneway Connection"));
    mouseMode.modeCreateOnewayConnectionAct->setCheckable(true);
    connect(mouseMode.modeCreateOnewayConnectionAct,
            &QAction::triggered,
            this,
            &MainWindow::onModeCreateOnewayConnectionSelect);

    mouseMode.mouseModeActGroup = new QActionGroup(this);
    mouseMode.mouseModeActGroup->setExclusive(true);
    mouseMode.mouseModeActGroup->addAction(mouseMode.modeMoveSelectAct);
    mouseMode.mouseModeActGroup->addAction(mouseMode.modeRoomSelectAct);
    mouseMode.mouseModeActGroup->addAction(mouseMode.modeConnectionSelectAct);
    mouseMode.mouseModeActGroup->addAction(mouseMode.modeCreateRoomAct);
    mouseMode.mouseModeActGroup->addAction(mouseMode.modeCreateConnectionAct);
    mouseMode.mouseModeActGroup->addAction(mouseMode.modeCreateOnewayConnectionAct);
    mouseMode.mouseModeActGroup->addAction(mouseMode.modeInfoMarkEditAct);
    mouseMode.modeMoveSelectAct->setChecked(true);

    createRoomAct = new QAction(QIcon(":/icons/roomcreate.png"), tr("Create New Room"), this);
    createRoomAct->setStatusTip(tr("Create a new room under the cursor"));
    connect(createRoomAct, &QAction::triggered, this, &MainWindow::onCreateRoom);

    editRoomSelectionAct = new QAction(QIcon(":/icons/roomedit.png"),
                                       tr("Edit Selected Rooms"),
                                       this);
    editRoomSelectionAct->setStatusTip(tr("Edit Selected Rooms"));
    editRoomSelectionAct->setShortcut(tr("Ctrl+E"));
    connect(editRoomSelectionAct, &QAction::triggered, this, &MainWindow::onEditRoomSelection);

    deleteRoomSelectionAct = new QAction(QIcon(":/icons/roomdelete.png"),
                                         tr("Delete Selected Rooms"),
                                         this);
    deleteRoomSelectionAct->setStatusTip(tr("Delete Selected Rooms"));
    connect(deleteRoomSelectionAct, &QAction::triggered, this, &MainWindow::onDeleteRoomSelection);

    moveUpRoomSelectionAct = new QAction(QIcon(":/icons/roommoveup.png"),
                                         tr("Move Up Selected Rooms"),
                                         this);
    moveUpRoomSelectionAct->setStatusTip(tr("Move Up Selected Rooms"));
    connect(moveUpRoomSelectionAct, &QAction::triggered, this, &MainWindow::onMoveUpRoomSelection);
    moveDownRoomSelectionAct = new QAction(QIcon(":/icons/roommovedown.png"),
                                           tr("Move Down Selected Rooms"),
                                           this);
    moveDownRoomSelectionAct->setStatusTip(tr("Move Down Selected Rooms"));
    connect(moveDownRoomSelectionAct,
            &QAction::triggered,
            this,
            &MainWindow::onMoveDownRoomSelection);
    mergeUpRoomSelectionAct = new QAction(QIcon(":/icons/roommergeup.png"),
                                          tr("Merge Up Selected Rooms"),
                                          this);
    mergeUpRoomSelectionAct->setStatusTip(tr("Merge Up Selected Rooms"));
    connect(mergeUpRoomSelectionAct, &QAction::triggered, this, &MainWindow::onMergeUpRoomSelection);
    mergeDownRoomSelectionAct = new QAction(QIcon(":/icons/roommergedown.png"),
                                            tr("Merge Down Selected Rooms"),
                                            this);
    mergeDownRoomSelectionAct->setStatusTip(tr("Merge Down Selected Rooms"));
    connect(mergeDownRoomSelectionAct,
            &QAction::triggered,
            this,
            &MainWindow::onMergeDownRoomSelection);
    connectToNeighboursRoomSelectionAct = new QAction(QIcon(":/icons/roomconnecttoneighbours.png"),
                                                      tr("Connect room(s) to its neighbour rooms"),
                                                      this);
    connectToNeighboursRoomSelectionAct->setStatusTip(tr("Connect room(s) to its neighbour rooms"));
    connect(connectToNeighboursRoomSelectionAct,
            &QAction::triggered,
            this,
            &MainWindow::onConnectToNeighboursRoomSelection);

    findRoomsAct = new QAction(QIcon(":/icons/roomfind.png"), tr("&Find Rooms"), this);
    findRoomsAct->setStatusTip(tr("Find matching rooms"));
    findRoomsAct->setShortcut(tr("Ctrl+F"));
    connect(findRoomsAct, &QAction::triggered, this, &MainWindow::onFindRoom);

    clientAct = new QAction(QIcon(":/icons/terminal.png"), tr("Integrated Mud &Client"), this);
    clientAct->setStatusTip(tr("Launch the integrated mud client"));
    connect(clientAct, &QAction::triggered, this, &MainWindow::onLaunchClient);

    releaseAllPathsAct = new QAction(QIcon(":/icons/cancel.png"), tr("Release All Paths"), this);
    releaseAllPathsAct->setStatusTip(tr("Release All Paths"));
    releaseAllPathsAct->setCheckable(false);
    connect(releaseAllPathsAct, &QAction::triggered, m_pathMachine, &PathMachine::releaseAllPaths);

    forceRoomAct = new QAction(QIcon(":/icons/force.png"),
                               tr("Force Path Machine to selected Room"),
                               this);
    forceRoomAct->setStatusTip(tr("Force Path Machine to selected Room"));
    forceRoomAct->setCheckable(false);
    forceRoomAct->setEnabled(false);
    connect(forceRoomAct,
            &QAction::triggered,
            m_mapWindow->getCanvas(),
            &MapCanvas::forceMapperToRoom);

    selectedRoomActGroup = new QActionGroup(this);
    selectedRoomActGroup->setExclusive(false);
    selectedRoomActGroup->addAction(editRoomSelectionAct);
    selectedRoomActGroup->addAction(deleteRoomSelectionAct);
    selectedRoomActGroup->addAction(moveUpRoomSelectionAct);
    selectedRoomActGroup->addAction(moveDownRoomSelectionAct);
    selectedRoomActGroup->addAction(mergeUpRoomSelectionAct);
    selectedRoomActGroup->addAction(mergeDownRoomSelectionAct);
    selectedRoomActGroup->addAction(connectToNeighboursRoomSelectionAct);
    selectedRoomActGroup->setEnabled(false);

    //editConnectionSelectionAct = new QAction(QIcon(":/icons/connectionedit.png"), tr("Edit Selected Connection"), this);
    //editConnectionSelectionAct->setStatusTip(tr("Edit Selected Connection"));
    //connect(editConnectionSelectionAct, SIGNAL(triggered()), this, SLOT(onEditConnectionSelection()));

    deleteConnectionSelectionAct = new QAction(QIcon(":/icons/connectiondelete.png"),
                                               tr("Delete Selected Connection"),
                                               this);
    deleteConnectionSelectionAct->setStatusTip(tr("Delete Selected Connection"));
    connect(deleteConnectionSelectionAct,
            &QAction::triggered,
            this,
            &MainWindow::onDeleteConnectionSelection);

    selectedConnectionActGroup = new QActionGroup(this);
    selectedConnectionActGroup->setExclusive(false);
    //connectionActGroup->addAction(editConnectionSelectionAct);
    selectedConnectionActGroup->addAction(deleteConnectionSelectionAct);
    selectedConnectionActGroup->setEnabled(false);

    mapperMode.playModeAct = new QAction(QIcon(":/icons/online.png"), tr("Switch to play mode"), this);
    mapperMode.playModeAct->setStatusTip(tr("Switch to play mode - no new rooms are created"));
    mapperMode.playModeAct->setCheckable(true);
    connect(mapperMode.playModeAct, &QAction::triggered, this, &MainWindow::onPlayMode);

    mapperMode.mapModeAct = new QAction(QIcon(":/icons/map.png"), tr("Switch to mapping mode"), this);
    mapperMode.mapModeAct->setStatusTip(tr("Switch to mapping mode - new rooms are created when moving"));
    mapperMode.mapModeAct->setCheckable(true);
    connect(mapperMode.mapModeAct, &QAction::triggered, this, &MainWindow::onMapMode);

    mapperMode.offlineModeAct = new QAction(QIcon(":/icons/play.png"),
                                 tr("Switch to offline emulation mode"),
                                 this);
    mapperMode.offlineModeAct->setStatusTip(
        tr("Switch to offline emulation mode - you can learn areas offline"));
    mapperMode.offlineModeAct->setCheckable(true);
    connect(mapperMode.offlineModeAct, &QAction::triggered, this, &MainWindow::onOfflineMode);

    mapperMode.mapModeActGroup = new QActionGroup(this);
    mapperMode.mapModeActGroup->setExclusive(true);
    mapperMode.mapModeActGroup->addAction(mapperMode.playModeAct);
    mapperMode.mapModeActGroup->addAction(mapperMode.mapModeAct);
    mapperMode.mapModeActGroup->addAction(mapperMode.offlineModeAct);
    mapperMode.mapModeActGroup->setEnabled(true);

    //cutAct->setEnabled(false);
    //copyAct->setEnabled(false);

    // Find Room Dialog Connections
    connect(m_findRoomsDlg, &FindRoomsDlg::center, m_mapWindow, &MapWindow::center);
    connect(m_findRoomsDlg, &FindRoomsDlg::log, this, &MainWindow::log);

    // group Manager
    groupMode.groupOffAct = new QAction(QIcon(":/icons/groupoff.png"), tr("&Off"), this);
    groupMode.groupOffAct->setShortcut(tr("Ctrl+G"));
    groupMode.groupOffAct->setCheckable(true);
    connect(groupMode.groupOffAct, &QAction::triggered, this, &MainWindow::groupOff, Qt::QueuedConnection);

    groupMode.groupClientAct = new QAction(QIcon(":/icons/groupclient.png"),
                                 tr("&Connect to a friend's map"),
                                 this);
    groupMode.groupClientAct->setCheckable(true);
    connect(groupMode.groupClientAct,
            &QAction::triggered,
            this,
            &MainWindow::groupClient,
            Qt::QueuedConnection);

    groupMode.groupServerAct = new QAction(QIcon(":/icons/groupserver.png"),
                                 tr("&Host your map with friends"),
                                 this);
    groupMode.groupServerAct->setCheckable(true);
    connect(groupMode.groupServerAct,
            &QAction::triggered,
            this,
            &MainWindow::groupServer,
            Qt::QueuedConnection);

    groupMode.groupManagerGroup = new QActionGroup(this);
    groupMode.groupManagerGroup->setExclusive(true);
    groupMode.groupManagerGroup->addAction(groupMode.groupOffAct);
    groupMode.groupManagerGroup->addAction(groupMode.groupClientAct);
    groupMode.groupManagerGroup->addAction(groupMode.groupServerAct);
    groupManagerOff();
}

void MainWindow::onPlayMode()
{
    disconnect(m_pathMachine, &Mmapper2PathMachine::createRoom, m_mapData, &MapData::createRoom);
    disconnect(m_pathMachine,
               &Mmapper2PathMachine::scheduleAction,
               m_mapData,
               &MapData::scheduleAction);
    Config().general.mapMode = MapMode::PLAY;
    modeMenu->setIcon(mapperMode.playModeAct->icon());
}

void MainWindow::onMapMode()
{
    log("MainWindow", "Map mode selected - new rooms are created when entering unmapped areas.");
    connect(m_pathMachine, &Mmapper2PathMachine::createRoom, m_mapData, &MapData::createRoom);
    connect(m_pathMachine,
            &Mmapper2PathMachine::scheduleAction,
            m_mapData,
            &MapData::scheduleAction);
    Config().general.mapMode = MapMode::MAP;
    modeMenu->setIcon(mapperMode.mapModeAct->icon());
}

void MainWindow::onOfflineMode()
{
    log("MainWindow", "Offline emulation mode selected - learn new areas safely.");
    disconnect(m_pathMachine, &Mmapper2PathMachine::createRoom, m_mapData, &MapData::createRoom);
    disconnect(m_pathMachine,
               &Mmapper2PathMachine::scheduleAction,
               m_mapData,
               &MapData::scheduleAction);
    Config().general.mapMode = MapMode::OFFLINE;
    modeMenu->setIcon(mapperMode.offlineModeAct->icon());
}

void MainWindow::disableActions(bool value)
{
    newAct->setDisabled(value);
    openAct->setDisabled(value);
    mergeAct->setDisabled(value);
    reloadAct->setDisabled(value);
    saveAct->setDisabled(value);
    saveAsAct->setDisabled(value);
    exportBaseMapAct->setDisabled(value);
    exportWebMapAct->setDisabled(value);
    exitAct->setDisabled(value);
    //cutAct->setDisabled(value);
    //copyAct->setDisabled(value);
    //pasteAct->setDisabled(value);
    aboutAct->setDisabled(value);
    aboutQtAct->setDisabled(value);
    //    nextWindowAct->setDisabled(value);
    //    prevWindowAct->setDisabled(value);
    zoomInAct->setDisabled(value);
    zoomOutAct->setDisabled(value);
    mapperMode.playModeAct->setDisabled(value);
    mapperMode.mapModeAct->setDisabled(value);
    mouseMode.modeRoomSelectAct->setDisabled(value);
    mouseMode.modeConnectionSelectAct->setDisabled(value);
    mouseMode.modeMoveSelectAct->setDisabled(value);
    mouseMode.modeInfoMarkEditAct->setDisabled(value);
    layerUpAct->setDisabled(value);
    layerDownAct->setDisabled(value);
    mouseMode.modeCreateRoomAct->setDisabled(value);
    mouseMode.modeCreateConnectionAct->setDisabled(value);
    mouseMode.modeCreateOnewayConnectionAct->setDisabled(value);
    releaseAllPathsAct->setDisabled(value);
    alwaysOnTopAct->setDisabled(value);
}

void MainWindow::setupMenuBar()
{
    fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(newAct);
    fileMenu->addAction(openAct);
    fileMenu->addAction(reloadAct);
    fileMenu->addAction(saveAct);
    fileMenu->addAction(saveAsAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exportBaseMapAct);
    fileMenu->addAction(exportWebMapAct);
    fileMenu->addAction(mergeAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct);

    editMenu = menuBar()->addMenu(tr("&Edit"));
    //editMenu->addAction(cutAct);
    //editMenu->addAction(copyAct);
    //editMenu->addAction(pasteAct);
    modeMenu = editMenu->addMenu(QIcon(":/icons/online.png"), tr("&Mode"));
    modeMenu->addAction(mapperMode.playModeAct);
    modeMenu->addAction(mapperMode.mapModeAct);
    modeMenu->addAction(mapperMode.offlineModeAct);
    editMenu->addSeparator();

    editMenu->addAction(mouseMode.modeInfoMarkEditAct);

    roomMenu = editMenu->addMenu(QIcon(":/icons/roomselection.png"), tr("&Rooms"));
    roomMenu->addAction(mouseMode.modeRoomSelectAct);
    roomMenu->addSeparator();
    roomMenu->addAction(mouseMode.modeCreateRoomAct);
    roomMenu->addAction(editRoomSelectionAct);
    roomMenu->addAction(deleteRoomSelectionAct);
    roomMenu->addAction(moveUpRoomSelectionAct);
    roomMenu->addAction(moveDownRoomSelectionAct);
    roomMenu->addAction(mergeUpRoomSelectionAct);
    roomMenu->addAction(mergeDownRoomSelectionAct);
    roomMenu->addAction(connectToNeighboursRoomSelectionAct);

    connectionMenu = editMenu->addMenu(QIcon(":/icons/connectionselection.png"), tr("&Connections"));
    connectionMenu->addAction(mouseMode.modeConnectionSelectAct);
    connectionMenu->addSeparator();
    connectionMenu->addAction(mouseMode.modeCreateConnectionAct);
    connectionMenu->addAction(mouseMode.modeCreateOnewayConnectionAct);
    //connectionMenu->addAction(editConnectionSelectionAct);
    connectionMenu->addAction(deleteConnectionSelectionAct);

    editMenu->addSeparator();
    editMenu->addAction(findRoomsAct);
    editMenu->addAction(preferencesAct);

    //editMenu->addAction(createRoomAct);
    //editMenu->addAction(createConnectionAct);

    viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(mouseMode.modeMoveSelectAct);
    QMenu *toolbars = viewMenu->addMenu(tr("&Toolbars"));
    toolbars->addAction(fileToolBar->toggleViewAction());
    //toolbars->addAction(editToolBar->toggleViewAction());
    toolbars->addAction(mapperModeToolBar->toggleViewAction());
    toolbars->addAction(mouseModeToolBar->toggleViewAction());
    toolbars->addAction(viewToolBar->toggleViewAction());
    toolbars->addAction(pathMachineToolBar->toggleViewAction());
    toolbars->addAction(roomToolBar->toggleViewAction());
    toolbars->addAction(connectionToolBar->toggleViewAction());
    toolbars->addAction(groupToolBar->toggleViewAction());
    toolbars->addAction(settingsToolBar->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(zoomInAct);
    viewMenu->addAction(zoomOutAct);
    viewMenu->addSeparator();
    viewMenu->addAction(layerUpAct);
    viewMenu->addAction(layerDownAct);
    viewMenu->addSeparator();
    viewMenu->addAction(alwaysOnTopAct);

    //    windowMenu->addAction(nextWindowAct);
    //    windowMenu->addAction(prevWindowAct);
    //    windowMenu->addSeparator();

    settingsMenu = menuBar()->addMenu(tr("&Tools"));
    settingsMenu->addAction(clientAct);
    groupMenu = settingsMenu->addMenu(QIcon(":/icons/groupclient.png"), tr("&Group Manager"));
    groupMenu->addAction(groupMode.groupOffAct);
    groupMenu->addAction(groupMode.groupClientAct);
    groupMenu->addAction(groupMode.groupServerAct);
    QMenu *pathMachineMenu = settingsMenu->addMenu(QIcon(":/icons/force.png"), tr("&Path Machine"));
    pathMachineMenu->addAction(mouseMode.modeRoomSelectAct);
    pathMachineMenu->addSeparator();
    pathMachineMenu->addAction(forceRoomAct);
    pathMachineMenu->addAction(releaseAllPathsAct);
    settingsMenu->addSeparator();
    settingsMenu->addAction(m_dockDialogLog->toggleViewAction());

    helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(voteAct);
    helpMenu->addSeparator();
    helpMenu->addAction(mmapperCheckForUpdateAct);
    mumeMenu = helpMenu->addMenu(QIcon::fromTheme("help-contents"), tr("M&UME"));
    mumeMenu->addAction(mumeWebsiteAct);
    mumeMenu->addAction(mumeForumAct);
    mumeMenu->addAction(mumeWikiAct);
    onlineTutorialsMenu = helpMenu->addMenu(QIcon::fromTheme("help-faq"), tr("Online &Tutorials"));
    onlineTutorialsMenu->addAction(newbieAct);
    onlineTutorialsMenu->addAction(settingUpMmapperAct);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAct);
    helpMenu->addAction(aboutQtAct);

    /*
    searchMenu = menuBar()->addMenu(tr("Sea&rch"));
    settingsMenu = menuBar()->addMenu(tr("&Settings"));
    */
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    QMenu contextMenu(tr("Context menu"), this);
    if (m_connectionSelection != nullptr) {
        contextMenu.addAction(deleteConnectionSelectionAct);
    } else if (m_roomSelection != nullptr) {
        contextMenu.addAction(editRoomSelectionAct);
        contextMenu.addAction(moveUpRoomSelectionAct);
        contextMenu.addAction(moveDownRoomSelectionAct);
        contextMenu.addAction(mergeUpRoomSelectionAct);
        contextMenu.addAction(mergeDownRoomSelectionAct);
        contextMenu.addAction(deleteRoomSelectionAct);
        contextMenu.addAction(connectToNeighboursRoomSelectionAct);
        contextMenu.addSeparator();
        contextMenu.addAction(forceRoomAct);
    } else if (m_connectionSelection == nullptr && m_roomSelection == nullptr) {
           contextMenu.addAction(createRoomAct);
    }
    contextMenu.addSeparator();
    QMenu *mouseMenu = contextMenu.addMenu(QIcon::fromTheme("input-mouse"), "Mouse Mode");
    mouseMenu->addAction(mouseMode.modeMoveSelectAct);
    mouseMenu->addAction(mouseMode.modeRoomSelectAct);
    mouseMenu->addAction(mouseMode.modeConnectionSelectAct);
    mouseMenu->addAction(mouseMode.modeCreateRoomAct);
    mouseMenu->addAction(mouseMode.modeCreateConnectionAct);
    mouseMenu->addAction(mouseMode.modeCreateOnewayConnectionAct);
    mouseMenu->addAction(mouseMode.modeInfoMarkEditAct);

    contextMenu.exec(m_mapWindow->getCanvas()->mapToGlobal(pos));
}

void MainWindow::alwaysOnTop()
{
    setWindowFlags(windowFlags() ^ Qt::WindowStaysOnTopHint);
    show();
}

void MainWindow::setupToolBars()
{
    fileToolBar = addToolBar(tr("File"));
    fileToolBar->setObjectName("FileToolBar");
    fileToolBar->addAction(newAct);
    fileToolBar->addAction(openAct);
    //  fileToolBar->addAction(mergeAct);
    //  fileToolBar->addAction(reloadAct);
    fileToolBar->addAction(saveAct);
    fileToolBar->hide();
    /*
        editToolBar = addToolBar(tr("Edit"));
        editToolBar->addAction(cutAct);
        editToolBar->addAction(copyAct);
        editToolBar->addAction(pasteAct);
    */

    mapperModeToolBar = addToolBar(tr("Mapper Mode"));
    mapperModeToolBar->setObjectName("MapperModeToolBar");
    mapperModeToolBar->addAction(mapperMode.playModeAct);
    mapperModeToolBar->addAction(mapperMode.mapModeAct);
    mapperModeToolBar->addAction(mapperMode.offlineModeAct);
    mapperModeToolBar->hide();

    mouseModeToolBar = addToolBar(tr("Mouse Mode"));
    mouseModeToolBar->setObjectName("ModeToolBar");
    mouseModeToolBar->addAction(mouseMode.modeMoveSelectAct);
    mouseModeToolBar->addAction(mouseMode.modeRoomSelectAct);
    mouseModeToolBar->addAction(mouseMode.modeConnectionSelectAct);
    mouseModeToolBar->addAction(mouseMode.modeCreateRoomAct);
    mouseModeToolBar->addAction(mouseMode.modeCreateConnectionAct);
    mouseModeToolBar->addAction(mouseMode.modeCreateOnewayConnectionAct);
    mouseModeToolBar->addAction(mouseMode.modeInfoMarkEditAct);
    mouseModeToolBar->hide();

    groupToolBar = addToolBar(tr("Group Manager"));
    groupToolBar->setObjectName("GroupManagerToolBar");
    groupToolBar->addAction(groupMode.groupOffAct);
    groupToolBar->addAction(groupMode.groupClientAct);
    groupToolBar->addAction(groupMode.groupServerAct);
    groupToolBar->hide();

    viewToolBar = addToolBar(tr("View"));
    viewToolBar->setObjectName("ViewToolBar");
    viewToolBar->addAction(zoomInAct);
    viewToolBar->addAction(zoomOutAct);
    viewToolBar->addAction(layerUpAct);
    viewToolBar->addAction(layerDownAct);
    viewToolBar->hide();

    pathMachineToolBar = addToolBar(tr("Path Machine"));
    pathMachineToolBar->setObjectName("PathMachineToolBar");
    pathMachineToolBar->addAction(releaseAllPathsAct);
    pathMachineToolBar->addAction(forceRoomAct);
    pathMachineToolBar->hide();
    //viewToolBar->addAction(m_dockDialog->toggleViewAction());

    roomToolBar = addToolBar(tr("Rooms"));
    roomToolBar->setObjectName("RoomsToolBar");
    roomToolBar->addAction(findRoomsAct);
    roomToolBar->addAction(editRoomSelectionAct);
    roomToolBar->addAction(deleteRoomSelectionAct);
    roomToolBar->addAction(moveUpRoomSelectionAct);
    roomToolBar->addAction(moveDownRoomSelectionAct);
    roomToolBar->addAction(mergeUpRoomSelectionAct);
    roomToolBar->addAction(mergeDownRoomSelectionAct);
    roomToolBar->addAction(connectToNeighboursRoomSelectionAct);
    roomToolBar->hide();

    connectionToolBar = addToolBar(tr("Connections"));
    connectionToolBar->setObjectName("ConnectionsToolBar");
    //connectionToolBar->addAction(editConnectionSelectionAct);
    connectionToolBar->addAction(deleteConnectionSelectionAct);
    connectionToolBar->hide();

    settingsToolBar = addToolBar(tr("Preferences"));
    settingsToolBar->setObjectName("PreferencesToolBar");
    settingsToolBar->addAction(preferencesAct);
    settingsToolBar->hide();
}

void MainWindow::setupStatusBar()
{
    statusBar()->showMessage(tr("Welcome to MMapper ..."));
    statusBar()->insertPermanentWidget(0, new MumeClockWidget(m_mumeClock));
}

void MainWindow::onPreferences()
{
    ConfigDialog dialog(m_groupManager, this);
    dialog.exec();
}

void MainWindow::newRoomSelection(const RoomSelection *rs)
{
    forceRoomAct->setEnabled(false);
    m_roomSelection = rs;
    if (m_roomSelection != nullptr) {
        selectedRoomActGroup->setEnabled(true);
        if (m_roomSelection->size() == 1) {
            forceRoomAct->setEnabled(true);
        }
    } else {
        selectedRoomActGroup->setEnabled(false);
    }
}

void MainWindow::newConnectionSelection(ConnectionSelection *cs)
{
    m_connectionSelection = cs;
    selectedConnectionActGroup->setEnabled(m_connectionSelection != nullptr);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    groupOff();
    writeSettings();
    if (maybeSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::newFile()
{
    m_mapWindow->getCanvas()->clearRoomSelection();
    m_mapWindow->getCanvas()->clearConnectionSelection();

    if (maybeSave()) {
        AbstractMapStorage *storage = (AbstractMapStorage *) new MapStorage(*m_mapData, "", this);
        connect(storage,
                &AbstractMapStorage::onNewData,
                m_mapWindow->getCanvas(),
                &MapCanvas::dataLoaded);
        connect(storage, &AbstractMapStorage::log, this, &MainWindow::log);
        storage->newData();
        delete (storage);
        setCurrentFile("");
    }
}

void MainWindow::merge()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Choose map file ...",
                                                    "",
                                                    "MMapper2 (*.mm2);;MMapper (*.map)");
    if (!fileName.isEmpty()) {
        auto *file = new QFile(fileName);

        if (!file->open(QFile::ReadOnly)) {
            QMessageBox::warning(this,
                                 tr("Application"),
                                 tr("Cannot read file %1:\n%2.")
                                     .arg(fileName)
                                     .arg(file->errorString()));

            m_mapWindow->getCanvas()->setEnabled(true);
            delete file;
            return;
        }

        //MERGE
        progressDlg = new QProgressDialog(this);
        QPushButton *cb = new QPushButton("Abort ...");
        cb->setEnabled(false);
        progressDlg->setCancelButton(cb);
        progressDlg->setLabelText("Importing map...");
        progressDlg->setCancelButtonText("Abort");
        progressDlg->setMinimum(0);
        progressDlg->setMaximum(100);
        progressDlg->setValue(0);
        progressDlg->show();

        m_mapWindow->getCanvas()->clearRoomSelection();
        m_mapWindow->getCanvas()->clearConnectionSelection();

        auto real_storage = std::make_unique<MapStorage>(*m_mapData, fileName, file, this);
        auto storage = static_cast<AbstractMapStorage *>(real_storage.get());
        connect(storage,
                &AbstractMapStorage::onDataLoaded,
                m_mapWindow->getCanvas(),
                &MapCanvas::dataLoaded);
        connect(&storage->getProgressCounter(),
                &ProgressCounter::onPercentageChanged,
                this,
                &MainWindow::percentageChanged);
        connect(storage, &AbstractMapStorage::log, this, &MainWindow::log);

        disableActions(true);
        m_mapWindow->getCanvas()->hide();
        if (storage->canLoad()) {
            storage->mergeData();
        }
        m_mapWindow->getCanvas()->show();
        disableActions(false);
        //cutAct->setEnabled(false);
        //copyAct->setEnabled(false);
        //pasteAct->setEnabled(false);

        storage = nullptr;
        real_storage.reset();
        delete progressDlg;

        statusBar()->showMessage(tr("File merged"), 2000);
        delete file;
    }
}

void MainWindow::open()
{
    if (!maybeSave())
        return;

    auto &lastMapDir = Config().autoLoad.lastMapDirectory;
    const QString fileName = QFileDialog::getOpenFileName(this,
                                                          "Choose map file ...",
                                                          lastMapDir,
                                                          "MMapper2 (*.mm2);;MMapper (*.map)");
    if (!fileName.isEmpty()) {
        QFileInfo file(fileName);
        lastMapDir = file.dir().absolutePath();
        loadFile(file.absoluteFilePath());
    }
}

void MainWindow::reload()
{
    if (maybeSave()) {
        loadFile(m_mapData->getFileName());
    }
}

bool MainWindow::save()
{
    if (m_mapData->getFileName().isEmpty()) {
        return saveAs();
    }
    return saveFile(m_mapData->getFileName(), SaveMode::SAVEM_FULL, SaveFormat::SAVEF_MM2);
}

std::unique_ptr<QFileDialog> MainWindow::createDefaultSaveDialog()
{
    auto save = std::make_unique<QFileDialog>(this, "Choose map file name ...");
    save->setFileMode(QFileDialog::AnyFile);
    save->setDirectory(QDir::current());
    save->setNameFilter("MMapper2 (*.mm2)");
    save->setDefaultSuffix("mm2");
    save->setAcceptMode(QFileDialog::AcceptSave);
    return save;
}

static QStringList getSaveFileNames(std::unique_ptr<QFileDialog> &&ptr)
{
    if (const auto pSaveDialog = ptr.get()) {
        if (pSaveDialog->exec() == QDialog::Accepted)
            return pSaveDialog->selectedFiles();
        return QStringList{};
    }
    throw std::runtime_error("null pointer");
}

bool MainWindow::saveAs()
{
    const QStringList fileNames = getSaveFileNames(createDefaultSaveDialog());
    if (fileNames.isEmpty()) {
        statusBar()->showMessage(tr("No filename provided"), 2000);
        return false;
    }

    return saveFile(fileNames[0], SaveMode::SAVEM_FULL, SaveFormat::SAVEF_MM2);
}

bool MainWindow::exportBaseMap()
{
    const auto makeSaveDialog = [this]() {
        auto saveDialog = createDefaultSaveDialog();
        QFileInfo currentFile(m_mapData->getFileName());
        if (currentFile.exists()) {
            saveDialog->setDirectory(currentFile.absoluteDir());
            saveDialog->selectFile(currentFile.fileName().replace(QRegExp("\\.mm2$"), "-base.mm2"));
        }
        return saveDialog;
    };

    const auto fileNames = getSaveFileNames(makeSaveDialog());
    if (fileNames.isEmpty()) {
        statusBar()->showMessage(tr("No filename provided"), 2000);
        return false;
    }

    return saveFile(fileNames[0], SaveMode::SAVEM_BASEMAP, SaveFormat::SAVEF_MM2);
}

bool MainWindow::exportWebMap()
{
    const auto makeSaveDialog = [this]() {
        auto save = std::make_unique<QFileDialog>(this,
                                                  "Choose map file name ...",
                                                  QDir::current().absolutePath());
        save->setFileMode(QFileDialog::Directory);
        save->setOption(QFileDialog::ShowDirsOnly, true);
        save->setAcceptMode(QFileDialog::AcceptSave);

        QFileInfo currentFile(m_mapData->getFileName());
        if (currentFile.exists()) {
            save->setDirectory(currentFile.absoluteDir());
        }
        return save;
    };

    const QStringList fileNames = getSaveFileNames(makeSaveDialog());
    if (fileNames.isEmpty()) {
        statusBar()->showMessage(tr("No filename provided"), 2000);
        return false;
    }

    return saveFile(fileNames[0], SaveMode::SAVEM_BASEMAP, SaveFormat::SAVEF_WEB);
}

void MainWindow::about()
{
    AboutDialog about(this);
    about.exec();
}

bool MainWindow::maybeSave()
{
    if (m_mapData->dataChanged()) {
        int ret = QMessageBox::warning(this,
                                       tr("mmapper"),
                                       tr("The document has been modified.\n"
                                          "Do you want to save your changes?"),
                                       QMessageBox::Yes | QMessageBox::Default,
                                       QMessageBox::No,
                                       QMessageBox::Cancel | QMessageBox::Escape);
        if (ret == QMessageBox::Yes) {
            return save();
        }
        if (ret == QMessageBox::Cancel) {
            return false;
        }
    }
    return true;
}

void MainWindow::loadFile(const QString &fileName)
{
    auto *file = new QFile(fileName);
    if (!file->open(QFile::ReadOnly)) {
        QMessageBox::warning(this,
                             tr("Application"),
                             tr("Cannot read file %1:\n%2.").arg(fileName).arg(file->errorString()));

        m_mapWindow->getCanvas()->setEnabled(true);
        delete file;
        return;
    }

    //LOAD
    progressDlg = new QProgressDialog(this);
    QPushButton *cb = new QPushButton("Abort ...");
    cb->setEnabled(false);
    progressDlg->setCancelButton(cb);
    progressDlg->setLabelText("Loading map...");
    progressDlg->setCancelButtonText("Abort");
    progressDlg->setMinimum(0);
    progressDlg->setMaximum(100);
    progressDlg->setValue(0);
    progressDlg->show();

    m_mapWindow->getCanvas()->clearRoomSelection();
    m_mapWindow->getCanvas()->clearConnectionSelection();

    auto *storage = (AbstractMapStorage *) new MapStorage(*m_mapData, fileName, file, this);
    connect(storage,
            &AbstractMapStorage::onDataLoaded,
            m_mapWindow->getCanvas(),
            &MapCanvas::dataLoaded);
    connect(&storage->getProgressCounter(),
            &ProgressCounter::onPercentageChanged,
            this,
            &MainWindow::percentageChanged);
    connect(storage, &AbstractMapStorage::log, this, &MainWindow::log);

    disableActions(true);
    m_mapWindow->getCanvas()->hide();
    if (storage->canLoad()) {
        storage->loadData();
    }
    m_mapWindow->getCanvas()->show();
    disableActions(false);
    //cutAct->setEnabled(false);
    //copyAct->setEnabled(false);
    //pasteAct->setEnabled(false);

    delete (storage);
    delete progressDlg;

    setCurrentFile(fileName);
    statusBar()->showMessage(tr("File loaded"), 2000);
    delete file;
}

void MainWindow::percentageChanged(quint32 p)
{
    if (progressDlg == nullptr) {
        return;
    }
    progressDlg->setValue(p);

    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
}

bool MainWindow::saveFile(const QString &fileName, SaveMode mode, SaveFormat format)
{
    FileSaver saver;
    if (format != SaveFormat::SAVEF_WEB) { // Web uses a whole directory
        try {
            saver.open(fileName);
        } catch (std::exception &e) {
            QMessageBox::warning(nullptr,
                                 tr("Application"),
                                 tr("Cannot write file %1:\n%2.").arg(fileName).arg(e.what()));
            m_mapWindow->getCanvas()->setEnabled(true);
            return false;
        }
    }

    std::unique_ptr<AbstractMapStorage> storage;
    if (format == SaveFormat::SAVEF_WEB) {
        storage.reset(
            static_cast<AbstractMapStorage *>(new JsonMapStorage(*m_mapData, fileName, this)));
    } else {
        storage.reset(static_cast<AbstractMapStorage *>(
            new MapStorage(*m_mapData, fileName, &saver.file(), this)));
    }

    if (!storage->canSave()) {
        return false;
    }

    m_mapWindow->getCanvas()->setEnabled(false);

    //SAVE
    progressDlg = new QProgressDialog(this);
    QPushButton *cb = new QPushButton("Abort ...");
    cb->setEnabled(false);
    progressDlg->setCancelButton(cb);
    progressDlg->setLabelText("Saving map...");
    progressDlg->setCancelButtonText("Abort");
    progressDlg->setMinimum(0);
    progressDlg->setMaximum(100);
    progressDlg->setValue(0);
    progressDlg->show();

    // REVISIT: This is done enough times that it should probably be a function by itself.
    connect(&storage->getProgressCounter(),
            &ProgressCounter::onPercentageChanged,
            this,
            &MainWindow::percentageChanged);
    connect(storage.get(), &AbstractMapStorage::log, this, &MainWindow::log);

    disableActions(true);
    //m_mapWindow->getCanvas()->hide();
    const bool saveOk = storage->saveData(mode == SaveMode::SAVEM_BASEMAP);
    //m_mapWindow->getCanvas()->show();
    disableActions(false);
    //cutAct->setEnabled(false);
    //copyAct->setEnabled(false);
    //pasteAct->setEnabled(false);

    delete progressDlg;

    try {
        saver.close();
    } catch (std::exception &e) {
        QMessageBox::warning(nullptr,
                             tr("Application"),
                             tr("Cannot write file %1:\n%2.").arg(fileName).arg(e.what()));
        m_mapWindow->getCanvas()->setEnabled(true);
        return false;
    }

    if (saveOk) {
        if (mode == SaveMode::SAVEM_FULL && format == SaveFormat::SAVEF_MM2) {
            setCurrentFile(fileName);
        }
        statusBar()->showMessage(tr("File saved"), 2000);
    } else {
        QMessageBox::warning(nullptr, tr("Application"), tr("Error while saving (see log)."));
    }

    m_mapWindow->getCanvas()->setEnabled(true);

    return true;
}

void MainWindow::onFindRoom()
{
    m_findRoomsDlg->show();
}

void MainWindow::onLaunchClient()
{
    m_welcomeWidget->hide();
    m_dockWelcome->hide();

    m_client->show();
    m_client->focusWidget();
    m_client->connectToHost();
}

void MainWindow::groupManagerOff()
{
    groupMode.groupOffAct->setChecked(true);
    m_dockDialogGroup->hide();
    m_groupWidget->hide();
}

void MainWindow::groupOff()
{
    groupManagerOff();
    if (m_groupManager->getType() != GroupManagerState::Off && groupMode.groupOffAct->isChecked()) {
        emit setGroupManagerType(GroupManagerState::Off);
    }
}

void MainWindow::groupClient()
{
    if (m_groupManager->getType() != GroupManagerState::Client && groupMode.groupClientAct->isChecked()) {
        emit setGroupManagerType(GroupManagerState::Client);
        m_dockDialogGroup->show();
        m_groupWidget->show();
    }
}

void MainWindow::groupServer()
{
    if (m_groupManager->getType() != GroupManagerState::Server && groupMode.groupServerAct->isChecked()) {
        emit setGroupManagerType(GroupManagerState::Server);
        m_dockDialogGroup->show();
        m_groupWidget->show();
    }
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    QString shownName;
    if (fileName.isEmpty()) {
        shownName = "untitled.mm2";
    } else {
        shownName = strippedName(fileName);
    }

    setWindowTitle(tr("%1[*] - %2").arg(shownName).arg(tr("MMapper")));
}

QString MainWindow::strippedName(const QString &fullFileName)
{
    return QFileInfo(fullFileName).fileName();
}

void MainWindow::onLayerUp()
{
    m_mapWindow->getCanvas()->layerUp();
}

void MainWindow::onLayerDown()
{
    m_mapWindow->getCanvas()->layerDown();
}

void MainWindow::onModeConnectionSelect()
{
    m_mapWindow->getCanvas()->setCanvasMouseMode(CanvasMouseMode::SELECT_CONNECTIONS);
}

void MainWindow::onModeRoomSelect()
{
    m_mapWindow->getCanvas()->setCanvasMouseMode(CanvasMouseMode::SELECT_ROOMS);
}

void MainWindow::onModeMoveSelect()
{
    m_mapWindow->getCanvas()->setCanvasMouseMode(CanvasMouseMode::MOVE);
}

void MainWindow::onModeCreateRoomSelect()
{
    m_mapWindow->getCanvas()->setCanvasMouseMode(CanvasMouseMode::CREATE_ROOMS);
}

void MainWindow::onModeCreateConnectionSelect()
{
    m_mapWindow->getCanvas()->setCanvasMouseMode(CanvasMouseMode::CREATE_CONNECTIONS);
}

void MainWindow::onModeCreateOnewayConnectionSelect()
{
    m_mapWindow->getCanvas()->setCanvasMouseMode(CanvasMouseMode::CREATE_ONEWAY_CONNECTIONS);
}

void MainWindow::onModeInfoMarkEdit()
{
    m_mapWindow->getCanvas()->setCanvasMouseMode(CanvasMouseMode::EDIT_INFOMARKS);
}

void MainWindow::onCreateRoom()
{
    m_mapWindow->getCanvas()->createRoom();
    m_mapWindow->getCanvas()->update();
}

void MainWindow::onEditRoomSelection()
{
    if (m_roomSelection != nullptr) {
        RoomEditAttrDlg m_roomEditDialog;
        m_roomEditDialog.setRoomSelection(m_roomSelection, m_mapData, m_mapWindow->getCanvas());
        m_roomEditDialog.exec();
        m_roomEditDialog.show();
    }
}

void MainWindow::onEditConnectionSelection()
{
    if (m_connectionSelection != nullptr) {
        /*RoomConnectionsDlg connectionsDlg;
        connectionsDlg.setRoom(static_cast<Room*>(m_connectionSelection->getFirst().room),
                                        m_mapData,
                                        static_cast<Room*>(m_connectionSelection->getSecond().room),
                                        m_connectionSelection->getFirst().direction,
                                        m_connectionSelection->getSecond().direction);
        connect(&connectionsDlg, SIGNAL(connectionChanged()), m_mapWindow->getCanvas(), SLOT(update()));

        connectionsDlg.exec();
        */
    }
}

void MainWindow::onDeleteRoomSelection()
{
    if (m_roomSelection != nullptr) {
        m_mapData->execute(new GroupAction(new Remove(), m_roomSelection), m_roomSelection);
        m_mapWindow->getCanvas()->clearRoomSelection();
        m_mapWindow->getCanvas()->update();
    }
}

void MainWindow::onDeleteConnectionSelection()
{
    if (m_connectionSelection != nullptr) {
        const Room *r1 = m_connectionSelection->getFirst().room;
        ExitDirection dir1 = m_connectionSelection->getFirst().direction;
        const Room *r2 = m_connectionSelection->getSecond().room;
        ExitDirection dir2 = m_connectionSelection->getSecond().direction;

        if (r2 != nullptr) {
            const RoomSelection *tmpSel = m_mapData->select();
            m_mapData->getRoom(r1->getId(), tmpSel);
            m_mapData->getRoom(r2->getId(), tmpSel);

            m_mapWindow->getCanvas()->clearConnectionSelection();

            m_mapData->execute(new RemoveTwoWayExit(r1->getId(), r2->getId(), dir1, dir2), tmpSel);
            //m_mapData->execute(new RemoveExit(r2->getId(), r1->getId(), dir2), tmpSel);

            m_mapData->unselect(tmpSel);
        }
    }

    m_mapWindow->getCanvas()->update();
}

void MainWindow::onMoveUpRoomSelection()
{
    if (m_roomSelection == nullptr) {
        return;
    }
    Coordinate moverel(0, 0, 1);
    m_mapData->execute(new GroupAction(new MoveRelative(moverel), m_roomSelection), m_roomSelection);
    onLayerUp();
    m_mapWindow->getCanvas()->update();
}

void MainWindow::onMoveDownRoomSelection()
{
    if (m_roomSelection == nullptr) {
        return;
    }
    Coordinate moverel(0, 0, -1);
    m_mapData->execute(new GroupAction(new MoveRelative(moverel), m_roomSelection), m_roomSelection);
    onLayerDown();
    m_mapWindow->getCanvas()->update();
}

void MainWindow::onMergeUpRoomSelection()
{
    if (m_roomSelection == nullptr) {
        return;
    }
    Coordinate moverel(0, 0, 1);
    m_mapData->execute(new GroupAction(new MergeRelative(moverel), m_roomSelection),
                       m_roomSelection);
    onLayerUp();
    onModeRoomSelect();
}

void MainWindow::onMergeDownRoomSelection()
{
    if (m_roomSelection == nullptr) {
        return;
    }
    Coordinate moverel(0, 0, -1);
    m_mapData->execute(new GroupAction(new MergeRelative(moverel), m_roomSelection),
                       m_roomSelection);
    onLayerDown();
    onModeRoomSelect();
}

void MainWindow::onConnectToNeighboursRoomSelection()
{
    if (m_roomSelection == nullptr) {
        return;
    }
    m_mapData->execute(new GroupAction(new ConnectToNeighbours, m_roomSelection), m_roomSelection);
    m_mapWindow->getCanvas()->update();
}

void MainWindow::onCheckForUpdate()
{
    QDesktopServices::openUrl(QUrl("https://github.com/MUME/MMapper/releases"));
}

void MainWindow::voteForMUMEOnTMC()
{
    QDesktopServices::openUrl(QUrl(
        "http://www.mudconnect.com/cgi-bin/vote_rank.cgi?mud=MUME+-+Multi+Users+In+Middle+Earth"));
}

void MainWindow::openMumeWebsite()
{
    QDesktopServices::openUrl(QUrl("http://mume.org/"));
}

void MainWindow::openMumeForum()
{
    QDesktopServices::openUrl(QUrl("http://mume.org/forum/"));
}

void MainWindow::openMumeWiki()
{
    QDesktopServices::openUrl(QUrl("http://mume.org/wiki/"));
}

void MainWindow::openSettingUpMmapper()
{
    QDesktopServices::openUrl(QUrl("https://github.com/MUME/MMapper/wiki/Troubleshooting"));
}

void MainWindow::openNewbieHelp()
{
    QDesktopServices::openUrl(QUrl("http://mume.org/newbie.php"));
}
