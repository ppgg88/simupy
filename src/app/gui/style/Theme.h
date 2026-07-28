#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

namespace simupy::theme {

struct Palette {
    QColor canvas;
    QColor grid;
    QColor gridStrong;

    QColor blockFill;
    QColor blockFillHover;
    QColor blockStroke;
    QColor blockStrokeSelected;
    QColor blockText;
    QColor blockGlyph;
    QColor blockError;

    QColor port;
    QColor portHover;
    QColor wire;
    QColor wireSelected;
    QColor wirePending;

    QColor accent;
    QColor success;
    QColor warning;
    QColor error;

    /// Series colours for the scope, in order of use.
    QList<QColor> series;
};

const Palette& palette();

void refresh(const QPalette& applicationPalette);

void applyDarkTheme();

QFont monospaceFont(int pointSize = 10);

}  // namespace simupy::theme
