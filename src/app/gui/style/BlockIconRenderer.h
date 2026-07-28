#pragma once

#include "io/CustomBlock.h"

#include <QIcon>
#include <QString>

QT_BEGIN_NAMESPACE
class QPainter;
class QRectF;
QT_END_NAMESPACE

namespace simupy {
namespace icons {

/// True when this build can rasterise SVG. SVG needs Qt6::Svg, which is a
/// separate package on most distributions, so a build without it still runs —
/// it just cannot draw that one icon format.
bool svgSupported();

void paint(QPainter* painter, const QRectF& rect, const BlockIcon& icon,
           const QColor& textColor);

/// A small icon for the palette tree, or a null QIcon when there is nothing
/// to draw.
QIcon toIcon(const BlockIcon& icon, const QColor& textColor, int size = 16);

/// Reads an image file. Throws ModelError when the format is unsupported or
/// the file will not decode — an icon that silently fails to appear is worse
/// than one that refuses to import.
BlockIcon fromFile(const QString& path);

QString fileFilter();

}  // namespace icons
}  // namespace simupy
