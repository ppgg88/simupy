#pragma once

#include "SimulationController.h"
#include "app/gui/UndoHistory.h"
#include "model/Model.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QAction;
class QDockWidget;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QTimer;
QT_END_NAMESPACE

namespace simupy {

class BlockItem;
class BlockLibraryPanel;
class ConsoleDock;
class ControlPanel;
class DiagramScene;
class DiagramView;
class PropertyPanel;
class ScopeWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);

    void startSimulation();

    DiagramScene* scene() const { return scene_; }

    QString currentPath() const;

    void refreshControlPanel();

    SignalLogPtr lastLog() const { return lastLog_; }

    bool isRunning() const { return controller_->isBusy(); }

    void reportLibraryProblem(const QString& message);

    static QString aboutHtml();

    void setAutosaveEnabled(bool enabled);
    bool isAutosaveEnabled() const;

public slots:
    void openSelectedSubsystem();
    void leaveSubsystem();

public:

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newModel();
    void open();
    bool save();
    bool saveAs();
    void autosave();

    void saveSelectionAsCustomBlock();
    void editCustomBlock(const QString& typeName);
    void showLibraryManager();

    void runSimulation();
    void stopSimulation();
    void showSolverSettings();

    void onBlockEditRequested(BlockItem* item);
    void onSelectionChanged();
    void onModelEdited();

    void onCompiled(CompiledModelPtr compiled);
    void onProgressed(double time, double fraction);
    void onDataUpdated(SignalLogPtr log);
    void onCompleted(bool ok, QString error, QString summary);

private:
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildDocks();
    void buildStatusBar();

    bool writeModel(const QString& path, QString* error);
    void setCurrentFile(const QString& path);
    void setDirty(bool dirty);
    bool confirmDiscardChanges();
    void updateWindowTitle();
    void setEditingEnabled(bool enabled);
    void updateRealTimeAction();
    void updateStopTimeControls();

    void openScope(const QString& path, const QString& title,
                   const Parameters& params);
    void openScopesForRun();
    Block* scopeBlockAt(const QString& path);
    void closeAllScopes();
    void setScopeDocked(const QString& path, bool docked);
    void setScopeAlwaysOnTop(const QString& path, bool onTop);
    void editPythonSource(const QString& blockId, const QString& paramName);
    void showBlockReference();
    void showAbout();

    void enterSubsystem(Block* subsystem);
    void navigateTo(int depth);
    void rebuildBreadcrumb();
    bool insideSubsystem() const { return path_.size() > 1; }

    bool modelIsTrusted() const;
    void setModelTrusted(bool trusted);
    void updateTrustAction();

    void undo();
    void redo();
    void seedHistory();
    void restoreState(const std::string& state);
    void updateUndoActions();
    QStringList openSubsystemNames() const;
    void reopenSubsystems(const QStringList& names);

    Model model_;

    struct Level {
        Model* model = nullptr;
        QString name;
        QStringList maskParameters;
    };
    QList<Level> path_;
    QWidget* breadcrumb_ = nullptr;
    QHBoxLayout* breadcrumbLayout_ = nullptr;
    DiagramScene* scene_ = nullptr;
    DiagramView* view_ = nullptr;
    BlockLibraryPanel* library_ = nullptr;
    PropertyPanel* properties_ = nullptr;
    ConsoleDock* console_ = nullptr;
    QDockWidget* consoleDock_ = nullptr;
    ControlPanel* controls_ = nullptr;
    QDockWidget* controlsDock_ = nullptr;
    SimulationController* controller_ = nullptr;

    QHash<QString, QPointer<ScopeWindow>> scopes_;
    QHash<QString, QPointer<QDockWidget>> scopeDocks_;
    SignalLogPtr lastLog_;

    QString currentFile_;
    bool dirty_ = false;

    UndoHistory history_;
    /// Set while restoring, so its own edits do not record fresh history.
    bool restoring_ = false;

    QTimer* autosaveTimer_ = nullptr;
    QString autosaveError_;

    QAction* runAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* autosaveAction_ = nullptr;
    QAction* newAction_ = nullptr;
    QAction* realTimeAction_ = nullptr;
    QAction* unboundedAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* trustAction_ = nullptr;
    QLineEdit* stopTimeField_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
};

}
