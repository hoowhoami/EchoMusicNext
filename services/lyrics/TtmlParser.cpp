#include "TtmlParser_p.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
#include <compare>
#include <optional>
namespace EchoMusic::LyricCodecs
{
namespace
{
const QString TT = "http://www.w3.org/ns/ttml";
const QString TTM = "http://www.w3.org/ns/ttml#metadata";
const QString TTP = "http://www.w3.org/ns/ttml#parameter";
const QString XML = "http://www.w3.org/XML/1998/namespace";
const QString APPLE = "http://music.apple.com/lyric-ttml-internal";
const QString STYLING = "http://www.w3.org/ns/ttml#styling";
constexpr qint64 SafeTime = 9007199254740991LL;
constexpr quint64 SubMs = 1000000000000000000ULL, Limb = 1000000000ULL;
constexpr qsizetype MaxNodes = 65536, MaxText = 1048576, MaxLine = 16384;
struct Failure
{
    QString code, message;
};
[[noreturn]] void invalid()
{
    throw Failure{"decode", "TTML 歌词结构或时间无效"};
}
[[noreturn]] void unsupported()
{
    throw Failure{"unsupported", "TTML 使用了当前歌词 profile 不支持的结构或时间模式"};
}
[[noreturn]] void overflow()
{
    throw Failure{"numeric_overflow", "歌词时间超出可表示范围"};
}
void limit(bool valid)
{
    if (!valid)
        throw Failure{"resource_limit", "歌词超过解析资源预算"};
}
void check(const std::function<bool()> &canceled)
{
    if (canceled())
        throw Failure{"Canceled", "TTML 歌词解析已取消"};
}
// Mixed fixed decimals avoid compiler-specific 128-bit arithmetic. Only the
// fractional millisecond uses 18 decimals; the integral part keeps JSON's exact
// safe-integer range. Two base-1e9 limbs make unit scaling exact on MSVC too.
struct Time
{
    qint64 whole = 0;
    quint64 sub = 0;
    auto operator<=>(const Time &) const = default;
};
Time checked(Time value)
{
    if (value.whole > SafeTime || (value.whole == SafeTime && value.sub))
        overflow();
    return value;
}
Time add(Time a, Time b)
{
    if (a.whole > SafeTime - b.whole)
        overflow();
    auto sub = a.sub + b.sub;
    Time result{a.whole + b.whole, sub % SubMs};
    if (sub >= SubMs) {
        if (result.whole == SafeTime)
            overflow();
        ++result.whole;
    }
    return checked(result);
}
qint64 millis(Time value)
{
    return value.whole + (value.sub >= SubMs / 2 ? 1 : 0);
}
bool digits(QStringView value)
{
    return !value.isEmpty() &&
           std::all_of(value.begin(), value.end(), [](QChar c) { return c >= u'0' && c <= u'9'; });
}
Time decimal(QStringView value, quint64 scale)
{
    const auto dot = value.indexOf(u'.');
    const auto whole = dot < 0 ? value : value.first(dot);
    const auto fraction = dot < 0 ? QStringView{} : value.sliced(dot + 1);
    if (!digits(whole) || fraction.size() > 18 || (dot >= 0 && !digits(fraction)))
        invalid();
    bool ok = false;
    const auto n = whole.toULongLong(&ok);
    if (!ok || n > quint64(SafeTime) / scale)
        overflow();
    quint64 sub = 0;
    if (!fraction.isEmpty()) {
        sub = fraction.toULongLong(&ok);
        if (!ok)
            invalid();
        for (qsizetype i = fraction.size(); i < 18; ++i)
            sub *= 10;
    }
    const auto low = (sub % Limb) * scale;
    const auto high = (sub / Limb) * scale + low / Limb;
    const auto carry = high / Limb;
    const auto integral = n * scale;
    if (carry > quint64(SafeTime) - integral)
        overflow();
    return checked({qint64(integral + carry), (high % Limb) * Limb + low % Limb});
}
Time time(QStringView value, bool apple)
{
    value = value.trimmed();
    if (value.contains(u':')) {
        const auto parts = value.toString().split(u':');
        if (parts.size() != 3 && !(apple && parts.size() == 2))
            unsupported();
        const auto seconds = decimal(parts.last(), 1000);
        if (seconds >= Time{60000, 0})
            invalid();
        const auto minutes = decimal(parts[parts.size() - 2], 60000);
        if (parts[parts.size() - 2].contains(u'.') ||
            (parts.size() == 3 && minutes >= Time{3600000, 0}))
            invalid();
        Time hours;
        if (parts.size() == 3) {
            if (parts[0].contains(u'.'))
                invalid();
            hours = decimal(parts[0], 3600000);
        }
        return add(add(hours, minutes), seconds);
    }
    for (const auto &[suffix, scale] :
         QList<QPair<QString, quint64>>{{"ms", 1}, {"h", 3600000}, {"m", 60000}, {"s", 1000}})
        if (value.endsWith(suffix))
            return decimal(value.first(value.size() - suffix.size()), scale);
    unsupported();
}
struct Part
{
    int element = -1;
    QString text;
};
struct Node
{
    QString ns, name;
    QMap<QPair<QString, QString>, QString> attributes;
    QList<Part> children;
    const QString *attr(const QString &ns, const QString &name) const
    {
        const auto it = attributes.constFind({ns, name});
        return it == attributes.cend() ? nullptr : &*it;
    }
    bool is(const QString &space, const QString &local) const
    {
        return ns == space && name == local;
    }
};
std::optional<QString> identifier(const QString *value)
{
    if (!value)
        return {};
    if (value->isEmpty() || value->toUtf8().size() > 256 ||
        std::any_of(value->begin(), value->end(), [](QChar c) { return c.isSpace(); }))
        invalid();
    return *value;
}
QList<Node> tree(const QString &text, const std::function<bool()> &canceled)
{
    limit(text.toUtf8().size() <= 4 * 1024 * 1024);
    QXmlStreamReader reader(text);
    QList<Node> nodes;
    QList<int> stack;
    QSet<QString> ids;
    qsizetype events = 0, textUnits = 0;
    const auto budget = [&](QStringView value) {
        textUnits += value.size();
        limit(textUnits <= MaxText);
        for (auto c : value)
            if ((c.unicode() < 32 && c != u'\t' && c != u'\r' && c != u'\n') ||
                c == QChar(0xfffe) || c == QChar(0xffff))
                invalid();
    };
    while (!reader.atEnd()) {
        check(canceled);
        limit(++events <= MaxNodes);
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            limit(stack.size() < 32 && nodes.size() < MaxNodes);
            if (stack.isEmpty() && !nodes.isEmpty())
                invalid();
            Node node{reader.namespaceUri().toString(), reader.name().toString(), {}, {}};
            limit(node.name.toUtf8().size() <= 128 && node.ns.toUtf8().size() <= 256);
            limit(reader.attributes().size() + reader.namespaceDeclarations().size() <= 32);
            for (const auto &declaration : reader.namespaceDeclarations()) {
                limit(declaration.namespaceUri().size() <= MaxLine);
                budget(declaration.namespaceUri());
            }
            for (const auto &attribute : reader.attributes()) {
                const auto ns = attribute.namespaceUri().toString(),
                           key = attribute.name().toString(), value = attribute.value().toString();
                limit(key.toUtf8().size() <= 128 && ns.toUtf8().size() <= 256 &&
                      value.size() <= MaxLine);
                budget(value);
                if (ns == XML && key == "id") {
                    if (!identifier(&value) || ids.contains(value))
                        invalid();
                    ids.insert(value);
                }
                const QPair<QString, QString> identity{ns, key};
                if (node.attributes.contains(identity))
                    invalid();
                node.attributes.insert(identity, value);
            }
            const int index = int(nodes.size());
            if (!stack.isEmpty())
                nodes[stack.last()].children.append(Part{index, {}});
            nodes.append(std::move(node));
            stack.append(index);
        } else if (token == QXmlStreamReader::EndElement) {
            if (stack.isEmpty())
                invalid();
            stack.removeLast();
        } else if (token == QXmlStreamReader::Characters) {
            const auto value = reader.text();
            budget(value);
            if (!stack.isEmpty())
                nodes[stack.last()].children.append({-1, value.toString()});
            else if (!value.trimmed().isEmpty())
                invalid();
        } else if (token == QXmlStreamReader::DTD)
            unsupported();
        else if (token == QXmlStreamReader::EntityReference)
            invalid();
        else if (token == QXmlStreamReader::StartDocument) {
            if ((!reader.documentVersion().isEmpty() && reader.documentVersion() != u"1.0") ||
                (!reader.documentEncoding().isEmpty() &&
                 reader.documentEncoding().compare(u"utf-8", Qt::CaseInsensitive)))
                unsupported();
        }
    }
    if (reader.hasError() || !stack.isEmpty() || nodes.isEmpty() || !nodes[0].is(TT, "tt"))
        invalid();
    return nodes;
}
struct Interval
{
    Time start;
    std::optional<Time> end;
};
Interval interval(const Node &node, Interval parent, bool apple)
{
    if (const auto *container = node.attr({}, "timeContainer"); container && *container != "par")
        unsupported();
    const auto reference = apple ? Time{} : parent.start;
    const auto startValue = node.attr({}, "begin"), endValue = node.attr({}, "end"),
               duration = node.attr({}, "dur");
    auto start = startValue ? add(reference, time(*startValue, apple)) : parent.start;
    auto end = endValue ? std::optional<Time>(add(reference, time(*endValue, apple))) : parent.end;
    if (duration) {
        const auto until = add(start, time(*duration, apple));
        end = end ? std::min(*end, until) : until;
    }
    if (end && *end < start)
        invalid();
    start = std::max(start, parent.start);
    if (parent.end)
        end = end ? std::min(*end, *parent.end) : parent.end;
    if (end && *end < start)
        invalid();
    return {start, end};
}
bool preserve(const Node &node, bool inherited)
{
    const auto *value = node.attr(XML, "space");
    if (!value)
        return inherited;
    if (*value == "preserve")
        return true;
    if (*value == "default")
        return false;
    invalid();
}
QString plain(const QList<Node> &nodes, int index)
{
    QString result;
    for (const auto &part : nodes[index].children) {
        if (part.element < 0)
            result += part.text;
        else if (nodes[part.element].is(TT, "br"))
            result += '\n';
        else
            result += plain(nodes, part.element);
    }
    return result;
}
QString normalized(const QString &text, bool keep)
{
    if (keep)
        return text;
    QString result;
    bool space = false;
    for (auto c : text) {
        if (c == u' ' || c == u'\t' || c == u'\r' || c == u'\n') {
            if (!space)
                result += ' ';
            space = true;
        } else {
            result += c;
            space = false;
        }
    }
    return result;
}
QString trimmedSpaces(QStringView text)
{
    while (!text.isEmpty() && text.front() == u' ')
        text = text.sliced(1);
    while (!text.isEmpty() && text.back() == u' ')
        text = text.first(text.size() - 1);
    return text.toString();
}
QStringList specialRoles(const Node &node)
{
    QStringList result;
    const auto *value = node.attr(TTM, "role");
    if (!value)
        return result;
    QString token;
    const auto flush = [&] {
        if (QStringList{"x-bg", "x-translation", "x-roman"}.contains(token))
            result.append(token);
        token.clear();
    };
    for (auto c : *value) {
        if (c.isSpace())
            flush();
        else
            token += c;
    }
    flush();
    return result;
}
struct Chunk
{
    QString text;
    bool keep;
};
struct Body
{
    QString text;
    qsizetype textUnits = 0;
    QList<Chunk> chunks;
    QJsonArray words, vocals, alternate;
    void append(const QString &value, bool keep)
    {
        textUnits += value.size();
        limit(textUnits <= MaxLine);
        for (auto c : value)
            if (keep || c != u' ' || !text.endsWith(u' '))
                text += c;
        chunks.append({value, keep});
    }
    QString first(const QString &role) const
    {
        for (const auto &entry : alternate)
            if (entry.toObject()["role"] == role)
                return entry.toObject()["text"].toString();
        return {};
    }
};
void optional(QJsonObject &object, const char *key, const std::optional<QString> &value)
{
    if (value)
        object.insert(key, *value);
}
void appendArray(QJsonArray &target, const QJsonArray &source)
{
    for (const auto &value : source)
        target.append(value);
}
class Parser
{
public:
    const QList<Node> &nodes;
    bool apple;
    const std::function<bool()> &canceled;
    qsizetype pieces = 0, blocks = 0;
    QJsonArray lines{};
    QJsonObject agents{};
    Body alternate(int index, bool keep)
    {
        const auto &node = nodes[index];
        keep = preserve(node, keep);
        Body result;
        for (const auto &part : node.children) {
            check(canceled);
            if (part.element < 0)
                result.append(normalized(part.text, keep), keep);
            else {
                const auto &child = nodes[part.element];
                if (child.is(TT, "br")) {
                    result.append("\n", true);
                    continue;
                }
                if (!child.is(TT, "span") || !specialRoles(child).isEmpty() ||
                    child.attr(STYLING, "ruby"))
                    unsupported();
                const auto body = alternate(part.element, keep);
                for (const auto &chunk : body.chunks)
                    result.append(chunk.text, chunk.keep);
            }
        }
        return result;
    }
    Body content(int index, Interval bounds, bool keep, bool vocal)
    {
        const auto &node = nodes[index];
        keep = preserve(node, keep);
        Body result;
        for (const auto &part : node.children) {
            check(canceled);
            if (part.element < 0)
                result.append(normalized(part.text, keep), keep);
            else {
                const auto &child = nodes[part.element];
                if (child.is(TT, "br")) {
                    result.append("\n", true);
                    continue;
                }
                if (!child.is(TT, "span") || child.attr(STYLING, "ruby"))
                    unsupported();
                const auto roles = specialRoles(child);
                if (roles.size() > 1)
                    unsupported();
                const auto role = roles.value(0);
                if (role == "x-translation" || role == "x-roman") {
                    const auto childKeep = preserve(child, keep);
                    auto text = alternate(part.element, keep).text;
                    if (!childKeep)
                        text = trimmedSpaces(text);
                    if (!text.trimmed().isEmpty()) {
                        QJsonObject value{
                            {"role", role == "x-roman" ? "romanization" : "translation"},
                            {"text", text}};
                        optional(value, "lang", identifier(child.attr(XML, "lang")));
                        result.alternate.append(value);
                        limit(result.alternate.size() <= 64);
                    }
                    continue;
                }
                const auto childBounds = interval(child, bounds, apple);
                auto body = content(part.element, childBounds, keep, role == "x-bg" || vocal);
                if (role == "x-bg") {
                    if (vocal || !body.vocals.isEmpty())
                        unsupported();
                    if (!childBounds.end)
                        unsupported();
                    if (!preserve(child, keep))
                        body.text = trimmedSpaces(body.text);
                    QJsonObject value{
                        {"start_ms", millis(childBounds.start)},
                        {"duration_ms", millis(*childBounds.end) - millis(childBounds.start)},
                        {"text", body.text},
                        {"words", body.words},
                        {"translation", body.first("translation")},
                        {"romanization", body.first("romanization")}};
                    optional(value, "agent_id", identifier(child.attr(TTM, "agent")));
                    if (!body.alternate.isEmpty())
                        value.insert("alternate_texts", body.alternate);
                    result.vocals.append(value);
                } else {
                    if (body.words.isEmpty() && !body.text.isEmpty() &&
                        (child.attr({}, "begin") || child.attr({}, "end") ||
                         child.attr({}, "dur"))) {
                        if (!childBounds.end)
                            unsupported();
                        limit(++pieces <= 65536);
                        body.words.append(QJsonObject{
                            {"start_ms", millis(childBounds.start)},
                            {"duration_ms", millis(*childBounds.end) - millis(childBounds.start)},
                            {"text", body.text}});
                    }
                    for (const auto &chunk : body.chunks)
                        result.append(chunk.text, chunk.keep);
                    appendArray(result.words, body.words);
                    appendArray(result.vocals, body.vocals);
                    appendArray(result.alternate, body.alternate);
                }
            }
            limit(result.vocals.size() <= 8 && result.alternate.size() <= 64);
        }
        return result;
    }
    void walk(int index, Interval parent, bool keep, std::optional<QString> part = {},
              std::optional<qint64> block = {})
    {
        check(canceled);
        const auto &node = nodes[index];
        if (node.ns != TT)
            unsupported();
        const auto bounds = interval(node, parent, apple);
        keep = preserve(node, keep);
        if (node.name == "div") {
            block = ++blocks;
            const auto *songPart = node.attr(APPLE, "songPart");
            if (!songPart)
                songPart = node.attr(APPLE, "song-part");
            if (songPart) {
                limit(songPart->size() <= 256);
                part = *songPart;
            }
        }
        if (node.name == "p") {
            if (!specialRoles(node).isEmpty())
                unsupported();
            limit(lines.size() < 8192);
            if (!bounds.end)
                unsupported();
            auto body = content(index, bounds, keep, false);
            if (!keep)
                body.text = trimmedSpaces(body.text);
            QJsonObject line{{"start_ms", millis(bounds.start)},
                             {"duration_ms", millis(*bounds.end) - millis(bounds.start)},
                             {"text", body.text},
                             {"words", body.words},
                             {"translation", body.first("translation")},
                             {"romanization", body.first("romanization")},
                             {"romanization_segments", QJsonArray{}}};
            optional(line, "id", identifier(node.attr(XML, "id")));
            optional(line, "agent_id", identifier(node.attr(TTM, "agent")));
            optional(line, "song_part", part);
            if (part && part->trimmed().compare("chorus", Qt::CaseInsensitive) == 0)
                line.insert("is_chorus", true);
            if (block)
                line.insert("block_index", *block);
            if (!body.vocals.isEmpty())
                line.insert("background_vocals", body.vocals);
            if (!body.alternate.isEmpty())
                line.insert("alternate_texts", body.alternate);
            lines.append(line);
            return;
        }
        if (node.name != "body" && node.name != "div")
            unsupported();
        for (const auto &child : node.children) {
            if (child.element >= 0)
                walk(child.element, bounds, keep, part, block);
            else if (!child.text.trimmed().isEmpty())
                unsupported();
        }
    }
    QJsonObject parse()
    {
        const auto &root = nodes[0];
        if (const auto *base = root.attr(TTP, "timeBase"); base && *base != "media")
            unsupported();
        for (const auto &key :
             {"frameRate", "frameRateMultiplier", "subFrameRate", "tickRate", "clockMode"})
            if (root.attr(TTP, key))
                unsupported();
        for (const auto &node : nodes) {
            check(canceled);
            if (node.ns == TT)
                for (const auto &key :
                     {"repeatCount", "repeatDur", "fill", "restart", "speed", "autoReverse"})
                    if (node.attr({}, key))
                        unsupported();
            if (const auto *container = node.attr({}, "timeContainer");
                container && *container != "par")
                unsupported();
            if (node.is(TTM, "agent")) {
                limit(agents.size() < 64);
                const auto id = identifier(node.attr(XML, "id"));
                if (!id)
                    invalid();
                QString name;
                for (const auto &part : node.children)
                    if (part.element >= 0 && nodes[part.element].is(TTM, "name")) {
                        name = plain(nodes, part.element);
                        break;
                    }
                limit(name.size() <= 256);
                QJsonObject value{{"id", *id}, {"name", name}};
                optional(value, "type", identifier(node.attr({}, "type")));
                if (agents.contains(*id))
                    invalid();
                agents.insert(*id, value);
            }
            if (node.ns == APPLE &&
                QStringList{"translations", "transliterations"}.contains(node.name))
                unsupported();
        }
        int body = -1;
        for (const auto &part : root.children)
            if (part.element >= 0) {
                if (nodes[part.element].is(TT, "body")) {
                    if (body >= 0)
                        invalid();
                    body = part.element;
                } else if (!nodes[part.element].is(TT, "head"))
                    unsupported();
            }
        if (body < 0)
            invalid();
        walk(body, {{}, {}}, preserve(root, false));
        QList<QJsonObject> sorted;
        sorted.reserve(lines.size());
        for (const auto &line : lines)
            sorted.append(line.toObject());
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const QJsonObject &a, const QJsonObject &b) {
                             return a["start_ms"].toInteger() < b["start_ms"].toInteger();
                         });
        lines = {};
        for (const auto &line : sorted)
            lines.append(line);
        QJsonObject value{{"scrollable", true},
                          {"offset_ms", 0},
                          {"lines", lines},
                          {"source_format", "ttml"},
                          {"timing_profile", apple ? "apple-lyrics-absolute" : "ttml-media"}};
        if (!agents.isEmpty())
            value.insert("agents", agents);
        limit(QJsonDocument(value).toJson(QJsonDocument::Compact).size() <= 16 * 1024 * 1024);
        return value;
    }
};
} // namespace
bool isTtml(QStringView text)
{
    while (!text.isEmpty() && (text.front().isSpace() || text.front() == QChar(0xfeff)))
        text = text.sliced(1);
    return text.startsWith(u"<?xml") || text.startsWith(u"<tt") ||
           (text.startsWith(u'<') && text.contains(TT));
}
LyricParser::Result parseTtml(const QString &source, const std::function<bool()> &canceled)
{
    try {
        QStringView text(source);
        while (!text.isEmpty() && (text.front().isSpace() || text.front() == QChar(0xfeff)))
            text = text.sliced(1);
        while (!text.isEmpty() && (text.back().isSpace() || text.back() == QChar(0xfeff)))
            text = text.first(text.size() - 1);
        const auto nodes = tree(text.toString(), canceled);
        bool apple = false;
        if (const auto *timing = nodes[0].attr(APPLE, "timing")) {
            if (*timing != "Word" && *timing != "Line")
                unsupported();
            apple = true;
        }
        return {Parser{nodes, apple, canceled}.parse(), {}, {}};
    } catch (const Failure &error) {
        return {{}, error.message, error.code};
    } catch (const std::bad_alloc &) {
        return {{}, "歌词超过解析资源预算", "resource_limit"};
    }
}
} // namespace EchoMusic::LyricCodecs
