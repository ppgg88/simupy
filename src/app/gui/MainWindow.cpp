#include "MainWindow.h"

#include "app/gui/style/AppIcons.h"
#include "app/gui/canvas/BlockItem.h"
#include "app/gui/panels/BlockLibraryPanel.h"
#include "app/gui/panels/ConsoleDock.h"
#include "app/gui/panels/ControlPanel.h"
#include "app/gui/dialogs/CustomBlockDialog.h"
#include "app/gui/canvas/DiagramScene.h"
#include "app/gui/canvas/DiagramView.h"
#include "app/gui/dialogs/LibraryManagerDialog.h"
#include "app/gui/panels/PropertyPanel.h"
#include "app/gui/dialogs/PythonEditor.h"
#include "app/gui/dialogs/ScopeWindow.h"
#include "app/gui/dialogs/SolverSettingsDialog.h"
#include "app/gui/style/Theme.h"
#include "app/gui/canvas/WireItem.h"
#include "model/BlockRegistry.h"
#include "io/CustomBlock.h"
#include "blocks/SubsystemBlock.h"
#include "io/ModelSerializer.h"
#include "scripting/PythonBlock.h"
#include "scripting/PythonEngine.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace simupy {
namespace {

constexpr const char* kFileFilter = "SimuPy models (*.spy);;All files (*)";

constexpr int kAutosaveIntervalMs = 30000;
constexpr const char* kAutosaveSetting = "editor/autosave";

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("SimuPy"));

    scene_ = new DiagramScene(model_, this);
    view_ = new DiagramView(this);
    view_->setScene(scene_);

    breadcrumb_ = new QWidget(this);
    breadcrumbLayout_ = new QHBoxLayout(breadcrumb_);
    breadcrumbLayout_->setContentsMargins(8, 4, 8, 4);
    breadcrumbLayout_->setSpacing(4);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(breadcrumb_);
    centralLayout->addWidget(view_, 1);
    setCentralWidget(central);

    path_.append({&model_, tr("model"), {}});

    controller_ = new SimulationController(this);

    buildActions();
    buildDocks();
    buildMenus();
    buildToolBar();
    buildStatusBar();

    connect(scene_, &DiagramScene::modelEdited, this,
            &MainWindow::onModelEdited);
    connect(scene_, &DiagramScene::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(scene_, &DiagramScene::blockEditRequested, this,
            &MainWindow::onBlockEditRequested);
    connect(scene_, &DiagramScene::statusMessage, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
            });
    connect(scene_, &DiagramScene::controlChanged, this, [this] {
        controls_->refreshValues();
        if (!controller_->isBusy()) {
            controls_->commitValues();
            setDirty(true);
        }
    });

    connect(view_, &DiagramView::zoomChanged, this, [this](qreal factor) {
        zoomLabel_->setText(tr("%1 %").arg(qRound(factor * 100)));
    });

    connect(controller_, &SimulationController::compiled, this,
            &MainWindow::onCompiled);
    connect(controller_, &SimulationController::progressed, this,
            &MainWindow::onProgressed);
    connect(controller_, &SimulationController::dataUpdated, this,
            &MainWindow::onDataUpdated);
    connect(controller_, &SimulationController::completed, this,
            &MainWindow::onCompleted);
    connect(controller_, &SimulationController::output, console_,
            &ConsoleDock::appendOutput);

    PythonEngine::instance().setOutputHandler(
        [this](const std::string& text, bool isError) {
            QMetaObject::invokeMethod(
                console_, "appendOutput", Qt::QueuedConnection,
                Q_ARG(QString, QString::fromStdString(text)),
                Q_ARG(bool, isError));
        });

    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(std::min(1500, available.width() - 80),
           std::min(950, available.height() - 80));

    setDirty(false);
    rebuildBreadcrumb();
    updateStopTimeControls();
    refreshControlPanel();
    onSelectionChanged();

    autosaveAction_->setChecked(
        QSettings().value(QLatin1String(kAutosaveSetting), false).toBool());

    console_->appendMessage(
        tr("SimuPy ready — Python %1, %2 block types available.")
            .arg(QString::fromStdString(PythonEngine::instance().version()))
            .arg(BlockRegistry::instance().all().size()));
}

MainWindow::~MainWindow() {
    if (controller_->isBusy()) {
        controller_->requestStop();
        controller_->wait(5000);
    }
    PythonEngine::instance().setOutputHandler({});
}

void MainWindow::buildActions() {
    runAction_ = new QAction(appicons::run(), tr("&Run"), this);
    runAction_->setShortcut(QKeySequence(Qt::Key_F5));
    runAction_->setStatusTip(tr("Compile and simulate the model"));
    runAction_->setToolTip(tr("Run the simulation (F5)"));
    connect(runAction_, &QAction::triggered, this, &MainWindow::runSimulation);

    stopAction_ = new QAction(appicons::stop(), tr("&Stop"), this);
    stopAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    stopAction_->setStatusTip(tr("Abandon the run in progress"));
    stopAction_->setToolTip(tr("Stop the simulation (Shift+F5)"));
    stopAction_->setEnabled(false);
    connect(stopAction_, &QAction::triggered, this,
            &MainWindow::stopSimulation);

    saveAction_ = new QAction(appicons::save(), tr("&Save"), this);
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setStatusTip(tr("Save the model"));
    saveAction_->setToolTip(tr("Save (Ctrl+S)"));
    connect(saveAction_, &QAction::triggered, this, &MainWindow::save);

    autosaveAction_ = new QAction(tr("Auto&save every 30 seconds"), this);
    autosaveAction_->setCheckable(true);
    autosaveAction_->setStatusTip(
        tr("Write the model back to its own file every 30 seconds while it "
           "has unsaved edits"));
    connect(autosaveAction_, &QAction::toggled, this,
            &MainWindow::setAutosaveEnabled);

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(kAutosaveIntervalMs);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);

    newAction_ = new QAction(appicons::newModel(), tr("&New"), this);
    newAction_->setShortcut(QKeySequence::New);
    newAction_->setStatusTip(tr("Start an empty model"));
    newAction_->setToolTip(tr("New model (Ctrl+N)"));
    connect(newAction_, &QAction::triggered, this, &MainWindow::newModel);

    openAction_ = new QAction(appicons::open(), tr("&Open…"), this);
    openAction_->setShortcut(QKeySequence::Open);
    openAction_->setStatusTip(tr("Open a model file"));
    openAction_->setToolTip(tr("Open (Ctrl+O)"));
    connect(openAction_, &QAction::triggered, this, &MainWindow::open);
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    fileMenu->addAction(newAction_);
    fileMenu->addAction(openAction_);
    fileMenu->addAction(saveAction_);

    QAction* saveAsAction =
        fileMenu->addAction(appicons::save(), tr("Save &As…"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

    fileMenu->addSeparator();
    fileMenu->addAction(autosaveAction_);

    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));

    QAction* quickAdd =
        editMenu->addAction(appicons::addBlock(), tr("&Add Block…"));
    quickAdd->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    quickAdd->setStatusTip(
        tr("Search every block type and insert one at the cursor. Typing a "
           "letter on the canvas does the same."));
    connect(quickAdd, &QAction::triggered, this,
            [this] { view_->openQuickAdd(); });

    editMenu->addSeparator();

    // Bound to the canvas rather than to the window: with a window-wide
    // shortcut, Ctrl+C in the property panel or the Python editor would copy
    // blocks instead of the text under the caret.
    auto canvasAction = [this, editMenu](const QIcon& icon, const QString& title,
                                         QKeySequence shortcut,
                                         const QString& tip, auto&& handler) {
        QAction* action = editMenu->addAction(icon, title);
        action->setShortcut(shortcut);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setStatusTip(tip);
        view_->addAction(action);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };

    canvasAction(appicons::copy(), tr("&Copy"), QKeySequence::Copy,
                 tr("Copy the selected blocks, with the wires between them."),
                 [this] { scene_->copySelection(); });

    canvasAction(appicons::cut(), tr("Cu&t"), QKeySequence::Cut,
                 tr("Copy the selected blocks and remove them."),
                 [this] { scene_->cutSelection(); });

    canvasAction(appicons::paste(), tr("&Paste"), QKeySequence::Paste,
                 tr("Paste blocks at the cursor."),
                 [this] { scene_->pasteAt(view_->insertionScenePos()); });

    canvasAction(appicons::duplicate(), tr("D&uplicate"), QKeySequence(Qt::CTRL | Qt::Key_D),
                 tr("Copy the selection alongside itself, without touching "
                    "the clipboard."),
                 [this] { scene_->duplicateSelection(); });

    editMenu->addSeparator();

    QAction* selectAll = editMenu->addAction(tr("Select &All"));
    selectAll->setShortcut(QKeySequence::SelectAll);
    connect(selectAll, &QAction::triggered, scene_, &DiagramScene::selectAll);

    QAction* deleteAction =
        editMenu->addAction(appicons::remove(), tr("&Delete"));
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, scene_,
            &DiagramScene::deleteSelection);

    QAction* mirrorAction =
        editMenu->addAction(appicons::mirror(), tr("&Mirror horizontally"));
    mirrorAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(mirrorAction, &QAction::triggered, scene_,
            &DiagramScene::mirrorSelection);

    QAction* nameSignal =
        editMenu->addAction(appicons::nameSignal(), tr("&Name Signal…"));
    nameSignal->setShortcut(QKeySequence(Qt::Key_F2));
    nameSignal->setStatusTip(
        tr("Label the selected wire. The name follows the signal into the "
           "scope legend and the CSV header."));
    connect(nameSignal, &QAction::triggered, this, [this] {
        WireItem* wire = scene_->selectedWire();
        if (!wire) {
            statusBar()->showMessage(tr("Select a single wire first."), 4000);
            return;
        }
        scene_->promptSignalName(wire);
    });

    editMenu->addSeparator();

    QAction* openSubsystem =
        editMenu->addAction(appicons::enterSubsystem(), tr("&Open Subsystem"));
    openSubsystem->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down));
    connect(openSubsystem, &QAction::triggered, this,
            &MainWindow::openSelectedSubsystem);

    QAction* leave =
        editMenu->addAction(appicons::leaveSubsystem(), tr("&Leave Subsystem"));
    leave->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up));
    connect(leave, &QAction::triggered, this, &MainWindow::leaveSubsystem);

    QMenu* simulationMenu = menuBar()->addMenu(tr("&Simulation"));
    simulationMenu->addAction(runAction_);
    simulationMenu->addAction(stopAction_);
    simulationMenu->addSeparator();

    QAction* settingsAction =
        simulationMenu->addAction(appicons::settings(), tr("&Settings…"));
    settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(settingsAction, &QAction::triggered, this,
            &MainWindow::showSolverSettings);

    QMenu* libraryMenu = menuBar()->addMenu(tr("&Library"));

    QAction* saveBlock = libraryMenu->addAction(appicons::saveAsBlock(),
                                               tr("&Save Selection as Block…"));
    saveBlock->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    saveBlock->setStatusTip(
        tr("Turn the selected Subsystem or Python block into a reusable "
           "library block."));
    connect(saveBlock, &QAction::triggered, this,
            &MainWindow::saveSelectionAsCustomBlock);

    libraryMenu->addSeparator();

    QAction* manage =
        libraryMenu->addAction(appicons::library(), tr("&Manage Libraries…"));
    connect(manage, &QAction::triggered, this, &MainWindow::showLibraryManager);

    QAction* reload = libraryMenu->addAction(tr("&Reload Libraries"));
    reload->setStatusTip(
        tr("Re-read every .spylib on the search path, after editing one by "
           "hand or dropping in a new file."));
    connect(reload, &QAction::triggered, this, [this] {
        const std::vector<std::string> problems =
            LibraryManager::instance().loadAll();
        library_->reload();
        for (const std::string& problem : problems)
            console_->appendMessage(tr("Library: %1")
                                        .arg(QString::fromStdString(problem)));
        statusLabel_->setText(tr("Reloaded block libraries"));
    });

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    QAction* zoomIn = viewMenu->addAction(appicons::zoomIn(), tr("Zoom &In"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, view_, &DiagramView::zoomIn);

    QAction* zoomOut = viewMenu->addAction(appicons::zoomOut(), tr("Zoom &Out"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, view_, &DiagramView::zoomOut);

    QAction* zoomReset = viewMenu->addAction(tr("&Actual Size"));
    zoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomReset, &QAction::triggered, view_, &DiagramView::resetZoom);

    QAction* zoomFit = viewMenu->addAction(appicons::zoomFit(), tr("&Fit to Window"));
    zoomFit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_9));
    connect(zoomFit, &QAction::triggered, view_, &DiagramView::zoomToFit);

    viewMenu->addSeparator();
    for (QDockWidget* dock : findChildren<QDockWidget*>())
        viewMenu->addAction(dock->toggleViewAction());

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* reference = helpMenu->addAction(tr("&Block reference"));
    connect(reference, &QAction::triggered, this,
            &MainWindow::showBlockReference);

    QAction* about = helpMenu->addAction(tr("&About SimuPy"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, tr("About SimuPy"),
            tr("<h3>SimuPy %3</h3>"
               "<p>Block-diagram simulation with custom blocks written in "
               "Python.</p>"
               "<p>Engine in C++ with Eigen; Python %1 embedded through "
               "pybind11; interface in Qt %2.</p>")
                .arg(QString::fromStdString(PythonEngine::instance().version()),
                     QLatin1String(qVersion()),
                     QApplication::applicationVersion()));
    });
}

void MainWindow::buildToolBar() {
    QToolBar* bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setMovable(false);
    bar->setIconSize(QSize(20, 20));
    // Icon-only for the file group, which everyone recognises; the two
    // simulation actions keep their label, because those are the ones a new
    // user is looking for and the ones whose state matters.
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    bar->addAction(newAction_);
    bar->addAction(openAction_);
    bar->addAction(saveAction_);
    bar->addSeparator();

    for (QAction* action : {runAction_, stopAction_}) {
        bar->addAction(action);
        if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(action)))
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }

    realTimeAction_ = new QAction(appicons::realTime(), tr("Real time"), this);
    realTimeAction_->setCheckable(true);
    realTimeAction_->setChecked(model_.solver().realTime);
    realTimeAction_->setStatusTip(
        tr("Hold the simulation to the wall clock instead of running flat out"));
    connect(realTimeAction_, &QAction::toggled, this, [this](bool on) {
        model_.solver().realTime = on;
        setDirty(true);
        updateRealTimeAction();
    });
    bar->addAction(realTimeAction_);
    if (auto* button =
            qobject_cast<QToolButton*>(bar->widgetForAction(realTimeAction_)))
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    updateRealTimeAction();

    bar->addSeparator();

    auto* stopTimeLabel = new QLabel(tr(" Stop time "), bar);
    bar->addWidget(stopTimeLabel);

    unboundedAction_ = new QAction(QStringLiteral("∞"), this);
    unboundedAction_->setCheckable(true);
    unboundedAction_->setChecked(model_.solver().unbounded);
    unboundedAction_->setStatusTip(
        tr("Run until Stop is pressed, ignoring the stop time"));
    connect(unboundedAction_, &QAction::toggled, this, [this](bool on) {
        model_.solver().unbounded = on;
        setDirty(true);
        updateStopTimeControls();
    });
    bar->addAction(unboundedAction_);

    auto* stopTimeField = new QLineEdit(
        QString::number(model_.solver().stopTime, 'g', 6), bar);
    stopTimeField->setMaximumWidth(80);
    stopTimeField->setAlignment(Qt::AlignRight);
    stopTimeField->setToolTip(tr("How long to simulate, in seconds"));
    bar->addWidget(stopTimeField);
    connect(stopTimeField, &QLineEdit::editingFinished, this,
            [this, stopTimeField] {
                bool ok = false;
                const double value = stopTimeField->text().toDouble(&ok);
                if (ok && value > model_.solver().startTime) {
                    model_.solver().stopTime = value;
                    setDirty(true);
                } else {
                    stopTimeField->setText(
                        QString::number(model_.solver().stopTime, 'g', 6));
                }
            });
    stopTimeField_ = stopTimeField;

    QAction* settings = bar->addAction(appicons::settings(), tr("Settings…"));
    settings->setToolTip(tr("Solver settings (Ctrl+E)"));
    connect(settings, &QAction::triggered, this,
            &MainWindow::showSolverSettings);

    bar->addSeparator();

    QAction* addBlock = bar->addAction(appicons::addBlock(), tr("Add block"));
    addBlock->setToolTip(
        tr("Search the block library and insert one (Ctrl+Space).\n"
           "Typing a letter on the canvas does the same."));
    connect(addBlock, &QAction::triggered, this,
            [this] { view_->openQuickAdd(); });

    QAction* libraries = bar->addAction(appicons::library(), tr("Libraries"));
    libraries->setToolTip(tr("Install, export and edit block libraries"));
    connect(libraries, &QAction::triggered, this,
            &MainWindow::showLibraryManager);

    auto* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    QAction* zoomOut = bar->addAction(appicons::zoomOut(), tr("Zoom out"));
    zoomOut->setToolTip(tr("Zoom out (Ctrl+-)"));
    connect(zoomOut, &QAction::triggered, view_, &DiagramView::zoomOut);

    QAction* zoomIn = bar->addAction(appicons::zoomIn(), tr("Zoom in"));
    zoomIn->setToolTip(tr("Zoom in (Ctrl++)"));
    connect(zoomIn, &QAction::triggered, view_, &DiagramView::zoomIn);

    QAction* zoomFit = bar->addAction(appicons::zoomFit(), tr("Fit"));
    zoomFit->setToolTip(tr("Fit the whole diagram in the window (Ctrl+9)"));
    connect(zoomFit, &QAction::triggered, view_, &DiagramView::zoomToFit);
}

void MainWindow::buildDocks() {
    auto* libraryDock = new QDockWidget(tr("Block Library"), this);
    libraryDock->setObjectName(QStringLiteral("libraryDock"));
    library_ = new BlockLibraryPanel(libraryDock);
    libraryDock->setWidget(library_);
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);

    connect(library_, &BlockLibraryPanel::blockActivated, this,
            [this](const QString& typeName) {
                const QPointF centre =
                    view_->mapToScene(view_->viewport()->rect().center());
                scene_->addBlock(typeName, centre);
            });
    connect(library_, &BlockLibraryPanel::librariesRequested, this,
            &MainWindow::showLibraryManager);
    connect(library_, &BlockLibraryPanel::customBlockEditRequested, this,
            &MainWindow::editCustomBlock);

    controlsDock_ = new QDockWidget(tr("Controls"), this);
    controlsDock_->setObjectName(QStringLiteral("controlsDock"));
    controls_ = new ControlPanel(controlsDock_);
    controlsDock_->setWidget(controls_);
    addDockWidget(Qt::RightDockWidgetArea, controlsDock_);
    controls_->setMinimumHeight(180);
    connect(controls_, &ControlPanel::controlChanged, this, [this] {
        // The block draws its live value, so it has to be repainted too —
        // otherwise the canvas and the dock disagree about the same number.
        scene_->update();
        if (!controller_->isBusy()) setDirty(true);
    });

    auto* propertiesDock = new QDockWidget(tr("Properties"), this);
    propertiesDock->setObjectName(QStringLiteral("propertiesDock"));
    properties_ = new PropertyPanel(propertiesDock);
    propertiesDock->setWidget(properties_);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    connect(properties_, &PropertyPanel::blockModified, this,
            [this](const QString& blockId) {
                scene_->refreshBlock(blockId);
                setDirty(true);
                refreshControlPanel();
            });
    connect(properties_, &PropertyPanel::codeEditRequested, this,
            &MainWindow::editPythonSource);

    auto* consoleDock = new QDockWidget(tr("Console"), this);
    consoleDock_ = consoleDock;
    consoleDock->setObjectName(QStringLiteral("consoleDock"));
    console_ = new ConsoleDock(consoleDock);
    consoleDock->setWidget(console_);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
    resizeDocks({consoleDock}, {170}, Qt::Vertical);
}

void MainWindow::buildStatusBar() {
    statusLabel_ = new QLabel(tr("Ready"), this);
    timeLabel_ = new QLabel(this);
    zoomLabel_ = new QLabel(QStringLiteral("100 %"), this);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 1000);
    progress_->setMaximumWidth(180);
    progress_->setTextVisible(false);
    progress_->hide();

    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(timeLabel_);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(zoomLabel_);
}

void MainWindow::setCurrentFile(const QString& path) {
    currentFile_ = path;
    if (!path.isEmpty())
        model_.setName(QFileInfo(path).completeBaseName().toStdString());
    updateWindowTitle();
}

void MainWindow::setDirty(bool dirty) {
    dirty_ = dirty;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    const QString name = currentFile_.isEmpty()
                             ? tr("untitled")
                             : QFileInfo(currentFile_).fileName();
    setWindowTitle(tr("%1%2 — SimuPy").arg(name, dirty_ ? QStringLiteral("*")
                                                        : QString()));
    if (saveAction_) saveAction_->setEnabled(dirty_ || currentFile_.isEmpty());
}

bool MainWindow::confirmDiscardChanges() {
    if (!dirty_) return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Unsaved changes"),
        tr("“%1” has unsaved changes.")
            .arg(currentFile_.isEmpty() ? tr("untitled")
                                        : QFileInfo(currentFile_).fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (answer == QMessageBox::Save) return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::newModel() {
    if (controller_->isBusy()) return;
    if (!confirmDiscardChanges()) return;

    closeAllScopes();
    lastLog_.reset();

    model_.clear();
    path_ = {{&model_, tr("model"), {}}};
    scene_->setModel(model_);
    rebuildBreadcrumb();
    properties_->setMaskParameters({});
    properties_->setBlock(nullptr, nullptr);
    updateRealTimeAction();
    updateStopTimeControls();
    refreshControlPanel();
    setCurrentFile({});
    setDirty(false);
    statusLabel_->setText(tr("New model"));
}

void MainWindow::open() {
    if (controller_->isBusy()) return;
    if (!confirmDiscardChanges()) return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open model"), {}, tr(kFileFilter));
    if (path.isEmpty()) return;
    openFile(path);
}

bool MainWindow::openFile(const QString& path) {
    LoadReport report;
    try {
        ModelSerializer::load(model_, path.toStdString(), &report);
    } catch (const ModelError& error) {
        QMessageBox::critical(this, tr("Could not open model"),
                              QString::fromStdString(error.what()));
        return false;
    }

    closeAllScopes();
    lastLog_.reset();

    path_ = {{&model_, tr("model"), {}}};
    scene_->setModel(model_);
    rebuildBreadcrumb();
    properties_->setMaskParameters({});
    properties_->setBlock(nullptr, nullptr);
    updateRealTimeAction();
    updateStopTimeControls();
    refreshControlPanel();
    setCurrentFile(path);
    setDirty(false);
    view_->zoomToFit();

    PythonEngine::instance().addSearchPath(
        QFileInfo(path).absolutePath().toStdString());

    if (!report.missingTypes.empty()) {
        QStringList names;
        for (const std::string& type : report.missingTypes)
            names << QString::fromStdString(type);

        QStringList sources;
        for (const LoadReport::Requirement& requirement : report.requires_)
            sources << QString::fromStdString(requirement.library);

        console_->appendMessage(
            tr("%1 uses block types this installation does not have: %2. "
               "They still work — a saved model carries its diagrams and "
               "Python sources — but they show generic artwork and their "
               "parameters are unlabelled. %3")
                .arg(QFileInfo(path).fileName(),
                     names.join(QStringLiteral(", ")),
                     sources.isEmpty()
                         ? tr("No library was recorded for them.")
                         : tr("Install the '%1' library to get them back.")
                               .arg(sources.join(QStringLiteral("', '")))));
    }

    statusLabel_->setText(tr("Opened %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool MainWindow::writeModel(const QString& path, QString* error) {
    if (controls_ && !controller_->isBusy()) controls_->commitValues();

    try {
        ModelSerializer::save(model_, path.toStdString());
    } catch (const ModelError& failure) {
        if (error) *error = QString::fromStdString(failure.what());
        return false;
    }
    setDirty(false);
    return true;
}

bool MainWindow::save() {
    if (currentFile_.isEmpty()) return saveAs();

    QString error;
    if (!writeModel(currentFile_, &error)) {
        QMessageBox::critical(this, tr("Could not save"), error);
        return false;
    }
    autosaveError_.clear();
    statusLabel_->setText(tr("Saved %1").arg(QFileInfo(currentFile_).fileName()));
    return true;
}

void MainWindow::setAutosaveEnabled(bool enabled) {
    // The menu entry is the switch, so keep the two in step whichever side
    // was flipped; setChecked comes back through here once and then stops.
    if (autosaveAction_ && autosaveAction_->isChecked() != enabled) {
        autosaveAction_->setChecked(enabled);
        return;
    }

    if (enabled)
        autosaveTimer_->start();
    else
        autosaveTimer_->stop();

    QSettings().setValue(QLatin1String(kAutosaveSetting), enabled);

    if (enabled && currentFile_.isEmpty() && statusLabel_) {
        statusLabel_->setText(
            tr("Autosave on — it starts once the model has a file"));
    }
}

bool MainWindow::isAutosaveEnabled() const {
    return autosaveTimer_ != nullptr && autosaveTimer_->isActive();
}

void MainWindow::autosave() {
    // Nothing to write, nowhere to write it — autosave never opens a Save As
    // dialog — or a run is reading the model. Try again on the next tick.
    if (!dirty_ || currentFile_.isEmpty() || controller_->isBusy()) return;

    QString error;
    if (!writeModel(currentFile_, &error)) {
        // A modal box every 30 seconds would be unusable, so report the
        // failure to the console once and leave the edits in memory: the
        // window still shows them as unsaved.
        if (error != autosaveError_) {
            autosaveError_ = error;
            console_->appendMessage(tr("Autosave failed — %1").arg(error));
        }
        return;
    }

    autosaveError_.clear();
    statusLabel_->setText(
        tr("Autosaved %1 at %2")
            .arg(QFileInfo(currentFile_).fileName(),
                 QTime::currentTime().toString(QStringLiteral("HH:mm"))));
}

bool MainWindow::saveAs() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save model"),
        currentFile_.isEmpty() ? QStringLiteral("untitled.spy") : currentFile_,
        tr(kFileFilter));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QLatin1String(".spy"))) path += QLatin1String(".spy");

    setCurrentFile(path);
    return save();
}

void MainWindow::reportLibraryProblem(const QString& message) {
    console_->appendMessage(tr("Block library skipped — %1").arg(message));
}

void MainWindow::saveSelectionAsCustomBlock() {
    const QList<BlockItem*> blocks = scene_->selectedBlocks();
    if (blocks.size() != 1) {
        QMessageBox::information(
            this, tr("Save as Custom Block"),
            tr("Select the single Subsystem or Python block to save.\n\n"
               "To turn part of a diagram into a block, drop a Subsystem, "
               "build inside it, then save that."));
        return;
    }

    Block* block = blocks.first()->block();

    CustomBlockDef def;
    try {
        captureBlockDefinition(*block, def);
    } catch (const ModelError& error) {
        QMessageBox::information(this, tr("Save as Custom Block"),
                                 QString::fromStdString(error.what()));
        return;
    }

    def.displayName = block->name();
    def.name = QString::fromStdString(block->name())
                   .remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")))
                   .toStdString();
    const BlockGeometry& geometry = path_.last().model->geometry(block->id());
    def.defaultWidth = geometry.width;
    def.defaultHeight = geometry.height;

    CustomBlockDialog dialog(std::move(def), this);
    if (dialog.exec() != QDialog::Accepted) return;

    LibraryManager& manager = LibraryManager::instance();
    const std::string libraryName = dialog.targetLibrary().toStdString();

    try {
        CustomLibrary* library = manager.library(libraryName);
        if (!library) library = &manager.create(libraryName);
        manager.addBlock(*library, dialog.definition());
    } catch (const ModelError& error) {
        QMessageBox::critical(this, tr("Save as Custom Block"),
                              QString::fromStdString(error.what()));
        return;
    }

    library_->reload();
    statusLabel_->setText(tr("Saved '%1' to the '%2' library")
                              .arg(QString::fromStdString(dialog.definition().name),
                                   dialog.targetLibrary()));
    console_->appendMessage(
        tr("'%1' is now in the palette. Send %2 to share it.")
            .arg(QString::fromStdString(dialog.definition().displayName),
                 QString::fromStdString(
                     manager.library(libraryName)->path)));
}

void MainWindow::editCustomBlock(const QString& typeName) {
    LibraryManager& manager = LibraryManager::instance();
    const CustomBlockDef* stored = manager.definition(typeName.toStdString());
    if (!stored) return;

    CustomLibrary* library = manager.library(stored->libraryName);
    if (!library) return;

    CustomBlockDialog dialog(*stored, this);
    dialog.setWindowTitle(tr("Edit Custom Block"));
    dialog.pinLibrary(QString::fromStdString(library->name));
    if (dialog.exec() != QDialog::Accepted) return;

    const std::string previousName = stored->name;
    try {
        if (dialog.definition().name != previousName)
            manager.removeBlock(*library, previousName);
        manager.addBlock(*library, dialog.definition());
    } catch (const ModelError& error) {
        QMessageBox::critical(this, tr("Edit Custom Block"),
                              QString::fromStdString(error.what()));
    }

    library_->reload();
    scene_->rebuild();
}

void MainWindow::showLibraryManager() {
    LibraryManagerDialog dialog(this);
    connect(&dialog, &LibraryManagerDialog::librariesChanged, this, [this] {
        library_->reload();
        scene_->rebuild();
    });
    dialog.exec();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (controller_->isBusy()) {
        controller_->requestStop();
        controller_->wait(3000);
    }
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    closeAllScopes();
    event->accept();
}

void MainWindow::onModelEdited() {
    setDirty(true);
    if (!controller_->isBusy()) refreshControlPanel();
}

void MainWindow::onSelectionChanged() {
    const QList<BlockItem*> blocks = scene_->selectedBlocks();
    if (blocks.size() == 1)
        properties_->setBlock(&model_, blocks.first()->block());
    else
        properties_->setBlock(nullptr, nullptr);
}

QString MainWindow::currentPath() const {
    QStringList parts;
    for (const Level& level : path_) parts << level.name;
    return parts.join(QStringLiteral(" / "));
}

void MainWindow::rebuildBreadcrumb() {
    QLayoutItem* child = nullptr;
    while ((child = breadcrumbLayout_->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    for (int i = 0; i < path_.size(); ++i) {
        if (i > 0) breadcrumbLayout_->addWidget(new QLabel(QStringLiteral("›")));

        auto* button = new QToolButton;
        button->setText(path_[i].name);
        button->setAutoRaise(true);
        button->setEnabled(i != path_.size() - 1);
        connect(button, &QToolButton::clicked, this,
                [this, i] { navigateTo(i + 1); });
        breadcrumbLayout_->addWidget(button);
    }
    breadcrumbLayout_->addStretch(1);

    breadcrumb_->setVisible(insideSubsystem());
}

void MainWindow::navigateTo(int depth) {
    if (depth < 1 || depth > path_.size()) return;
    while (path_.size() > depth) path_.removeLast();

    scene_->setModel(*path_.last().model);
    properties_->setMaskParameters(path_.last().maskParameters);
    properties_->setBlock(nullptr, nullptr);
    rebuildBreadcrumb();
    view_->zoomToFit();
}

void MainWindow::openSelectedSubsystem() {
    const QList<BlockItem*> blocks = scene_->selectedBlocks();
    if (blocks.size() != 1) {
        statusBar()->showMessage(tr("Select a single Subsystem block first."),
                                 4000);
        return;
    }
    if (!isSubsystem(blocks.first()->block())) {
        statusBar()->showMessage(tr("'%1' is not a subsystem.")
                                     .arg(QString::fromStdString(
                                         blocks.first()->block()->name())),
                                 4000);
        return;
    }
    enterSubsystem(blocks.first()->block());
}

void MainWindow::leaveSubsystem() {
    if (insideSubsystem()) navigateTo(path_.size() - 1);
}

void MainWindow::enterSubsystem(Block* subsystem) {
    auto* typed = dynamic_cast<SubsystemBlock*>(subsystem);
    if (!typed) return;

    QStringList mask;
    if (const BlockType* type =
            BlockRegistry::instance().find(subsystem->typeName()))
        for (const ParamSpec& spec : type->params)
            mask << QString::fromStdString(spec.name);

    path_.append({&typed->contents(),
                  QString::fromStdString(subsystem->name()), mask});
    scene_->setModel(typed->contents());
    properties_->setMaskParameters(mask);
    properties_->setBlock(nullptr, nullptr);
    rebuildBreadcrumb();
    view_->zoomToFit();
    statusLabel_->setText(tr("Inside %1").arg(path_.last().name));
}

void MainWindow::onBlockEditRequested(BlockItem* item) {
    if (!item) return;
    Block* block = item->block();

    if (isSubsystem(block)) {
        enterSubsystem(block);
        return;
    }
    if (block->typeName() == "Scope") {
        QString path = QString::fromStdString(block->name());
        for (int i = 1; i < path_.size(); ++i)
            path = path_[i].name + QLatin1Char('/') + path;
        openScope(path, QString::fromStdString(block->name()), block->params());
        return;
    }
    if (dynamic_cast<PythonBlock*>(block)) {
        editPythonSource(item->blockId(), QStringLiteral("code"));
        return;
    }

    properties_->setBlock(&model_, block);
    if (auto* dock = qobject_cast<QDockWidget*>(properties_->parentWidget())) {
        dock->show();
        dock->raise();
    }
}

void MainWindow::editPythonSource(const QString& blockId,
                                  const QString& paramName) {
    Block* block = model_.block(blockId.toStdString());
    if (!block) return;

    PythonEditor editor(this);
    editor.setSubject(QString::fromStdString(block->name()));
    editor.setSource(
        QString::fromStdString(block->params().text(paramName.toStdString())));

    const bool isSource = paramName == QLatin1String("code");
    if (isSource) {
        editor.setValidator([block](const QString& source) -> QString {
            const ParamValue original = block->params().all().count("code")
                                            ? block->params().all().at("code")
                                            : ParamValue(std::string());
            block->params().set("code", source.toStdString());

            QString problem;
            if (auto* pythonBlock = dynamic_cast<PythonBlock*>(block))
                problem = QString::fromStdString(pythonBlock->validate());
            else
                problem = QString::fromStdString(
                    PythonEngine::instance().checkSyntax(source.toStdString(),
                                                         "<block>"));

            block->params().set("code", original);
            return problem;
        });
    } else {
        editor.setValidator([](const QString& source) -> QString {
            return QString::fromStdString(PythonEngine::instance().checkSyntax(
                source.toStdString(), "<parameters>"));
        });
    }

    if (editor.exec() != QDialog::Accepted) return;

    block->params().set(paramName.toStdString(),
                        editor.source().toStdString());
    scene_->refreshBlock(blockId);
    properties_->refresh();
    setDirty(true);

    if (isSource) {
        if (auto* pythonBlock = dynamic_cast<PythonBlock*>(block)) {
            const QString problem =
                QString::fromStdString(pythonBlock->validate());
            scene_->setBlockProblem(blockId, problem);
            if (!problem.isEmpty()) console_->appendError(problem);
        }
    }
}

void MainWindow::closeAllScopes() {
    for (const QPointer<QDockWidget>& dock : std::as_const(scopeDocks_))
        if (dock) delete dock;
    scopeDocks_.clear();
    scopes_.clear();
}

void MainWindow::openScope(const QString& path, const QString& title,
                           const Parameters& params) {
    QPointer<ScopeWindow>& slot = scopes_[path];

    if (!slot) {
        slot = new ScopeWindow(path, title);
        slot->setYRange(params.boolean("autoscale", true),
                        params.real("yMin", -1.0), params.real("yMax", 1.0));

        // A QDockWidget rather than a bare window: it gives floating, docking
        // and tabbing with the Console for the price of one container, and it
        // lets the plot be dragged into place by hand as well as by button.
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(QStringLiteral("scope:") + path);
        dock->setWidget(slot);
        dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea |
                              Qt::LeftDockWidgetArea | Qt::TopDockWidgetArea);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        if (consoleDock_) tabifyDockWidget(consoleDock_, dock);
        dock->setFloating(true);
        scopeDocks_[path] = dock;

        connect(slot, &ScopeWindow::dockRequested, this,
                [this, path](bool docked) { setScopeDocked(path, docked); });
        connect(slot, &ScopeWindow::alwaysOnTopRequested, this,
                [this, path](bool onTop) { setScopeAlwaysOnTop(path, onTop); });

        const QRect available =
            QGuiApplication::primaryScreen()->availableGeometry();
        const int step = 32 * (scopes_.size() - 1);
        QPoint corner(frameGeometry().right() - dock->width() / 2 + step,
                      frameGeometry().top() + 60 + step);
        corner.setX(std::min(corner.x(), available.right() - dock->width()));
        corner.setY(std::min(corner.y(), available.bottom() - dock->height()));
        dock->move(std::max(corner.x(), available.left()),
                   std::max(corner.y(), available.top()));

        slot->setPlacement(false, false);
        if (lastLog_) slot->setLog(lastLog_);
    }

    QDockWidget* dock = scopeDocks_.value(path);
    if (!dock) return;

    dock->show();
    dock->raise();
    if (dock->isFloating()) dock->activateWindow();
}

void MainWindow::setScopeDocked(const QString& path, bool docked) {
    QDockWidget* dock = scopeDocks_.value(path);
    ScopeWindow* scope = scopes_.value(path);
    if (!dock || !scope) return;

    if (docked) {
        dock->setWindowFlag(Qt::WindowStaysOnTopHint, false);
        dock->setFloating(false);
        if (consoleDock_) {
            tabifyDockWidget(consoleDock_, dock);
            dock->raise();  // bring the new tab to the front
        }
    } else {
        dock->setFloating(true);
        dock->activateWindow();
    }

    scope->setPlacement(docked, false);
}

void MainWindow::setScopeAlwaysOnTop(const QString& path, bool onTop) {
    QDockWidget* dock = scopeDocks_.value(path);
    ScopeWindow* scope = scopes_.value(path);
    if (!dock || !scope) return;

    if (!dock->isFloating()) dock->setFloating(true);

    const QRect geometry = dock->geometry();
    dock->setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
    dock->setGeometry(geometry);
    dock->show();
    dock->raise();

    scope->setPlacement(false, onTop);
}

void MainWindow::openScopesForRun() {
    struct Walker {
        MainWindow* window;
        void operator()(Model& model, const QString& prefix) {
            for (const BlockPtr& block : model.blocks()) {
                const QString name = QString::fromStdString(block->name());
                if (auto* subsystem = dynamic_cast<SubsystemBlock*>(block.get())) {
                    (*this)(subsystem->contents(), prefix + name + QLatin1Char('/'));
                } else if (block->typeName() == "Scope") {
                    window->openScope(prefix + name, name, block->params());
                }
            }
        }
    };
    Walker{this}(model_, {});
}

void MainWindow::refreshControlPanel() {
    controls_->setModel(&model_);
    if (controlsDock_) controlsDock_->setVisible(!controls_->isEmpty());
}

void MainWindow::updateStopTimeControls() {
    const bool unbounded = model_.solver().unbounded;

    if (unboundedAction_) {
        unboundedAction_->setChecked(unbounded);
        unboundedAction_->setToolTip(
            unbounded ? tr("Running until stopped. Click to use the stop time "
                           "again.")
                      : tr("Click to run until Stop is pressed, with no end "
                           "time."));
    }
    if (stopTimeField_) {
        stopTimeField_->setEnabled(!unbounded);
        stopTimeField_->setText(
            unbounded ? QStringLiteral("∞")
                      : QString::number(model_.solver().stopTime, 'g', 6));
    }
}

void MainWindow::updateRealTimeAction() {
    if (!realTimeAction_) return;

    const SolverSettings& solver = model_.solver();
    const double factor = solver.realTimeFactor;
    const QString speed =
        factor == 1.0 ? tr("real time") : tr("%1x real time").arg(factor, 0, 'g', 3);

    realTimeAction_->setChecked(solver.realTime);
    realTimeAction_->setText(
        solver.realTime ? tr("%1x").arg(factor, 0, 'g', 3) : QString());
    realTimeAction_->setToolTip(
        solver.realTime
            ? tr("Running at %1. Click to run flat out instead.").arg(speed)
            : tr("Running flat out. Click to pace the run at %1.").arg(speed));
}

void MainWindow::setEditingEnabled(bool enabled) {
    scene_->setInteractionLocked(!enabled);
    library_->setEnabled(enabled);
    properties_->setEnabled(enabled);
    runAction_->setEnabled(enabled);
    stopAction_->setEnabled(!enabled);
    if (realTimeAction_) realTimeAction_->setEnabled(enabled);
    if (unboundedAction_) unboundedAction_->setEnabled(enabled);

    // Deliberately *not* disabled: driving the model while it runs is what
    // the Controls dock is for, and it is the one panel that stays live.
    if (controls_) controls_->setEnabled(true);
}

void MainWindow::startSimulation() { runSimulation(); }

void MainWindow::runSimulation() {
    if (controller_->isBusy()) return;

    if (model_.blocks().empty()) {
        statusBar()->showMessage(
            tr("The model is empty — drag a block from the library first."),
            5000);
        return;
    }

    scene_->clearBlockProblems();
    scene_->clearSignalWidths();

    openScopesForRun();

    setEditingEnabled(false);

    if (model_.solver().unbounded) {
        progress_->setRange(0, 0);
    } else {
        progress_->setRange(0, 1000);
        progress_->setValue(0);
    }
    progress_->show();
    statusLabel_->setText(model_.solver().unbounded
                              ? tr("Running — press Stop to finish")
                              : tr("Running…"));
    console_->appendMessage(tr("--- run started ---"));

    controller_->start(model_);
}

void MainWindow::stopSimulation() {
    if (!controller_->isBusy()) return;
    controller_->requestStop();
    statusLabel_->setText(tr("Stopping…"));
}

void MainWindow::onCompiled(CompiledModelPtr compiled) {
    if (compiled) scene_->applySignalWidths(*compiled);
}

void MainWindow::onProgressed(double time, double fraction) {
    if (!model_.solver().unbounded)
        progress_->setValue(static_cast<int>(fraction * 1000));
    timeLabel_->setText(tr("t = %1 s").arg(time, 0, 'f', 4));
}

void MainWindow::onDataUpdated(SignalLogPtr log) {
    lastLog_ = log;
    for (const QPointer<ScopeWindow>& scope : std::as_const(scopes_))
        if (scope) scope->setLog(log);
}

void MainWindow::onCompleted(bool ok, QString error, QString summary) {
    setEditingEnabled(true);
    progress_->hide();
    // Back to a determinate bar, or the next bounded run would still show a
    // busy indicator.
    progress_->setRange(0, 1000);

    if (controls_) {
        if (controls_->commitValues()) setDirty(true);
        controls_->refreshValues();
        properties_->refresh();
    }

    if (ok) {
        statusLabel_->setText(summary);
        console_->appendMessage(summary);
        return;
    }

    statusLabel_->setText(tr("Simulation failed"));
    console_->appendError(error);

    for (const BlockPtr& block : model_.blocks()) {
        const QString name = QString::fromStdString(block->name());
        if (error.contains(QStringLiteral("'%1'").arg(name)))
            scene_->setBlockProblem(QString::fromStdString(block->id()), error);
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(tr("Simulation failed"));
    box.setText(error.section(QLatin1Char('\n'), 0, 0));
    if (error.contains(QLatin1Char('\n'))) box.setDetailedText(error);
    box.exec();
}

void MainWindow::showSolverSettings() {
    if (controller_->isBusy()) return;
    SolverSettingsDialog dialog(model_, this);
    if (dialog.exec() != QDialog::Accepted) return;

    setDirty(true);
    updateStopTimeControls();
    updateRealTimeAction();
}

void MainWindow::showBlockReference() {
    QString html = QStringLiteral("<h2>Block reference</h2>");
    const BlockRegistry& registry = BlockRegistry::instance();

    for (const std::string& category : registry.categories()) {
        html += QStringLiteral("<h3>%1</h3><table cellpadding='4'>")
                    .arg(QString::fromStdString(category));
        for (const BlockType* type : registry.inCategory(category)) {
            html += QStringLiteral(
                        "<tr><td valign='top'><b>%1</b></td><td>%2")
                        .arg(QString::fromStdString(type->displayName),
                             QString::fromStdString(type->description));
            if (!type->params.empty()) {
                html += QStringLiteral("<br><i>");
                QStringList names;
                for (const ParamSpec& param : type->params)
                    names << QString::fromStdString(param.label);
                html += names.join(QStringLiteral(" · ")) +
                        QStringLiteral("</i>");
            }
            html += QStringLiteral("</td></tr>");
        }
        html += QStringLiteral("</table>");
    }

    auto* window = new QDialog(this);
    window->setWindowTitle(tr("Block reference"));
    window->resize(760, 620);
    window->setAttribute(Qt::WA_DeleteOnClose);

    auto* browser = new QTextBrowser(window);
    browser->setHtml(html);

    auto* layout = new QVBoxLayout(window);
    layout->addWidget(browser);
    window->show();
}

}  // namespace simupy
