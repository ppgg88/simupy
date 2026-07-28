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

bool svgSupported();

void paint(QPainter* painter, const QRectF& rect, const BlockIcon& icon,
           const QColor& textColor);

QIcon toIcon(const BlockIcon& icon, const QColor& textColor, int size = 16);

BlockIcon fromFile(const QString& path);

QString fileFilter();

}
}
