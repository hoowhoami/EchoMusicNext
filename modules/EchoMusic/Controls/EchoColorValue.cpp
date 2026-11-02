#include "EchoColorValue.h"
#include "ThemeStore.h"
#include <QFontDatabase>
#include <QQmlEngine>
#include <QRegularExpression>
namespace EchoMusic
{
EchoColorValue *EchoColorValue::create(QQmlEngine *engine, QJSEngine *)
{
    // Engine-owned singleton: helpers are stateless and die with their engine.
    return new EchoColorValue(engine);
}
QFont EchoColorValue::fixedFont() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}
QString EchoColorValue::normalize(const QString &text) const
{
    static const QRegularExpression hex("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$");
    const auto raw = text.trimmed();
    return hex.match(raw).hasMatch() ? QColor(raw).name() : QStringLiteral("#0071e3");
}
qreal EchoColorValue::contrast(const QColor &a, const QColor &b) const
{
    return ThemeStore::contrast(a, b);
}
QVariantMap EchoColorValue::hsv(const QString &text) const
{
    const QColor color(normalize(text));
    return {{"hue", color.hsvHueF()},
            {"saturation", color.hsvSaturationF()},
            {"value", color.valueF()}};
}
QString EchoColorValue::fromHsv(qreal hue, qreal saturation, qreal value) const
{
    return QColor::fromHsvF(qBound(0., hue, 1.) == 1 ? 0 : qBound(0., hue, 1.),
                            qBound(0., saturation, 1.), qBound(0., value, 1.))
        .name();
}
} // namespace EchoMusic
