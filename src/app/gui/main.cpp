#include "MainWindow.h"
#include "app/gui/style/Theme.h"
#include "model/BlockRegistry.h"
#include "io/CustomBlock.h"
#include "scripting/PythonEngine.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QSplashScreen>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SimuPy"));
    QApplication::setApplicationVersion(QStringLiteral(SIMUPY_VERSION));
    QApplication::setOrganizationName(QStringLiteral("SimuPy"));

    // Ties the window to its desktop entry under Wayland and inside Flatpak.
    QApplication::setDesktopFileName(QStringLiteral("io.github.ppgg88.SimuPy"));
    // An icon theme is a freedesktop idea: fromTheme finds nothing on Windows
    // or macOS, so it falls back to the raster copy built into the binary.
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("io.github.ppgg88.SimuPy"),
                         QIcon(QStringLiteral(":/appicon.png"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Block-diagram simulation with Python blocks"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("model"),
                                 QStringLiteral("Model file to open (.spy)"));

    QCommandLineOption lightOption(QStringLiteral("light"),
                                   QStringLiteral("Use the light theme"));
    parser.addOption(lightOption);

    QCommandLineOption runOption(
        QStringLiteral("run"),
        QStringLiteral("Simulate the model as soon as it is open"));
    parser.addOption(runOption);
    parser.process(app);

    if (!parser.isSet(lightOption))
        simupy::theme::applyDarkTheme();
    else
        simupy::theme::refresh(QApplication::palette());

    simupy::registerBuiltinBlocks();

    const QString userLibrary =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/blocks");
    QDir().mkpath(userLibrary);

    try {
        simupy::PythonEngine::instance().initialize(
            {userLibrary.toStdString(), QDir::currentPath().toStdString()});
    } catch (const simupy::ModelError& error) {
        QMessageBox::critical(
            nullptr, QStringLiteral("SimuPy"),
            QStringLiteral(
                "Python could not be started, so Python blocks will not "
                "work.\n\n%1")
                .arg(QString::fromStdString(error.what())));
    }

    const std::vector<std::string> libraryProblems =
        simupy::LibraryManager::instance().loadAll();

    simupy::MainWindow window;
    window.show();

    for (const std::string& problem : libraryProblems)
        window.reportLibraryProblem(QString::fromStdString(problem));

    const QStringList positional = parser.positionalArguments();
    const bool opened = !positional.isEmpty() && window.openFile(positional.first());

    if (opened && parser.isSet(runOption)) {
        QTimer::singleShot(0, &window, &simupy::MainWindow::startSimulation);
    }

    return app.exec();
}
