#pragma once

#include <QIcon>

namespace simupy {

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

}
}
