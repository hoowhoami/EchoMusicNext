#include "EchoIconSource.h"
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSvgRenderer>

namespace EchoMusic
{
EchoIconImageProvider::EchoIconImageProvider()
    : QQuickImageProvider(Image, ForceAsynchronousImageLoading)
{
}

QImage EchoIconImageProvider::requestImage(const QString &id, QSize *size,
                                           const QSize &requestedSize)
{
    if (size)
        *size = {};
    const auto fields = id.split('/');
    if (fields.size() != 2 || fields[1].size() != 8)
        return {};
    const auto path = EchoIconSource::iconResourcePath(fields[0]);
    bool valid = false;
    const auto rgba = fields[1].toUInt(&valid, 16);
    if (path.isEmpty() || !valid)
        return {};
    // Renderer, painter and image are local to this worker request; neither
    // QPixmap nor GUI objects nor another mutable cache cross thread boundaries.
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};
    renderer.setAspectRatioMode(Qt::KeepAspectRatio);
    if (size)
        *size = renderer.defaultSize();
    const QSize pixels(
        requestedSize.width() > 0 ? requestedSize.width() : renderer.defaultSize().width(),
        requestedSize.height() > 0 ? requestedSize.height() : renderer.defaultSize().height());
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    // Tint the alpha silhouette, preserving stroke antialiasing and foreground
    // alpha. Multiplying SVG RGB by a color would leave black glyphs black.
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), QColor::fromRgba(rgba));
    return image;
}

EchoIconSource *EchoIconSource::create(QQmlEngine *engine, QJSEngine *)
{
    // QQmlEngine owns and drains the provider; its destructor has no shared data.
    engine->addImageProvider(QStringLiteral("echo-icons"), new EchoIconImageProvider);
    return new EchoIconSource(engine);
}
QString EchoIconSource::iconResourcePath(const QString &glyph)
{
    static const QRegularExpression name(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*$"));
    if (!name.match(glyph).hasMatch())
        return {};
    const auto path = QStringLiteral(":/qt/qml/EchoMusic/Controls/icons/%1.svg").arg(glyph);
    return QFile::exists(path) ? path : QString{};
}
QUrl EchoIconSource::urlFor(const QString &glyph, const QColor &color) const
{
    if (!color.isValid() || iconResourcePath(glyph).isEmpty())
        return {};
    return QUrl(QStringLiteral("image://echo-icons/%1/%2")
                    .arg(glyph, QString::number(color.rgba(), 16).rightJustified(8, '0')));
}
QPointF EchoIconSource::visualCenter(const QString &glyph, const QSizeF &size) const
{
    const QPointF center(size.width() / 2, size.height() / 2);
    const auto path = iconResourcePath(glyph);
    if (path.isEmpty() || size.isEmpty())
        return center;
    QSvgRenderer renderer(path);
    const auto view = renderer.viewBoxF();
    const auto ink = renderer.boundsOnElement(QString());
    if (!renderer.isValid() || view.isEmpty() || ink.isEmpty())
        return center;
    // EchoIcon requests a square raster, then fits it in the item. Native SVG
    // bounds include the stroke; a triangle's ink need not center in its viewBox.
    const qreal scale = qMin(size.width(), size.height()) / qMax(view.width(), view.height());
    return center + (ink.center() - view.center()) * scale;
}
} // namespace EchoMusic
