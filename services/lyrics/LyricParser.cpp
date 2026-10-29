#include "LyricParser.h"
#include "TtmlParser_p.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QPointer>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QSet>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <optional>
#include <zlib.h>
namespace EchoMusic
{
namespace
{
constexpr qint64 MaxTime = 9007199254740991LL;
constexpr qsizetype MaxBytes = 4 * 1024 * 1024, MaxRows = 20000, MaxWords = 100000;
struct Failure
{
    QString code, message;
};
[[noreturn]] void fail(const char *code = "decode", const char *message = "无法解析歌词数据")
{
    throw Failure{QString::fromLatin1(code), QString::fromUtf8(message)};
}
void limit(bool valid)
{
    if (!valid)
        fail("resource_limit", "歌词超过解析资源预算");
}
qint64 number(QStringView value, bool signedValue = false)
{
    bool ok = false;
    const auto n = value.toLongLong(&ok);
    if (!ok) {
        static const QRegularExpression integer("^[+-]?[0-9]+$");
        if (!integer.matchView(value).hasMatch())
            fail();
    }
    if (!ok || (!signedValue && n < 0) || n < -MaxTime || n > MaxTime)
        fail("numeric_overflow", "歌词时间超出可表示范围");
    return n;
}
qint64 sum(qint64 a, qint64 b)
{
    if (b < 0 || a < 0 || a > MaxTime - b)
        fail("numeric_overflow", "歌词时间超出可表示范围");
    return a + b;
}
QString utf8(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder(bytes);
    if (decoder.hasError())
        fail();
    return text;
}
QByteArray base64(const QString &text)
{
    const auto result =
        QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!result || text.toLatin1().size() != text.size())
        fail();
    return result.decoded;
}
struct Word
{
    qint64 start = 0, duration = 0;
    QString text;
};
struct Line
{
    qint64 start = 0, duration = 0;
    QString text, translation, romanization;
    QStringList segments;
    QList<Word> words;
};
struct Lyrics
{
    bool scrollable = true;
    qint64 offset = 0;
    QList<Line> lines;
};
QJsonObject json(const Lyrics &value);
QJsonObject ttml(const QString &text, const std::atomic_bool *canceled)
{
    const auto result =
        LyricCodecs::parseTtml(text, [canceled] { return canceled && canceled->load(); });
    if (!result.error.isEmpty())
        throw Failure{result.code, result.error};
    return result.lyrics;
}
const QRegularExpression &lrcStamp()
{
    static const QRegularExpression value("^\\[(\\d+):(\\d+(?:\\.\\d+)?)\\]");
    return value;
}
const QRegularExpression &enhancedTag()
{
    static const QRegularExpression value("<(\\d+):(\\d+(?:\\.\\d+)?)>");
    return value;
}
const QRegularExpression &krcLine()
{
    static const QRegularExpression value("^\\[(\\d+),(\\d+)\\](.*)$");
    return value;
}
QString compact(QString value)
{
    value.remove(enhancedTag());
    static const QRegularExpression whitespace("\\s");
    value.remove(whitespace);
    return value;
}
qint64 stamp(QStringView minutes, QStringView seconds)
{
    bool a = false, b = false;
    const double time = (minutes.toDouble(&a) * 60 + seconds.toDouble(&b)) * 1000;
    if (!a || !b || !std::isfinite(time) || time < 0 || time > double(MaxTime))
        fail("numeric_overflow", "歌词时间超出可表示范围");
    return qint64(std::round(time));
}
struct Schedule
{
    QList<qint64> times;
    QString body;
};
Schedule schedule(const QString &text)
{
    QStringView source(text);
    Schedule out;
    QSet<qint64> seen;
    while (true) {
        auto match = lrcStamp().matchView(source);
        if (!match.hasMatch())
            break;
        const auto at = stamp(match.capturedView(1), match.capturedView(2));
        if (!seen.contains(at)) {
            seen.insert(at);
            out.times.append(at);
            limit(out.times.size() <= MaxRows);
        }
        source = source.sliced(match.capturedLength());
    }
    out.body = source.toString();
    return out;
}
bool paired(const QList<qint64> &a, const QString &x, const QList<qint64> &b, const QString &y)
{
    if (a.size() != b.size() || x == y)
        return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > 250)
            return false;
    return true;
}
class Codec
{
    const std::atomic_bool *canceled;
    qsizetype words = 0;
    void check() const
    {
        if (canceled && canceled->load())
            fail("Canceled", "歌词解析已取消");
    }
    void budget(const Lyrics &value) const
    {
        limit(value.lines.size() <= MaxRows);
    }
    QJsonArray language;
    bool languageTag(const QString &source)
    {
        if (!source.startsWith("[language:") || !source.endsWith(']'))
            return false;
        const auto encoded = source.mid(10, source.size() - 11);
        limit(encoded.size() <= 2 * 1024 * 1024);
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(base64(encoded), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            fail();
        const auto value = doc.object().value("content");
        if (!value.isUndefined() && !value.isNull() && !value.isArray())
            fail();
        language = value.toArray();
        return true;
    }
    void enrich(QList<Line> &lines)
    {
        for (const auto &entry : language) {
            check();
            if (!entry.isObject())
                fail();
            const auto content = entry.toObject();
            const auto optional = [](QJsonValue value) -> std::optional<quint32> {
                bool ok = false;
                quint64 n = 0;
                if (value.isString())
                    n = value.toString().toULongLong(&ok);
                else {
                    const auto number = value.toDouble(-1);
                    ok = value.isDouble() && std::isfinite(number) && number >= 0 &&
                         number <= double(std::numeric_limits<quint32>::max()) &&
                         std::trunc(number) == number;
                    if (ok)
                        n = quint64(number);
                }
                if (!ok || n > std::numeric_limits<quint32>::max())
                    return {};
                return quint32(n);
            };
            const auto lang = optional(content.value("language")),
                       kind = optional(content.value("type"));
            if ((lang && *lang != 0) || !kind || *kind > 1)
                continue;
            const auto rows = content.value("lyricContent");
            if (!rows.isNull() && !rows.isUndefined() && !rows.isArray())
                fail();
            const auto array = rows.toArray();
            for (qsizetype i = 0; i < std::min(lines.size(), array.size()); ++i) {
                if (!array[i].isArray())
                    fail();
                QStringList segments;
                for (const auto &v : array[i].toArray()) {
                    if (!v.isString())
                        fail();
                    segments.append(v.toString());
                }
                const auto text = segments.join("").trimmed();
                if (*kind == 1) {
                    if (!text.isEmpty())
                        lines[i].translation = text;
                } else {
                    lines[i].romanization = text;
                    lines[i].segments = segments;
                }
            }
        }
    }
    QList<Word> enhanced(const QString &body, qint64 start)
    {
        struct Tag
        {
            qsizetype begin, end;
            qint64 at;
        };
        QList<Tag> tags;
        auto matches = enhancedTag().globalMatch(body);
        while (matches.hasNext()) {
            check();
            auto m = matches.next();
            tags.append(
                {m.capturedStart(), m.capturedEnd(), stamp(m.capturedView(1), m.capturedView(2))});
            limit(tags.size() <= MaxWords + 1);
        }
        if (tags.isEmpty())
            return {};
        if (!body.left(tags[0].begin).trimmed().isEmpty())
            tags.prepend({0, 0, start});
        QList<Word> result;
        for (qsizetype i = 0; i < tags.size(); ++i) {
            const auto &tag = tags[i];
            const auto text = body.mid(
                tag.end, (i + 1 < tags.size() ? tags[i + 1].begin : body.size()) - tag.end);
            if (text.isEmpty())
                continue;
            const auto end = i + 1 < tags.size() ? tags[i + 1].at : sum(tag.at, 3000);
            result.append({tag.at, std::max<qint64>(1, end - tag.at), text});
            limit(++words <= MaxWords);
        }
        return result;
    }
    bool pairLayout(const QString &text)
    {
        qsizetype entries = 0, pairs = 0;
        std::optional<Schedule> pending;
        for (const auto source : QStringView(text).tokenize(u'\n')) {
            check();
            auto row = schedule(source.trimmed().toString());
            const auto body = compact(row.body);
            if (row.times.isEmpty() || body.isEmpty()) {
                pending.reset();
                continue;
            }
            ++entries;
            if (pending && paired(pending->times, compact(pending->body), row.times, body)) {
                ++pairs;
                pending.reset();
            } else
                pending = std::move(row);
        }
        return pairs >= 2 &&
               entries - 2 * pairs <=
                   std::max<qsizetype>(2, qsizetype(std::round(double(entries) * .3)));
    }
    Line krcRow(const QRegularExpressionMatch &match)
    {
        Line row;
        row.start = number(match.capturedView(1));
        row.duration = number(match.capturedView(2));
        sum(row.start, row.duration);
        static const QRegularExpression pattern("<(\\d+),(\\d+),\\d+>([^<]*)");
        auto tags = pattern.globalMatch(match.captured(3));
        while (tags.hasNext()) {
            check();
            auto w = tags.next();
            const auto start = sum(row.start, number(w.capturedView(1))),
                       duration = number(w.capturedView(2));
            sum(start, duration);
            row.words.append({start, duration, w.captured(3)});
            row.text += w.captured(3);
            limit(++words <= MaxWords);
        }
        if (row.words.isEmpty())
            row.text = match.captured(3);
        return row;
    }
    Lyrics lrc(const QString &text, bool scrollable, bool embedded, bool mixed)
    {
        limit(text.toUtf8().size() <= MaxBytes);
        const bool pairing = embedded && scrollable && pairLayout(text);
        Lyrics result;
        result.scrollable = scrollable;
        QList<Line> rows;
        QList<QList<qint64>> schedules;
        std::optional<qsizetype> pending;
        for (const auto raw : QStringView(text).tokenize(u'\n')) {
            check();
            const auto source = raw.trimmed().toString();
            if (source.isEmpty()) {
                pending.reset();
                continue;
            }
            if (source.startsWith("[offset:") && source.endsWith(']')) {
                result.offset = number(QStringView(source).mid(8, source.size() - 9), true);
                pending.reset();
                continue;
            }
            if (scrollable && languageTag(source)) {
                pending.reset();
                continue;
            }
            if (!scrollable) {
                Line row;
                row.text = source;
                result.lines.append(row);
                budget(result);
                continue;
            }
            if (source.startsWith('[') && source.section(']', 0, 0).contains(',')) {
                pending.reset();
                if (mixed) {
                    auto m = krcLine().match(source);
                    if (m.hasMatch()) {
                        auto row = krcRow(m);
                        schedules.append({row.start});
                        rows.append(std::move(row));
                        limit(rows.size() <= MaxRows);
                    }
                }
                continue;
            }
            auto plan = schedule(source);
            if (plan.times.isEmpty()) {
                pending.reset();
                continue;
            }
            const auto content = plan.body.trimmed();
            if (compact(content).isEmpty()) {
                pending.reset();
                continue;
            }
            if (pairing && pending) {
                const auto i = *pending;
                pending.reset();
                if (paired(schedules[i], compact(rows[i].text), plan.times, compact(content))) {
                    auto translated = content;
                    translated.remove(enhancedTag());
                    rows[i].translation = translated.trimmed();
                    continue;
                }
            }
            Line row;
            row.words = enhanced(content, plan.times.first());
            row.text = content;
            if (!row.words.isEmpty()) {
                row.text.clear();
                for (const auto &word : row.words)
                    row.text += word.text;
                row.start = row.words.first().start;
                const auto &last = row.words.last();
                row.duration = sum(std::max<qint64>(0, last.start - row.start), last.duration);
            } else
                row.start = plan.times.first();
            rows.append(std::move(row));
            schedules.append(std::move(plan.times));
            pending = rows.size() - 1;
            limit(rows.size() <= MaxRows);
        }
        enrich(rows);
        qsizetype count = 0, totalWords = 0, totalBytes = 0;
        for (qsizetype i = 0; i < rows.size(); ++i) {
            check();
            const auto &row = rows[i];
            const auto &times = schedules[i];
            const auto size = times.size();
            qsizetype bytes = row.text.toUtf8().size() + row.translation.toUtf8().size() +
                              row.romanization.toUtf8().size();
            for (const auto &word : row.words)
                bytes += word.text.toUtf8().size();
            for (const auto &part : row.segments)
                bytes += part.toUtf8().size();
            count += size;
            totalWords += size * row.words.size();
            totalBytes += size * bytes;
            limit(count <= MaxRows && totalWords <= MaxWords && totalBytes <= 16 * 1024 * 1024);
            for (auto time : times) {
                auto repeat = row;
                const auto delta = time - times.first();
                auto shift = [delta](qint64 n) {
                    if (n + delta < 0 || n + delta > MaxTime)
                        fail("numeric_overflow", "歌词时间超出可表示范围");
                    return n + delta;
                };
                repeat.start = shift(repeat.start);
                for (auto &w : repeat.words) {
                    w.start = shift(w.start);
                    sum(w.start, w.duration);
                }
                result.lines.append(std::move(repeat));
            }
        }
        if (scrollable) {
            std::stable_sort(result.lines.begin(), result.lines.end(),
                             [](const auto &a, const auto &b) { return a.start < b.start; });
            for (qsizetype i = 0; i + 1 < result.lines.size(); ++i)
                if (result.lines[i].words.isEmpty() && result.lines[i].duration == 0)
                    result.lines[i].duration = result.lines[i + 1].start - result.lines[i].start;
        }
        return result;
    }
    Lyrics krc(const QString &text)
    {
        limit(text.toUtf8().size() <= MaxBytes);
        Lyrics result;
        for (const auto raw : QStringView(text).tokenize(u'\n')) {
            check();
            auto source = raw.toString();
            if (source.endsWith('\r'))
                source.chop(1);
            if (languageTag(source))
                continue;
            if (source.startsWith("[offset:") && source.endsWith(']')) {
                result.offset = number(QStringView(source).mid(8, source.size() - 9), true);
                continue;
            }
            auto m = krcLine().match(source);
            if (!m.hasMatch())
                continue;
            result.lines.append(krcRow(m));
            budget(result);
        }
        enrich(result.lines);
        std::stable_sort(result.lines.begin(), result.lines.end(),
                         [](const auto &a, const auto &b) { return a.start < b.start; });
        return result;
    }
    Lyrics yrc(const QString &text)
    {
        static const QRegularExpression tag("\\((\\d+),(\\d+),\\d+\\)");
        Lyrics result;
        for (const auto raw : QStringView(text).tokenize(u'\n')) {
            check();
            auto source = raw.toString();
            if (source.endsWith('\r'))
                source.chop(1);
            if (source.startsWith("[offset:") && source.endsWith(']')) {
                result.offset = number(QStringView(source).mid(8, source.size() - 9), true);
                continue;
            }
            auto m = krcLine().match(source);
            if (!m.hasMatch())
                continue;
            Line row;
            row.start = number(m.capturedView(1));
            row.duration = number(m.capturedView(2));
            sum(row.start, row.duration);
            const auto body = m.captured(3);
            QList<QRegularExpressionMatch> tags;
            auto matches = tag.globalMatch(body);
            while (matches.hasNext()) {
                tags.append(matches.next());
                limit(tags.size() <= MaxWords);
            }
            if (!tags.isEmpty() && tags.first().capturedStart() > 0)
                row.words.append({row.start, 0, body.left(tags.first().capturedStart())});
            for (qsizetype i = 0; i < tags.size(); ++i) {
                auto w = tags[i];
                const auto start = number(w.capturedView(1)), duration = number(w.capturedView(2));
                sum(start, duration);
                const auto end = i + 1 < tags.size() ? tags[i + 1].capturedStart() : body.size();
                row.words.append(
                    {start, duration, body.mid(w.capturedEnd(), end - w.capturedEnd())});
                limit(++words <= MaxWords);
            }
            if (row.words.isEmpty())
                row.text = body;
            else
                for (const auto &word : row.words)
                    row.text += word.text;
            result.lines.append(std::move(row));
            budget(result);
        }
        std::stable_sort(result.lines.begin(), result.lines.end(),
                         [](const auto &a, const auto &b) { return a.start < b.start; });
        return result;
    }
    QString unpackKrc(const QString &encoded)
    {
        limit(encoded.size() <= MaxBytes);
        auto packed = base64(encoded);
        if (!packed.startsWith("krc1"))
            fail();
        packed.remove(0, 4);
        static const unsigned char key[] = {0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
                                            0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
        for (qsizetype i = 0; i < packed.size(); ++i)
            packed[i] = char(uchar(packed[i]) ^ key[i % 16]);
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef *>(packed.data());
        stream.avail_in = uInt(packed.size());
        if (inflateInit(&stream) != Z_OK)
            fail();
        struct End
        {
            z_stream *stream;
            ~End()
            {
                inflateEnd(stream);
            }
        } end{&stream};
        QByteArray bytes;
        char buffer[16384];
        int status;
        do {
            check();
            stream.next_out = reinterpret_cast<Bytef *>(buffer);
            stream.avail_out = sizeof(buffer);
            status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END)
                fail();
            limit(bytes.size() + qsizetype(sizeof(buffer) - stream.avail_out) <= MaxBytes);
            bytes.append(buffer, qsizetype(sizeof(buffer) - stream.avail_out));
        } while (status != Z_STREAM_END);
        return utf8(bytes);
    }
    QString plainWithoutHeaders(const QString &text, bool authored)
    {
        static const QRegularExpression embedded(
            "^\\[(?:id|ar|ti|al|by|offset|hash|language|kana):",
            QRegularExpression::CaseInsensitiveOption),
            document("^\\[(?:id|ar|ti|al|by|offset|hash|language|length):",
                     QRegularExpression::CaseInsensitiveOption);
        QString out;
        for (const auto line : QStringView(text).tokenize(u'\n')) {
            check();
            if (!(authored ? document : embedded).matchView(line.trimmed()).hasMatch()) {
                if (!out.isEmpty())
                    out += '\n';
                out += line;
            }
        }
        return out;
    }

    QJsonObject companions(QJsonObject result, const QVariantMap &input)
    {
        qsizetype companion = 0;
        for (const auto &key : {QStringLiteral("translation"), QStringLiteral("romanization")}) {
            if (!input.contains(key))
                continue;
            language = {};
            const auto auxiliary = lrc(input.value(key).toString(), true, false, false);
            QMap<qint64, QString> byTime;
            for (const auto &line : auxiliary.lines) {
                auto &value = byTime[line.start];
                if (!value.isEmpty())
                    value += '\n';
                value += line.text;
            }
            auto lines = result.value("lines").toArray();
            for (qsizetype index = 0; index < lines.size(); ++index) {
                check();
                auto line = lines[index].toObject();
                const auto value = byTime.constFind(line.value("start_ms").toInteger());
                if (value == byTime.cend())
                    continue;
                companion += value->toUtf8().size();
                limit(companion <= MaxBytes);
                line.insert(key, *value);
                if (key == "romanization")
                    line.insert("romanization_segments", QJsonArray{});
                lines[index] = line;
            }
            result.insert("lines", lines);
        }
        return result;
    }

public:
    explicit Codec(const std::atomic_bool *cancel) : canceled(cancel) {}
    QJsonObject parse(LyricParser::Source source, const QVariantMap &input)
    {
        check();
        language = {};
        words = 0;
        QString text = input.value("content").toString();
        limit(text.toUtf8().size() <= MaxBytes);
        if (source == LyricParser::Source::Embedded) {
            QStringView trimmed(text);
            const auto edge = [](QChar c) {
                return c.isSpace() || c == QChar(0) || c == QChar(0xfeff);
            };
            while (!trimmed.isEmpty() && edge(trimmed.front()))
                trimmed = trimmed.sliced(1);
            while (!trimmed.isEmpty() && edge(trimmed.back()))
                trimmed = trimmed.first(trimmed.size() - 1);
            text = trimmed.toString();
            if (LyricCodecs::isTtml(text))
                return ttml(text, canceled);
            auto result = lrc(text, true, true, true);
            if (!result.lines.isEmpty())
                return json(result);
            return json(lrc(plainWithoutHeaders(text, false), false, false, false));
        }
        const auto format = input.value("format").toString(),
                   encoding = input.value("encoding", "text").toString();
        if (source == LyricParser::Source::Document) {
            const QStringList keys{"format", "content",     "encoding",
                                   "source", "translation", "romanization"};
            for (auto i = input.begin(); i != input.end(); ++i) {
                if (!keys.contains(i.key()) || i.value().metaType().id() != QMetaType::QString)
                    fail("input", "无效的歌词文档字段");
                limit(i.value().toString().toUtf8().size() <=
                      (i.key() == "source" ? 1024 : 1024 * 1024));
            }
            if (!input.contains("content"))
                fail("input", "缺少歌词内容");
        }
        if (encoding == "base64") {
            if (format == "krc")
                text = unpackKrc(text);
            else if (source == LyricParser::Source::Downloaded &&
                     (format == "lrc" || format == "text"))
                text = utf8(base64(text));
            else
                fail("input", "不支持的歌词编码");
        } else if (encoding != "text")
            fail("input", "不支持的歌词编码");
        qsizetype bom = 0;
        while (bom < text.size() && text[bom] == QChar(0xfeff))
            ++bom;
        if (bom)
            text = text.sliced(bom);
        if (format == "ttml")
            return companions(ttml(text, canceled), input);
        Lyrics result;
        if (format == "krc")
            result = krc(text);
        else if (format == "yrc")
            result = yrc(text);
        else if (format == "lrc") {
            const bool authored = source == LyricParser::Source::Document;
            result = lrc(text, true, authored, false);
            if (authored && result.lines.isEmpty() && !text.trimmed().isEmpty()) {
                const auto offset = result.offset;
                result = lrc(plainWithoutHeaders(text, true), false, false, false);
                result.offset = offset;
            }
        } else if (format == "text" && source == LyricParser::Source::Downloaded)
            result = lrc(text, false, false, false);
        else
            fail("input", "不支持的歌词格式");
        if (source == LyricParser::Source::Document && format != "lrc" &&
            !text.trimmed().isEmpty() && result.lines.isEmpty())
            fail("input", "歌词文档没有有效内容");
        return companions(json(result), input);
    }
};
QJsonObject json(const Lyrics &value)
{
    QJsonArray lines;
    qsizetype words = 0, bytes = 0;
    limit(value.lines.size() <= MaxRows);
    for (const auto &line : value.lines) {
        sum(line.start, line.duration);
        QJsonArray wordRows, segments;
        words += line.words.size();
        bytes += line.text.toUtf8().size() + line.translation.toUtf8().size() +
                 line.romanization.toUtf8().size();
        for (const auto &word : line.words) {
            sum(word.start, word.duration);
            bytes += word.text.toUtf8().size();
            wordRows.append(QJsonObject{
                {"start_ms", word.start}, {"duration_ms", word.duration}, {"text", word.text}});
        }
        for (const auto &segment : line.segments) {
            bytes += segment.toUtf8().size();
            segments.append(segment);
        }
        limit(words <= MaxWords && bytes <= 16 * 1024 * 1024);
        lines.append(QJsonObject{{"start_ms", line.start},
                                 {"duration_ms", line.duration},
                                 {"text", line.text},
                                 {"words", wordRows},
                                 {"translation", line.translation},
                                 {"romanization", line.romanization},
                                 {"romanization_segments", segments}});
    }
    return {{"scrollable", value.scrollable}, {"offset_ms", value.offset}, {"lines", lines}};
}
class Lifetime final : public QObject
{
public:
    std::shared_ptr<std::atomic_bool> canceled;
    Lifetime(QObject *owner, std::shared_ptr<std::atomic_bool> canceled)
        : QObject(owner), canceled(std::move(canceled))
    {
    }
    ~Lifetime() override
    {
        canceled->store(true);
    }
};
} // namespace
LyricParser::Result LyricParser::parse(Source source, const QVariantMap &input,
                                       const std::atomic_bool *canceled)
{
    try {
        Codec codec(canceled);
        return {codec.parse(source, input), {}, {}};
    } catch (const Failure &error) {
        return {{}, error.message, error.code};
    } catch (const std::bad_alloc &) {
        return {{}, QStringLiteral("歌词超过解析资源预算"), QStringLiteral("resource_limit")};
    }
}
LyricParser::LyricParser(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(10000);
}
LyricParser::~LyricParser()
{
    for (const auto &cancel : m_pending)
        cancel->store(true);
    m_pool.waitForDone();
}
QObject *LyricParser::submit(Source source, QVariantMap input, QObject *owner, Done done)
{
    auto canceled = std::make_shared<std::atomic_bool>(false);
    auto *job = new Lifetime(owner, canceled);
    const QPointer<QObject> guard(job);
    if (m_pending.size() >= 8) {
        QTimer::singleShot(0, job, [guard, done = std::move(done)] {
            if (guard) {
                guard->deleteLater();
                done({{}, QStringLiteral("歌词解析器忙碌"), QStringLiteral("Busy")});
            }
        });
        return job;
    }
    m_pending.append(canceled);
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this,
            [this, watcher, guard, canceled, done = std::move(done)] {
                m_pending.removeOne(canceled);
                auto result = watcher->result();
                watcher->deleteLater();
                if (!guard || canceled->load())
                    return;
                guard->deleteLater();
                done(std::move(result));
            });
    watcher->setFuture(QtConcurrent::run(&m_pool, [source, input = std::move(input), canceled] {
        return parse(source, input, canceled.get());
    }));
    return job;
}
} // namespace EchoMusic
