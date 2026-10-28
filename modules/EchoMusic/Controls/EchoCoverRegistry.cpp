#include "EchoCoverRegistry.h"
#include "EchoIconSource.h"
#include <QFile>
#include <QXmlStreamReader>
#include <cmath>
#include <QTimer>
#include <QScopedValueRollback>
#include <algorithm>
namespace EchoMusic
{
EchoCoverRegistry::EchoCoverRegistry(QQmlEngine &engine) : QObject(&engine) {}
EchoCoverRegistry *EchoCoverRegistry::create(QQmlEngine *engine, QJSEngine *)
{
    return engine ? new EchoCoverRegistry(*engine) : nullptr;
}
EchoCoverRegistry *EchoCoverRegistry::forEngine(QQmlEngine &engine)
{
    return engine.singletonInstance<EchoCoverRegistry *>("EchoMusic.Controls", "EchoCoverRegistry");
}
bool EchoCoverRegistry::put(const QString &token, int priority,
                            std::function<QUrl(const QVariantMap &)> callback)
{
    if (token.isEmpty() || !callback || priority < -10000 || priority > 10000 ||
        (!m_entries.contains(token) && m_entries.size() >= 64))
        return false;
    m_entries.insert(token, {token, priority, std::move(callback)});
    emit changed();
    return true;
}
void EchoCoverRegistry::remove(const QString &token)
{
    if (m_entries.remove(token))
        emit changed();
}
void EchoCoverRegistry::invalidate(const QString &token)
{
    if (m_entries.contains(token))
        emit changed();
}
QUrl EchoCoverRegistry::resolve(const QVariantMap &context, const QSet<QUrl> &rejected)
{
    if (m_resolving)
        return {};
    QScopedValueRollback guard(m_resolving, true);
    auto entries = m_entries.values();
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.priority != b.priority ? a.priority > b.priority : a.token < b.token;
    });
    for (const auto &entry : entries) {
        if (!m_entries.contains(entry.token))
            continue;
        const auto result = entry.resolve(context);
        // A callback can dispose itself or another contribution synchronously.
        if (m_entries.contains(entry.token) && !result.isEmpty() && result.isValid() &&
            !rejected.contains(result))
            return result;
    }
    return {};
}
QUrl EchoCoverRegistry::themedIconUrl(const QString &glyph, const QColor &accent)
{
    if (!accent.isValid())
        return {};
    const auto path = EchoIconSource::iconResourcePath(glyph);
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto svg = QString::fromUtf8(file.readAll());
    const auto body =
        svg.mid(svg.indexOf('>') + 1, svg.lastIndexOf("</svg>") - svg.indexOf('>') - 1);
    if (body.isEmpty())
        return {};
    return themedSvgUrl(body, 24, 24, accent);
}
QUrl EchoCoverRegistry::themedSvgUrl(const QString &body, double width, double height,
                                     const QColor &accent)
{
    if (body.isEmpty() || body.size() > 65536 || !accent.isValid() || !std::isfinite(width) ||
        !std::isfinite(height) || width < 1 || height < 1 || width > 1024 || height > 1024)
        return {};
    QXmlStreamReader xml("<svg>" + body + "</svg>");
    int nodes = 0;
    const QStringList tags{
        "svg",  "g",        "path",    "rect", "circle",         "ellipse",
        "line", "polyline", "polygon", "defs", "linearGradient", "radialGradient",
        "stop", "clipPath", "use"};
    const QStringList attributes{"id",
                                 "d",
                                 "x",
                                 "y",
                                 "x1",
                                 "y1",
                                 "x2",
                                 "y2",
                                 "cx",
                                 "cy",
                                 "r",
                                 "rx",
                                 "ry",
                                 "width",
                                 "height",
                                 "points",
                                 "fill",
                                 "stroke",
                                 "stroke-width",
                                 "stroke-linecap",
                                 "stroke-linejoin",
                                 "fill-rule",
                                 "clip-rule",
                                 "opacity",
                                 "fill-opacity",
                                 "stroke-opacity",
                                 "transform",
                                 "offset",
                                 "stop-color",
                                 "stop-opacity",
                                 "gradientUnits",
                                 "gradientTransform",
                                 "clip-path",
                                 "href"};
    while (!xml.atEnd()) {
        const auto type = xml.readNext();
        if (type == QXmlStreamReader::DTD || type == QXmlStreamReader::EntityReference ||
            type == QXmlStreamReader::ProcessingInstruction)
            return {};
        if (!xml.isStartElement())
            continue;
        if (++nodes > 2048 || !tags.contains(xml.name().toString()))
            return {};
        for (const auto &a : xml.attributes()) {
            const auto name = a.name().toString(), value = a.value().toString();
            if (!attributes.contains(name) || !a.namespaceUri().isEmpty() ||
                (name == "href" && !value.startsWith('#')) ||
                (value.contains("url(") && (!value.startsWith("url(#") || !value.endsWith(')'))))
                return {};
        }
    }
    if (xml.hasError())
        return {};
    const double scale = 168. / std::max(width, height);
    const auto transform = QString("translate(%1 %2) scale(%3)")
                               .arg((400 - width * scale) / 2)
                               .arg((400 - height * scale) / 2)
                               .arg(scale);
    const auto from = accent.name(QColor::HexRgb), to = accent.lighter(135).name(QColor::HexRgb);
    const auto document =
        QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"400\" "
                       "viewBox=\"0 0 400 400\"><defs><linearGradient id=\"g\" x2=\"1\" "
                       "y2=\"1\"><stop stop-color=\"%1\"/><stop offset=\"1\" "
                       "stop-color=\"%2\"/></linearGradient></defs><rect width=\"400\" "
                       "height=\"400\" fill=\"url(#g)\"/><circle cx=\"104\" cy=\"96\" r=\"52\" "
                       "fill=\"white\" opacity=\".14\"/><circle cx=\"308\" cy=\"304\" r=\"72\" "
                       "fill=\"white\" opacity=\".1\"/><rect x=\"108\" y=\"108\" width=\"184\" "
                       "height=\"184\" rx=\"46\" fill=\"white\" opacity=\".18\"/><g "
                       "transform=\"%4\">%3</g></svg>")
            .arg(from, to, QString(body).replace("currentColor", "#ffffff"), transform);
    return QUrl("data:image/svg+xml;base64," + QString::fromLatin1(document.toUtf8().toBase64()));
}
void EchoCoverSource::componentComplete()
{
    m_complete = true;
    if (auto *engine = qmlEngine(this))
        m_registry = EchoCoverRegistry::forEngine(*engine);
    schedule(true);
}
void EchoCoverSource::setSource(const QUrl &value)
{
    if (m_source != value) {
        m_source = value;
        emit inputChanged();
        schedule(true);
    }
}
void EchoCoverSource::setContext(const QVariantMap &value)
{
    if (m_context != value) {
        m_context = value;
        emit inputChanged();
        schedule(false);
    }
}
void EchoCoverSource::setActive(bool value)
{
    if (m_active != value) {
        m_active = value;
        emit inputChanged();
        schedule(true);
    }
}
void EchoCoverSource::schedule(bool reset)
{
    if (reset)
        m_rejected.clear();
    if (m_complete && (!m_active || (reset && m_source.isEmpty()) ||
                       (!m_source.isEmpty() && !m_rejected.contains(m_source)))) {
        const auto value = m_active ? m_source : QUrl{};
        if (m_resolved != value) {
            m_resolved = value;
            emit changed();
        }
    }
    if (!m_complete || std::exchange(m_queued, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_queued = false;
        resolve();
    });
}
void EchoCoverSource::reject(const QUrl &source)
{
    if (!m_active || source.isEmpty() || source != m_resolved || m_rejected.size() >= 65)
        return;
    m_rejected.insert(source);
    schedule(false);
}
void EchoCoverSource::resolve()
{
    const bool fallback = m_active && (m_source.isEmpty() || m_rejected.contains(m_source));
    if (fallback && !m_connection && m_registry)
        m_connection = connect(m_registry, &EchoCoverRegistry::changed, this, [this] {
            const bool primaryFailed = m_rejected.contains(m_source);
            m_rejected.clear();
            if (primaryFailed)
                m_rejected.insert(m_source);
            schedule(false);
        });
    else if (!fallback && m_connection) {
        disconnect(m_connection);
        m_connection = {};
    }
    QUrl resolved = m_active ? m_source : QUrl{};
    if (fallback) {
        auto context = m_context;
        context["url"] = m_source.toString();
        context["normalizedUrl"] = m_source.toString(QUrl::NormalizePathSegments);
        context["reason"] = m_source.isEmpty() ? "empty" : "error";
        if (!m_source.isEmpty())
            context["failedUrl"] = m_source.toString();
        resolved = m_registry ? m_registry->resolve(context, m_rejected) : QUrl{};
    }
    if (!m_active)
        m_rejected.clear();
    if (m_resolved != resolved) {
        m_resolved = resolved;
        emit changed();
    }
}
} // namespace EchoMusic
