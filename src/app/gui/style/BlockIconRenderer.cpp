#include "BlockIconRenderer.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>

#ifdef SIMUPY_HAVE_SVG
#include <QSvgRenderer>
#endif

namespace simupy {
namespace icons {
namespace {

QPixmap rasterFor(const QByteArray& bytes, const QSize& size) {
    static QHash<QByteArray, QPixmap> cache;

    QByteArray key = QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
    key += QByteArray::number(size.width()) + 'x' +
           QByteArray::number(size.height());

    const auto found = cache.constFind(key);
    if (found != cache.constEnd()) return *found;

    QImage image;
    if (!image.loadFromData(bytes)) return {};

    QPixmap pixmap = QPixmap::fromImage(image.scaled(
        size, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (cache.size() > 128) cache.clear();
    cache.insert(key, pixmap);
    return pixmap;
}

void paintText(QPainter* painter, const QRectF& rect, const QString& text,
               const QColor& color) {
    if (text.isEmpty()) return;

    QFont font = painter->font();
    font.setBold(true);
    for (int size = 20; size >= 5; --size) {
        font.setPointSize(size);
        const QFontMetricsF metrics(font);
        if (metrics.horizontalAdvance(text) <= rect.width() &&
            metrics.height() <= rect.height())
            break;
    }

    painter->save();
    painter->setFont(font);
    painter->setPen(color);
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}

}  // namespace

bool svgSupported() {
#ifdef SIMUPY_HAVE_SVG
    return true;
#else
    return false;
#endif
}

void paint(QPainter* painter, const QRectF& rect, const BlockIcon& icon,
           const QColor& textColor) {
    if (icon.empty() || rect.isEmpty()) return;

    switch (icon.kind) {
        case BlockIcon::Kind::Text:
            paintText(painter, rect, QString::fromStdString(icon.data),
                      textColor);
            return;

        case BlockIcon::Kind::Svg: {
#ifdef SIMUPY_HAVE_SVG
            QSvgRenderer renderer(
                QByteArray::fromStdString(icon.data));
            if (!renderer.isValid()) return;

            QSizeF size = renderer.defaultSize();
            if (size.isEmpty()) size = rect.size();
            size.scale(rect.size(), Qt::KeepAspectRatio);
            QRectF target(QPointF(), size);
            target.moveCenter(rect.center());
            renderer.render(painter, target);
#endif
            return;
        }

        case BlockIcon::Kind::Raster: {
            const qreal ratio = painter->device()->devicePixelRatioF();
            const QSize pixels = (rect.size() * ratio).toSize();
            QPixmap pixmap = rasterFor(QByteArray::fromStdString(icon.data),
                                       pixels);
            if (pixmap.isNull()) return;

            pixmap.setDevicePixelRatio(ratio);
            QRectF target(QPointF(), QSizeF(pixmap.size()) / ratio);
            target.moveCenter(rect.center());
            painter->drawPixmap(target.topLeft(), pixmap);
            return;
        }

        case BlockIcon::Kind::None:
            return;
    }
}

QIcon toIcon(const BlockIcon& icon, const QColor& textColor, int size) {
    if (icon.empty()) return {};

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint(&painter, QRectF(0, 0, size, size), icon, textColor);
    painter.end();

    return QIcon(pixmap);
}

BlockIcon fromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw ModelError("cannot read '" + path.toStdString() + "'");

    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty())
        throw ModelError("'" + path.toStdString() + "' is empty");
    if (bytes.size() > 512 * 1024)
        throw ModelError("this image is " +
                         std::to_string(bytes.size() / 1024) +
                         " kB; keep an icon under 512 kB so the library stays "
                         "small enough to share comfortably");

    BlockIcon icon;
    const QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == QLatin1String("svg") || suffix == QLatin1String("svgz")) {
        if (!svgSupported())
            throw ModelError("this build cannot read SVG — install the Qt SVG "
                             "module (qt6-svg-dev) and rebuild, or use a PNG");
#ifdef SIMUPY_HAVE_SVG
        QSvgRenderer renderer(bytes);
        if (!renderer.isValid())
            throw ModelError("this SVG will not parse");
#endif
        icon.kind = BlockIcon::Kind::Svg;
        icon.data = bytes.toStdString();
        return icon;
    }

    QImage probe;
    if (!probe.loadFromData(bytes))
        throw ModelError("this file is not an image SimuPy can read");

    icon.kind = BlockIcon::Kind::Raster;
    icon.data = bytes.toStdString();
    return icon;
}

QString fileFilter() {
    QStringList patterns{QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                         QStringLiteral("*.jpeg"), QStringLiteral("*.bmp")};
    if (svgSupported()) patterns.prepend(QStringLiteral("*.svg"));

    return QObject::tr("Images (%1)").arg(patterns.join(QLatin1Char(' ')));
}

}  // namespace icons
}  // namespace simupy
