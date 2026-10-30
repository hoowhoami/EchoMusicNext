#include "AssetSvgValidation.h"
#include <QBuffer>
#include <QImageReader>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>
#include <cmath>

namespace EchoMusic
{
namespace
{
constexpr qint64 sourceLimit = 8 * 1024 * 1024;
bool fail(QString &error, const QString &message)
{
    error = message;
    return false;
}
bool fragment(const QString &value)
{
    if (value.size() < 2 || value.size() > 1024 || value.front() != '#')
        return false;
    for (const auto character : value.mid(1))
        if (character.isSpace() || character.unicode() < 32 || character == '\\' ||
            character == '\'' || character == '"' || character == '(' || character == ')')
            return false;
    return true;
}
bool cssReferences(QString text, QString &error, QList<QPair<QString, QString>> &references,
                   const QString &attribute = {})
{
    // Deliberately finite CSS reference syntax. Decode XML first, remove comments,
    // then inspect functions; CSS escapes and at-rules are outside this language.
    if (text.contains('\\') || text.contains('@'))
        return fail(error, "SVG CSS escapes and at-rules are not supported");
    while (true) {
        const auto first = text.indexOf("/*");
        if (first < 0)
            break;
        const auto last = text.indexOf("*/", first + 2);
        if (last < 0)
            return fail(error, "Unclosed SVG CSS comment");
        text.remove(first, last + 2 - first);
    }
    static const QRegularExpression declarations(
        "(?:^|[;{])\\s*(opacity|mask|clip-path|marker(?:-start|-mid|-end)?)\\s*:\\s*([^;}]+)",
        QRegularExpression::CaseInsensitiveOption);
    auto declaration = declarations.globalMatch(text);
    while (declaration.hasNext()) {
        const auto match = declaration.next();
        bool number = false;
        const auto opacity = match.captured(2).trimmed().toDouble(&number);
        if (match.captured(1).compare("opacity", Qt::CaseInsensitive) || !number || opacity != 1)
            return fail(error, "SVG CSS group opacity, mask and clip selectors are outside the "
                               "bounded static subset");
    }
    static const QRegularExpression functions("([a-zA-Z_-][a-zA-Z0-9_-]*)\\s*\\(");
    auto iterator = functions.globalMatch(text);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        const auto name = match.captured(1).toLower();
        if (name == "url") {
            const auto end = text.indexOf(')', match.capturedEnd());
            if (end < 0)
                return fail(error, "Unclosed SVG CSS URL");
            auto value = text.mid(match.capturedEnd(), end - match.capturedEnd()).trimmed();
            if (value.size() >= 2 && ((value.front() == '\'' && value.back() == '\'') ||
                                      (value.front() == '"' && value.back() == '"')))
                value = value.mid(1, value.size() - 2);
            if (!fragment(value))
                return fail(error, "SVG CSS may only reference local fragment IDs");
            auto property = attribute.toLower();
            if (property.isEmpty() || property == "style") {
                const auto before = text.left(match.capturedStart());
                const auto colon = before.lastIndexOf(':');
                const auto boundary = qMax(before.lastIndexOf(';'), before.lastIndexOf('{'));
                if (colon <= boundary)
                    return fail(error, "SVG URL is not attached to a supported paint property");
                property = before.mid(boundary + 1, colon - boundary - 1).trimmed().toLower();
            }
            if (property == "fill" || property == "stroke")
                references.append({value.mid(1), "paint"});
            else if (property == "mask")
                references.append({value.mid(1), "mask"});
            else if (property == "clip-path")
                references.append({value.mid(1), "clipPath"});
            else
                return fail(error,
                            "SVG URLs are supported only for fill, stroke, clip-path and mask");
        } else if (name != "rgb" && name != "rgba" && name != "hsl" && name != "hsla" &&
                   name != "matrix" && name != "translate" && name != "scale" && name != "rotate" &&
                   name != "skewx" && name != "skewy") {
            return fail(error, "Unsupported SVG CSS function");
        }
    }
    return true;
}
bool embeddedRaster(QString &value, qint64 &pixels, QString &error)
{
    static const QRegularExpression syntax(
        "^data:(image/(?:png|jpeg|gif|webp));base64,([A-Za-z0-9+/=\\t\\r\\n ]+)$",
        QRegularExpression::CaseInsensitiveOption);
    const auto match = syntax.match(value);
    if (!match.hasMatch())
        return fail(error, "SVG images require an embedded PNG, JPEG, GIF or WebP data URL");
    auto encoded = match.captured(2).toLatin1();
    encoded.replace("\r", "");
    encoded.replace("\n", "");
    encoded.replace("\t", "");
    encoded.replace(" ", "");
    const auto decoded =
        QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > sourceLimit)
        return fail(error, "Invalid SVG raster base64 or byte budget");
    QBuffer buffer;
    buffer.setData(decoded.decoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg" && format != "gif" && format != "webp")
        return fail(error, "SVG raster data has an unsupported actual format");
    if (match.captured(1).compare("image/" + QString::fromLatin1(format), Qt::CaseInsensitive))
        return fail(error, "SVG raster MIME does not match its content");
    if (format == "gif" && !decoded.decoded.endsWith('\x3b'))
        return fail(error, "SVG embedded GIF is missing its trailer");
    if (format == "webp" &&
        (decoded.decoded.size() < 12 ||
         quint64(qFromLittleEndian<quint32>(decoded.decoded.constData() + 4)) + 8 !=
             quint64(decoded.decoded.size())))
        return fail(error, "SVG embedded WebP is incomplete");
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096 ||
        qint64(size.width()) * size.height() > 16 * 1024 * 1024 || reader.imageCount() > 256)
        return fail(error, "SVG embedded raster exceeds its dimensions or frame budget");
    int frames = 0;
    const auto expected = reader.imageCount();
    while (reader.canRead()) {
        const auto frame = reader.read();
        if (frame.isNull() || ++frames > 256 || frame.width() > 4096 || frame.height() > 4096)
            return fail(error, "Cannot decode SVG raster within its frame budget");
        pixels += qint64(frame.width()) * frame.height();
        if (pixels > 64 * 1024 * 1024)
            return fail(error, "SVG raster pixels exceed 64 Mi pixels");
        if (!reader.supportsAnimation())
            break;
    }
    if (frames == 0 || (expected > 0 && frames != expected))
        return fail(error, "Incomplete SVG embedded raster");
    value = "data:image/" + QString::fromLatin1(format) + ";base64," +
            QString::fromLatin1(decoded.decoded.toBase64());
    return true;
}
} // namespace

bool AssetSvgValidation::render(const QByteArray &source, QImage &image, QString &error)
{
    image = {};
    error.clear();
    if (source.isEmpty() || source.size() > sourceLimit)
        return fail(error, "SVG source exceeds 8 MiB");
    static const QSet<QString> elements{
        "svg",   "g",      "defs",           "switch",         "a",        "style",    "path",
        "rect",  "circle", "ellipse",        "line",           "polyline", "polygon",  "text",
        "tspan", "image",  "linearGradient", "radialGradient", "stop",     "clipPath", "mask"};
    static const QSet<QString> leaves{"path",     "rect",    "circle", "ellipse", "line",
                                      "polyline", "polygon", "image",  "stop"};
    static const QRegularExpression pathTokens(
        "[a-zA-Z]|[-+]?(?:(?:[0-9]+\\.?[0-9]*)|(?:\\.[0-9]+))(?:[eE][-+]?[0-9]+)?");
    QXmlStreamReader xml(source);
    QByteArray sanitized;
    QXmlStreamWriter writer(&sanitized);
    qint64 rasterPixels = 0, tokenCount = 0;
    int depth = 0, elementCount = 0, attributeCount = 0;
    int maskDefinitions = 0, maskReferences = 0, resourceDepth = 0;
    QList<bool> leafStack;
    QHash<QString, QString> idTypes;
    QList<QPair<QString, QString>> typedReferences;
    bool rootSeen = false, inStyle = false;
    QString styleText;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference ||
            token == QXmlStreamReader::ProcessingInstruction)
            return fail(error, "SVG DTD, entities and processing instructions are forbidden");
        if (token == QXmlStreamReader::StartElement) {
            if (inStyle)
                return fail(error, "SVG style content must be text or CDATA only");
            const auto name = xml.name().toString();
            const auto namespaceUri = xml.namespaceUri().toString();
            if (!rootSeen) {
                if (name != "svg" ||
                    (!namespaceUri.isEmpty() && namespaceUri != "http://www.w3.org/2000/svg"))
                    return fail(error, "The SVG root is invalid");
                rootSeen = true;
            }
            // Editor metadata is not a rendering input. Do not forward arbitrary
            // RDF/foreign XML to QtSvg's namespace-insensitive parser.
            if (name == "metadata" || name == "title" || name == "desc") {
                xml.skipCurrentElement();
                continue;
            }
            if (name == "animate" || name == "animateTransform" || name == "animateMotion" ||
                name == "set" || name == "mpath") {
                xml.skipCurrentElement();
                continue;
            }
            if (!elements.contains(name) ||
                (!namespaceUri.isEmpty() && namespaceUri != "http://www.w3.org/2000/svg"))
                return fail(error, "Unsupported SVG element or namespace");
            if (!leafStack.isEmpty() && leafStack.constLast())
                return fail(error, "SVG paint leaves may not contain rendering children");
            if (name == "mask" && ++maskDefinitions > 1)
                return fail(error, "The static SVG subset supports one mask definition");
            if (++depth > 128 || ++elementCount > 100000)
                return fail(error, "SVG nesting or element budget exceeded");
            leafStack.append(leaves.contains(name));
            if (!resourceDepth && (name == "defs" || name == "mask" || name == "clipPath"))
                resourceDepth = depth;
            writer.writeStartElement(name);
            if (depth == 1) {
                writer.writeDefaultNamespace("http://www.w3.org/2000/svg");
                writer.writeNamespace("http://www.w3.org/1999/xlink", "xlink");
            }
            for (const auto &attribute : xml.attributes()) {
                if (++attributeCount > 500000)
                    return fail(error, "SVG attribute budget exceeded");
                const auto key = attribute.name().toString();
                const auto uri = attribute.namespaceUri().toString();
                auto value = attribute.value().toString();
                if (key.startsWith("on", Qt::CaseInsensitive) || key == "base")
                    return fail(error, "SVG event handlers and base URLs are forbidden");
                if (!uri.isEmpty() && uri != "http://www.w3.org/1999/xlink" &&
                    uri != "http://www.w3.org/XML/1998/namespace")
                    continue; // non-rendering editor attributes are omitted
                if (key == "marker" || key == "marker-start" || key == "marker-mid" ||
                    key == "marker-end")
                    return fail(error,
                                "SVG marker references are outside the bounded static subset");
                if (key == "id") {
                    if (!uri.isEmpty() || value.isEmpty() || value.size() > 1024 ||
                        idTypes.contains(value))
                        return fail(error, "SVG IDs must be unique unqualified bounded names");
                    idTypes.insert(value, name);
                }
                if ((key == "mask" || key == "clip-path") && value.trimmed() != "none") {
                    static const QRegularExpression localReference(
                        "^url\\(\\s*['\"]?(#[^'\"\\s()]+)['\"]?\\s*\\)$");
                    const auto reference = localReference.match(value.trimmed());
                    if (!reference.hasMatch())
                        return fail(error,
                                    "SVG mask and clip references require one local fragment");
                    typedReferences.append(
                        {reference.captured(1).mid(1), key == "mask" ? "mask" : "clipPath"});
                }
                if (key == "opacity" && !leaves.contains(name)) {
                    bool number = false;
                    const auto opacity = value.trimmed().toDouble(&number);
                    if (!number || opacity != 1)
                        return fail(error,
                                    "SVG container opacity is outside the bounded static subset");
                }
                if (key == "mask" && value.trimmed() != "none") {
                    if (resourceDepth || !leaves.contains(name) || ++maskReferences > 1)
                        return fail(error, "The static SVG subset supports one leaf mask reference "
                                           "outside resources");
                }
                if (key == "clip-path" && resourceDepth && value.trimmed() != "none")
                    return fail(
                        error,
                        "SVG clipping resources may not recursively reference clipping resources");
                if (key == "href") {
                    if (name == "image") {
                        value = value.trimmed();
                        if (!embeddedRaster(value, rasterPixels, error))
                            return false;
                    } else
                        return fail(error, "SVG href inheritance and node expansion are outside "
                                           "the bounded static subset");
                } else if (!cssReferences(value, error, typedReferences, key))
                    return false;
                if (key == "d" || key == "points") {
                    auto tokens = pathTokens.globalMatch(value);
                    while (tokens.hasNext()) {
                        tokens.next();
                        if (++tokenCount > 1000000)
                            return fail(error, "SVG path token budget exceeded");
                    }
                }
                if (uri == "http://www.w3.org/1999/xlink")
                    writer.writeAttribute("xlink:" + key, value);
                else if (uri == "http://www.w3.org/XML/1998/namespace")
                    writer.writeAttribute("xml:" + key, value);
                else
                    writer.writeAttribute(key, value);
            }
            inStyle = name == "style";
            if (inStyle)
                styleText.clear();
        } else if (token == QXmlStreamReader::EndElement) {
            if (inStyle && !cssReferences(styleText, error, typedReferences))
                return false;
            writer.writeEndElement();
            if (resourceDepth == depth)
                resourceDepth = 0;
            leafStack.removeLast();
            --depth;
            inStyle = false;
        } else if (token == QXmlStreamReader::Characters) {
            if (inStyle)
                styleText += xml.text();
            writer.writeCharacters(xml.text());
        }
    }
    if (xml.hasError() || !rootSeen || depth || writer.hasError())
        return fail(error, "Malformed SVG XML");
    for (const auto &reference : typedReferences) {
        const auto type = idTypes.value(reference.first);
        if (reference.second == "paint" ? type != "linearGradient" && type != "radialGradient"
                                        : type != reference.second)
            return fail(
                error,
                "SVG paint, mask or clip reference has a missing or incorrect resource type");
    }
    QSvgRenderer renderer;
    renderer.setOptions(QtSvg::DisableAnimations);
    renderer.setAnimationEnabled(false);
    if (!renderer.load(sanitized))
        return fail(error, "QtSvg cannot load this static SVG");
    renderer.setAnimationEnabled(false);
    const auto size = renderer.defaultSize();
    const auto view = renderer.viewBoxF();
    if (!size.isValid() || size.width() < 1 || size.height() < 1 || size.width() > 4096 ||
        size.height() > 4096 || qint64(size.width()) * size.height() > 16 * 1024 * 1024 ||
        !std::isfinite(view.x()) || !std::isfinite(view.y()) || !std::isfinite(view.width()) ||
        !std::isfinite(view.height()))
        return fail(error, "SVG dimensions are invalid or exceed 4096 pixels / 16 Mi pixels");
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull())
        return fail(error, "Cannot allocate bounded SVG image");
    result.fill(Qt::transparent);
    QPainter painter(&result);
    renderer.render(&painter);
    painter.end();
    image = std::move(result);
    return true;
}
} // namespace EchoMusic
