#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QStyleFactory>

namespace simupy::theme {
namespace {

Palette g_palette;
bool g_initialized = false;

Palette darkPalette() {
    Palette p;
    p.canvas = QColor(0x1d, 0x21, 0x26);
    p.grid = QColor(0x27, 0x2c, 0x33);
    p.gridStrong = QColor(0x31, 0x37, 0x40);

    p.blockFill = QColor(0x2b, 0x31, 0x3a);
    p.blockFillHover = QColor(0x34, 0x3b, 0x46);
    p.blockStroke = QColor(0x6b, 0x77, 0x88);
    p.blockStrokeSelected = QColor(0x4d, 0x9d, 0xff);
    p.blockText = QColor(0xdd, 0xe3, 0xea);
    p.blockGlyph = QColor(0xf0, 0xf4, 0xf8);
    p.blockError = QColor(0xe5, 0x5c, 0x5c);

    p.port = QColor(0x8a, 0x97, 0xa8);
    p.portHover = QColor(0x6c, 0xc6, 0x4b);
    p.wire = QColor(0x93, 0xa1, 0xb2);
    p.wireSelected = QColor(0x4d, 0x9d, 0xff);
    p.wirePending = QColor(0x6c, 0xc6, 0x4b);

    p.accent = QColor(0x4d, 0x9d, 0xff);
    p.success = QColor(0x6c, 0xc6, 0x4b);
    p.warning = QColor(0xe8, 0xa3, 0x3d);
    p.error = QColor(0xe5, 0x5c, 0x5c);

    p.series = {QColor(0x5b, 0xa8, 0xf5), QColor(0xf5, 0x9e, 0x42),
                QColor(0x5c, 0xc9, 0x7a), QColor(0xe5, 0x6b, 0x6b),
                QColor(0xb1, 0x8a, 0xf0), QColor(0x4a, 0xc7, 0xc7),
                QColor(0xe0, 0xc3, 0x4a), QColor(0xf0, 0x7a, 0xc0)};
    return p;
}

Palette lightPalette() {
    Palette p;
    p.canvas = QColor(0xfa, 0xfb, 0xfc);
    p.grid = QColor(0xe6, 0xe9, 0xed);
    p.gridStrong = QColor(0xd2, 0xd8, 0xe0);

    p.blockFill = QColor(0xff, 0xff, 0xff);
    p.blockFillHover = QColor(0xf0, 0xf4, 0xf9);
    p.blockStroke = QColor(0x53, 0x5d, 0x6b);
    p.blockStrokeSelected = QColor(0x1a, 0x73, 0xe8);
    p.blockText = QColor(0x1f, 0x25, 0x2d);
    p.blockGlyph = QColor(0x13, 0x18, 0x1f);
    p.blockError = QColor(0xc0, 0x28, 0x28);

    p.port = QColor(0x53, 0x5d, 0x6b);
    p.portHover = QColor(0x1e, 0x8e, 0x3e);
    p.wire = QColor(0x44, 0x4e, 0x5c);
    p.wireSelected = QColor(0x1a, 0x73, 0xe8);
    p.wirePending = QColor(0x1e, 0x8e, 0x3e);

    p.accent = QColor(0x1a, 0x73, 0xe8);
    p.success = QColor(0x1e, 0x8e, 0x3e);
    p.warning = QColor(0xb8, 0x6e, 0x00);
    p.error = QColor(0xc0, 0x28, 0x28);

    p.series = {QColor(0x1a, 0x73, 0xe8), QColor(0xe8, 0x71, 0x0a),
                QColor(0x0f, 0x9d, 0x58), QColor(0xd9, 0x30, 0x25),
                QColor(0x7b, 0x3f, 0xd4), QColor(0x00, 0x97, 0xa7),
                QColor(0xb5, 0x9f, 0x00), QColor(0xc2, 0x18, 0x5b)};
    return p;
}

}

const Palette& palette() {
    if (!g_initialized) refresh(QApplication::palette());
    return g_palette;
}

void refresh(const QPalette& applicationPalette) {
    const QColor window = applicationPalette.color(QPalette::Window);
    const bool dark = window.lightness() < 128;
    g_palette = dark ? darkPalette() : lightPalette();
    g_initialized = true;
}

void applyDarkTheme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, QColor(0x25, 0x2a, 0x31));
    p.setColor(QPalette::WindowText, QColor(0xdd, 0xe3, 0xea));
    p.setColor(QPalette::Base, QColor(0x1d, 0x21, 0x26));
    p.setColor(QPalette::AlternateBase, QColor(0x2b, 0x31, 0x3a));
    p.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x31, 0x3a));
    p.setColor(QPalette::ToolTipText, QColor(0xdd, 0xe3, 0xea));
    p.setColor(QPalette::Text, QColor(0xdd, 0xe3, 0xea));
    p.setColor(QPalette::Button, QColor(0x2f, 0x35, 0x3e));
    p.setColor(QPalette::ButtonText, QColor(0xdd, 0xe3, 0xea));
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Link, QColor(0x4d, 0x9d, 0xff));
    p.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xba));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x7a, 0x85, 0x94));

    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6b, 0x74, 0x80));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6b, 0x74, 0x80));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6b, 0x74, 0x80));

    QApplication::setPalette(p);
    refresh(p);
}

QFont monospaceFont(int pointSize) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(pointSize);
    font.setStyleHint(QFont::Monospace);
    return font;
}

}
