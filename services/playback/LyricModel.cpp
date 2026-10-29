#include "LyricModel.h"
#include <QJsonObject>
#include <algorithm>
#include <QTextBoundaryFinder>
#include <cmath>
#include <limits>
namespace
{
bool timestamp(const QJsonValue &v, bool signedValue = false)
{
    const double n = v.toDouble(std::numeric_limits<double>::quiet_NaN());
    return v.isDouble() && std::isfinite(n) && std::trunc(n) == n &&
           std::abs(n) <= 9007199254740991. && (signedValue || n >= 0);
}
struct MetadataBudget
{
    qsizetype units = 0;
    qsizetype words = 0;
    bool text(const QJsonValue &value, qsizetype maximum = 16384)
    {
        if (!value.isString() || value.toString().size() > maximum)
            return false;
        units += value.toString().size();
        // Serialized canonical rows repeat display text in their timed pieces.
        return units <= 4 * 1024 * 1024;
    }
};
bool intervalFields(const QJsonObject &row)
{
    return timestamp(row.value("start_ms")) && timestamp(row.value("duration_ms")) &&
           row.value("start_ms").toDouble() + row.value("duration_ms").toDouble() <=
               9007199254740991.;
}
bool words(const QJsonValue &value, MetadataBudget &budget, qint64 start = -1, qint64 end = -1)
{
    if (!value.isArray())
        return false;
    const auto array = value.toArray();
    budget.words += array.size();
    if (budget.words > 65536)
        return false;
    for (const auto &entry : array) {
        if (!entry.isObject())
            return false;
        const auto word = entry.toObject();
        if (!budget.text(word.value("text")) || !intervalFields(word))
            return false;
        const auto at = word.value("start_ms").toInteger();
        if (start >= 0 && (at < start || at + word.value("duration_ms").toInteger() > end))
            return false;
    }
    return true;
}
bool optionalText(const QJsonObject &source, const QString &key, const QString &target,
                  QVariantMap &result, MetadataBudget &budget, qsizetype maximum = 256)
{
    if (!source.contains(key))
        return true;
    if (!budget.text(source.value(key), maximum))
        return false;
    result.insert(target, source.value(key).toString());
    return true;
}
bool alternateTexts(const QJsonObject &source, QVariantMap &result, MetadataBudget &budget)
{
    if (!source.contains("alternate_texts"))
        return true;
    if (!source.value("alternate_texts").isArray() ||
        source.value("alternate_texts").toArray().size() > 64)
        return false;
    QVariantList entries;
    for (const auto &value : source.value("alternate_texts").toArray()) {
        if (!value.isObject())
            return false;
        const auto entry = value.toObject();
        if (!budget.text(entry.value("text")) || !entry.value("role").isString() ||
            (entry.value("role").toString() != "translation" &&
             entry.value("role").toString() != "romanization"))
            return false;
        QVariantMap alternate{{"text", entry.value("text").toString()},
                              {"role", entry.value("role").toString()}};
        if (!optionalText(entry, "lang", "lang", alternate, budget))
            return false;
        entries << alternate;
    }
    result.insert("alternateTexts", entries);
    return true;
}
bool rowMetadata(const QJsonObject &source, QVariantMap &result, MetadataBudget &budget)
{
    if (!optionalText(source, "id", "id", result, budget) ||
        !optionalText(source, "agent_id", "agentId", result, budget) ||
        !optionalText(source, "song_part", "songPart", result, budget) ||
        !alternateTexts(source, result, budget))
        return false;
    if (source.contains("block_index")) {
        if (!timestamp(source.value("block_index")) ||
            source.value("block_index").toDouble() > 65536)
            return false;
        result.insert("blockIndex", source.value("block_index").toInt());
    }
    if (source.contains("is_chorus")) {
        if (!source.value("is_chorus").isBool())
            return false;
        result.insert("isChorus", source.value("is_chorus").toBool());
    }
    return true;
}
bool backgrounds(const QJsonObject &source, QVariantList &result, MetadataBudget &budget)
{
    if (!source.contains("background_vocals"))
        return true;
    if (!source.value("background_vocals").isArray() ||
        source.value("background_vocals").toArray().size() > 8)
        return false;
    for (const auto &value : source.value("background_vocals").toArray()) {
        if (!value.isObject())
            return false;
        const auto vocal = value.toObject();
        if (vocal.contains("background_vocals"))
            return false;
        if (!budget.text(vocal.value("text")) || !intervalFields(vocal))
            return false;
        const auto start = vocal.value("start_ms").toInteger(),
                   duration = vocal.value("duration_ms").toInteger();
        if (!words(vocal.value("words"), budget, start, start + duration))
            return false;
        QVariantMap entry{{"lineText", vocal.value("text").toString()},
                          {"startMs", start},
                          {"durationMs", duration},
                          {"words", vocal.value("words").toArray().toVariantList()}};
        if (!optionalText(vocal, "translation", "translation", entry, budget, 16384) ||
            !optionalText(vocal, "romanization", "romanization", entry, budget, 16384) ||
            !rowMetadata(vocal, entry, budget))
            return false;
        result << entry;
    }
    return true;
}
bool documentMetadata(const QJsonObject &source, QVariantMap &result, MetadataBudget &budget)
{
    if (!optionalText(source, "source_format", "sourceFormat", result, budget, 64) ||
        !optionalText(source, "timing_profile", "timingProfile", result, budget, 64))
        return false;
    if (source.contains("timing_profile") &&
        (source.value("source_format").toString() != "ttml" ||
         (source.value("timing_profile").toString() != "ttml-media" &&
          source.value("timing_profile").toString() != "apple-lyrics-absolute")))
        return false;
    if (source.contains("agents")) {
        if (!source.value("agents").isObject() || source.value("agents").toObject().size() > 64)
            return false;
        QVariantMap agents;
        const auto input = source.value("agents").toObject();
        for (auto it = input.begin(); it != input.end(); ++it) {
            if (it.key().isEmpty() || it.key().size() > 256 || !it.value().isObject())
                return false;
            const auto value = it.value().toObject();
            if (!value.value("id").isString() || value.value("id").toString() != it.key())
                return false;
            QVariantMap agent{{"id", it.key()}};
            if (!optionalText(value, "name", "name", agent, budget) ||
                !optionalText(value, "type", "type", agent, budget))
                return false;
            agents.insert(it.key(), agent);
        }
        result.insert("agents", agents);
    }
    return true;
}
} // namespace
namespace EchoMusic
{
void LyricModel::copyPresentationFrom(const LyricModel &source)
{
    if (this == &source)
        return;
    beginResetModel();
    m_lines = source.m_lines;
    m_filter = source.m_filter;
    m_filterEnabled = source.m_filterEnabled;
    m_endPrefix = source.m_endPrefix;
    m_offset = source.m_offset;
    m_current = source.m_current;
    m_scrollable = source.m_scrollable;
    m_hasTranslation = source.m_hasTranslation;
    m_hasRomanization = source.m_hasRomanization;
    m_metadata = source.m_metadata;
    endResetModel();
    emit changed();
    emit currentChanged();
}
QVariant LyricModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() < 0 || i.row() >= m_lines.size())
        return {};
    const auto &l = m_lines[i.row()];
    switch (role) {
    case Qt::UserRole + 1:
        return l.text;
    case Qt::UserRole + 2:
        return l.start + m_offset;
    case Qt::UserRole + 3:
        return l.duration;
    case Qt::UserRole + 4:
        return l.words;
    case Qt::UserRole + 5:
        return l.translation;
    case Qt::UserRole + 6:
        return l.romanization;
    case Qt::UserRole + 7:
        return l.filtered;
    case Qt::UserRole + 8:
        return std::any_of(l.words.cbegin(), l.words.cend(), [](const QVariant &word) {
            return word.toMap().value("duration_ms").toLongLong() > 0;
        });
    case Qt::UserRole + 9:
        return l.metadata.value("id");
    case Qt::UserRole + 10:
        return l.metadata.value("agentId");
    case Qt::UserRole + 11:
        return l.metadata.value("songPart");
    case Qt::UserRole + 12:
        return l.metadata.value("blockIndex");
    case Qt::UserRole + 13:
        return l.metadata.value("isChorus");
    case Qt::UserRole + 14: {
        auto result = l.backgroundVocals;
        for (auto &value : result) {
            auto vocal = value.toMap();
            vocal.insert("startMs", vocal.value("startMs").toLongLong() + m_offset);
            value = vocal;
        }
        return result;
    }
    case Qt::UserRole + 15:
        return l.metadata.value("alternateTexts", QVariantList{});
    default:
        return {};
    }
}
QVariantMap LyricModel::lineAt(int row) const
{
    if (row < 0 || row >= m_lines.size())
        return {};
    QVariantMap result;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        result.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return result;
}
void LyricModel::setFilter(bool enabled, const QString &pattern)
{
    m_filterEnabled = enabled;
    m_filter = QRegularExpression(pattern);
    for (auto &line : m_lines)
        line.filtered = enabled && m_filter.isValid() && m_filter.match(line.text).hasMatch();
    if (!m_lines.isEmpty())
        emit dataChanged(index(0), index(m_lines.size() - 1), {Qt::UserRole + 7});
    emit changed();
}
int LyricModel::visibleIndex(int row) const
{
    for (int i = qMin(row, int(m_lines.size()) - 1); i >= 0; --i)
        if (!m_lines[i].filtered)
            return i;
    return -1;
}
int LyricModel::nextVisible(int row) const
{
    for (int i = qMax(0, row + 1); i < m_lines.size(); ++i)
        if (!m_lines[i].filtered)
            return i;
    return -1;
}
QVariantList LyricModel::verticalAt(int row) const
{
    return row < 0 || row >= m_lines.size() ? QVariantList{} : verticalUnits(m_lines[row]);
}
QVariantList LyricModel::verticalText(const QString &text) const
{
    return verticalUnits({text, 0, 0, {}, QString(), QString(), false, {}, {}});
}
QVariantList LyricModel::verticalUnits(const Line &line)
{
    QVariantList result;
    const bool wordTimed =
        std::any_of(line.words.cbegin(), line.words.cend(), [](const QVariant &word) {
            return word.toMap().value("duration_ms").toLongLong() > 0;
        });
    QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, line.text);
    int start = 0, wordStart = 0, wordIndex = 0;
    while (true) {
        int end = boundary.toNextBoundary();
        if (end < 0)
            break;
        auto text = line.text.mid(start, end - start);
        const auto code = text.toUcs4().value(0);
        const auto script = QChar::script(code);
        const bool sideways = script == QChar::Script_Latin || script == QChar::Script_Arabic ||
                              script == QChar::Script_Hebrew || QChar::isDigit(code);
        if (sideways) {
            // Keep complete Latin/RTL runs so Qt retains contextual shaping and bidi.
            while (end < line.text.size()) {
                const int next = boundary.toNextBoundary();
                if (next < 0)
                    break;
                const auto candidate = line.text.mid(end, next - end);
                const auto cp = candidate.toUcs4().value(0);
                const auto sc = QChar::script(cp);
                if (sc != script && !QChar::isDigit(cp)) {
                    boundary.setPosition(end);
                    break;
                }
                text += candidate;
                end = next;
            }
        }
        while (wordIndex < line.words.size() &&
               wordStart + line.words[wordIndex].toMap().value("text").toString().size() <= start) {
            wordStart += line.words[wordIndex].toMap().value("text").toString().size();
            ++wordIndex;
        }
        qint64 at = line.start, duration = line.duration;
        double from = double(start) / qMax<qsizetype>(1, line.text.size()),
               to = double(end) / qMax<qsizetype>(1, line.text.size());
        if (wordIndex < line.words.size()) {
            const auto word = line.words[wordIndex].toMap();
            const int length = word.value("text").toString().size();
            if (end <= wordStart + length && length > 0) {
                at = word.value("start_ms").toLongLong();
                duration = word.value("duration_ms").toLongLong();
                from = double(start - wordStart) / length;
                to = double(end - wordStart) / length;
            }
        }
        result << QVariantMap{{"text", text},          {"sideways", sideways},
                              {"start_ms", at},        {"duration_ms", duration},
                              {"from", from},          {"to", to},
                              {"wordTimed", wordTimed}};
        start = end;
    }
    return result;
}
QVariantList LyricModel::rubyAt(int row) const
{
    if (row < 0 || row >= m_lines.size())
        return {};
    const auto &line = m_lines[row];
    if (!data(index(row), Qt::UserRole + 8).toBool())
        return {};
    const auto tokens = line.romanization.simplified().split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return {};
    // Presentation grouping only. Every group retains original base-word timing;
    // romanization never supplies invented syllable times or modifies the domain model.
    auto words = line.words;
    if (words.isEmpty())
        words.append(QVariantMap{
            {"text", line.text}, {"start_ms", line.start}, {"duration_ms", line.duration}});
    QString joined;
    for (const auto &word : words)
        joined += word.toMap().value("text").toString();
    if (joined != line.text)
        return {};
    QVariantList result;
    const int groups = qMin(int(words.size()), int(tokens.size()));
    for (int i = 0; i < groups; ++i) {
        const int first = qRound(double(i) * words.size() / groups),
                  last = qRound(double(i + 1) * words.size() / groups);
        const int rfirst = qRound(double(i) * tokens.size() / groups),
                  rlast = qRound(double(i + 1) * tokens.size() / groups);
        QString text;
        QVariantList segment;
        for (int j = first; j < last; ++j) {
            text += words[j].toMap().value("text").toString();
            segment << words[j];
        }
        const auto start = words[first].toMap().value("start_ms").toLongLong();
        const auto tail = words[last - 1].toMap();
        result << QVariantMap{
            {"text", text},
            {"ruby", tokens.mid(rfirst, rlast - rfirst).join(' ')},
            {"words", segment},
            {"start_ms", start},
            {"duration_ms", qMax<qint64>(1, tail.value("start_ms").toLongLong() +
                                                tail.value("duration_ms").toLongLong() - start)}};
    }
    return result;
}
qint64 LyricModel::startAt(int index) const
{
    return index >= 0 && index < m_lines.size() ? m_lines[index].start + m_offset : -1;
}
qint64 LyricModel::endThrough(int index) const
{
    return index < 0 ? 0 : index < m_endPrefix.size() ? m_endPrefix[index] + m_offset : -1;
}
qint64 LyricModel::durationAt(int index) const
{
    return index >= 0 && index < m_lines.size() ? m_lines[index].duration : 0;
}
bool LyricModel::replace(const QJsonObject &lyrics)
{
    if (!lyrics.value("lines").isArray())
        return false;
    if (!lyrics.value("offset_ms").isUndefined() && !timestamp(lyrics.value("offset_ms"), true))
        return false;
    QList<Line> lines;
    MetadataBudget budget;
    QVariantMap metadata;
    if (!documentMetadata(lyrics, metadata, budget))
        return false;
    const auto source = lyrics.value("lines").toArray();
    const bool extended = !metadata.isEmpty() ||
                          std::any_of(source.begin(), source.end(), [](const QJsonValue &value) {
                              const auto line = value.toObject();
                              return line.contains("background_vocals") ||
                                     line.contains("agent_id") || line.contains("song_part") ||
                                     line.contains("block_index") || line.contains("is_chorus") ||
                                     line.contains("alternate_texts") || line.contains("id");
                          });
    if (extended && source.size() > 8192)
        return false;
    for (const auto &v : source) {
        const auto l = v.toObject();
        if (!l.value("text").isString() || !timestamp(l.value("start_ms")) ||
            !timestamp(l.value("duration_ms")) || !l.value("words").isArray())
            return false;
        const auto words = l.value("words").toArray();
        for (const auto &field : {"translation", "romanization"})
            if (l.contains(field) && !l.value(field).isString())
                return false;
        for (const auto &word : words) {
            const auto w = word.toObject();
            if (!w.value("text").isString() || !timestamp(w.value("start_ms")) ||
                !timestamp(w.value("duration_ms")))
                return false;
        }
        if (extended && (!budget.text(l.value("text")) || !intervalFields(l) ||
                         !::words(l.value("words"), budget)))
            return false;
        if (extended)
            for (const auto &field : {"translation", "romanization"})
                if (l.contains(field) && !budget.text(l.value(field)))
                    return false;
        Line line{l.value("text").toString(),
                  qint64(l.value("start_ms").toDouble()),
                  qint64(l.value("duration_ms").toDouble()),
                  l.value("words").toArray().toVariantList(),
                  l.value("translation").toString(),
                  l.value("romanization").toString(),
                  false,
                  {},
                  {}};
        if (!rowMetadata(l, line.metadata, budget) ||
            !backgrounds(l, line.backgroundVocals, budget))
            return false;
        lines.append(std::move(line));
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const auto &a, const auto &b) { return a.start < b.start; });
    beginResetModel();
    m_lines = std::move(lines);
    metadata.insert("hasBackgroundVocals",
                    std::any_of(m_lines.cbegin(), m_lines.cend(),
                                [](const Line &line) { return !line.backgroundVocals.isEmpty(); }));
    m_metadata = std::move(metadata);
    for (auto &line : m_lines)
        line.filtered =
            m_filterEnabled && m_filter.isValid() && m_filter.match(line.text).hasMatch();
    m_scrollable = lyrics.value("scrollable").toBool(true);
    m_endPrefix.clear();
    qint64 end = 0;
    for (const auto &line : m_lines) {
        end = std::max(end, line.start + line.duration);
        m_endPrefix.append(end);
    }
    m_hasTranslation = std::any_of(m_lines.cbegin(), m_lines.cend(),
                                   [](const Line &l) { return !l.translation.isEmpty(); });
    m_hasRomanization = std::any_of(m_lines.cbegin(), m_lines.cend(),
                                    [](const Line &l) { return !l.romanization.isEmpty(); });
    m_offset = qint64(lyrics.value("offset_ms").toDouble());
    m_current = -1;
    endResetModel();
    emit changed();
    emit currentChanged();
    return true;
}
void LyricModel::clear()
{
    beginResetModel();
    m_lines.clear();
    m_metadata.clear();
    m_scrollable = true;
    m_endPrefix.clear();
    m_current = -1;
    m_offset = 0;
    m_hasTranslation = m_hasRomanization = false;
    endResetModel();
    emit changed();
    emit currentChanged();
}
int LyricModel::indexAt(qint64 positionMs) const
{
    if (!m_scrollable)
        return -1;
    const auto it = std::upper_bound(m_lines.cbegin(), m_lines.cend(), positionMs - m_offset,
                                     [](auto time, const auto &line) { return time < line.start; });
    return int(it - m_lines.cbegin()) - 1;
}
int LyricModel::visibleIndexAt(qint64 positionMs) const
{
    // Unsynced lyrics (a plain text lyric, or one without timelines) have no
    // playhead to follow; the first line is what the strip shows then.
    if (!m_scrollable)
        return nextVisible(-1);
    return visibleIndex(indexAt(positionMs));
}
void LyricModel::setPosition(qint64 positionMs)
{
    const int index = indexAt(positionMs);
    if (index != m_current) {
        m_current = index;
        emit currentChanged();
    }
}
} // namespace EchoMusic
