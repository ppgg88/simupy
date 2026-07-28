#pragma once

#include <QIcon>

namespace simupy {

/// Toolbar and menu icons, drawn rather than loaded.
///
/// Two reasons not to ship image files: they would need a Qt resource system
/// this project does not otherwise use, and a system icon theme is not
/// something a Linux desktop guarantees — a build that silently loses half its
/// toolbar is worse than one that draws its own. Drawing them also means they
/// follow the application palette, so the same code looks right on the dark
/// theme and the light one.
///
/// Icons are cached after the first request, and every set is rendered at the
/// sizes Qt actually asks for, so they stay crisp on a HiDPI screen.
namespace appicons {

QIcon run();
QIcon stop();
QIcon realTime();

QIcon newModel();
QIcon open();
QIcon save();

QIcon copy();
QIcon cut();
QIcon paste();
QIcon duplicate();
QIcon remove();
QIcon mirror();
QIcon addBlock();
QIcon nameSignal();

QIcon enterSubsystem();
QIcon leaveSubsystem();
QIcon zoomIn();
QIcon zoomOut();
QIcon zoomFit();

QIcon library();
QIcon saveAsBlock();
QIcon settings();

void invalidateCache();

}  // namespace appicons
}  // namespace simupy
