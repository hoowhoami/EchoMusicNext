#include "EchoUiModelOrder.h"
#include <QSet>

namespace EchoMusic
{
int EchoUiModelOrder::nativeAt(int position) const
{
    if (position < 0 || position >= m_count)
        return -1;
    for (const auto &span : m_spans) {
        if (position < span.count)
            return span.first >= 0 ? span.first + position : -1;
        position -= span.count;
    }
    return -1;
}
int EchoUiModelOrder::extraAt(int position) const
{
    if (position < 0 || position >= m_count)
        return -1;
    for (const auto &span : m_spans) {
        if (position < span.count)
            return span.extra;
        position -= span.count;
    }
    return -1;
}
int EchoUiModelOrder::positionOfNative(int index) const
{
    int position = 0;
    for (const auto &span : m_spans) {
        if (span.first >= 0 && index >= span.first && index < span.first + span.count)
            return position + index - span.first;
        position += span.count;
    }
    return -1;
}
int EchoUiModelOrder::positionOfExtra(int index) const
{
    int position = 0;
    for (const auto &span : m_spans) {
        if (span.extra == index)
            return position;
        position += span.count;
    }
    return -1;
}
void EchoUiModelOrder::insert(int position, Span value)
{
    int offset = 0;
    for (int i = 0; i < m_spans.size(); ++i) {
        const auto span = m_spans[i];
        if (position == offset) {
            m_spans.insert(i, value);
            ++m_count;
            return;
        }
        if (position < offset + span.count) {
            const int left = position - offset;
            m_spans[i].count = left;
            m_spans.insert(i + 1, value);
            m_spans.insert(i + 2, {span.first + left, span.count - left, span.extra});
            ++m_count;
            return;
        }
        offset += span.count;
    }
    m_spans.append(value);
    ++m_count;
}
void EchoUiModelOrder::erase(int position)
{
    int offset = 0;
    for (int i = 0; i < m_spans.size(); ++i) {
        const auto span = m_spans[i];
        if (position >= offset && position < offset + span.count) {
            const int left = position - offset, right = span.count - left - 1;
            m_spans.removeAt(i);
            if (right)
                m_spans.insert(i, {span.first + left + 1, right, span.extra});
            if (left)
                m_spans.insert(i, {span.first, left, span.extra});
            --m_count;
            return;
        }
        offset += span.count;
    }
}
bool EchoUiModelOrder::build(int nativeCount, const QList<QVariantMap> &entries,
                             const NativeLookup &lookup, QString &error)
{
    EchoUiModelOrder next;
    if (nativeCount)
        next.m_spans.append({0, nativeCount, -1});
    next.m_count = nativeCount;
    QHash<QString, int> keys;
    for (int i = 0; i < entries.size(); ++i) {
        const auto key = entries[i].value("_echoUiKey", entries[i].value("key")).toString();
        if (key.isEmpty() || keys.contains(key)) {
            error = "Ambiguous model entry key";
            return false;
        }
        keys[key] = i;
    }
    const auto nativeIndex = [&](const QString &reference) {
        if (!reference.startsWith("native:"))
            return -1;
        const auto key = reference.mid(7);
        bool ok = false;
        const int index = key.startsWith('#') ? key.mid(1).toInt(&ok) : lookup(key);
        return (key.startsWith('#') && !ok) || index < 0 || index >= nativeCount ? -1 : index;
    };
    const auto resolve = [&](const QString &anchor, bool includeHidden = false) {
        if (anchor.startsWith("native:")) {
            const int index = nativeIndex(anchor);
            if (index < 0)
                return -1;
            const int visible = next.positionOfNative(index);
            if (visible >= 0 || !includeHidden)
                return visible;
            int position = 0;
            for (const auto &span : next.m_spans) {
                if (!span.count && span.first == index)
                    return position;
                position += span.count;
            }
            return -1;
        }
        return keys.contains(anchor) ? next.positionOfExtra(keys.value(anchor)) : -1;
    };
    const auto targetKey = [&](const QVariantMap &entry) {
        const auto target = entry.value("target").toString();
        const int index = nativeIndex(target);
        return index >= 0 ? "@native:" + QString::number(index) : target;
    };
    QHash<QString, int> winners;
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].value("operation", "insert") != "insert")
            winners[targetKey(entries[i])] = i;
    QSet<int> pending;
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].value("operation", "insert") == "insert" ||
            winners.value(targetKey(entries[i])) == i)
            pending.insert(i);
    while (!pending.isEmpty()) {
        bool progressed = false;
        for (int i = 0; i < entries.size(); ++i) {
            if (!pending.contains(i))
                continue;
            const auto &entry = entries[i];
            const auto operation = entry.value("operation", "insert").toString();
            const auto position = entry.value("position", "append").toString();
            const auto anchor = entry.value("anchor").toString();
            int at = position == "prepend" ? 0 : next.count();
            if (position == "before" || position == "after") {
                at = resolve(anchor, true);
                if (at < 0)
                    continue;
                if (position == "after" && (!anchor.startsWith("native:") ||
                                            next.positionOfNative(nativeIndex(anchor)) >= 0))
                    ++at;
            }
            if (operation != "insert") {
                const int target = resolve(entry.value("target").toString());
                if (target < 0)
                    continue;
                const int native = next.nativeAt(target), extra = next.extraAt(target);
                if (operation == "move") {
                    if (at > target)
                        --at;
                    next.erase(target);
                    next.insert(at, {native, 1, extra});
                } else {
                    next.erase(target);
                    if (operation == "replace")
                        next.insert(target, {native, 1, i});
                    else if (operation == "hide" && native >= 0) {
                        int offset = 0, at = 0;
                        for (; at < next.m_spans.size(); ++at) {
                            if (offset == target)
                                break;
                            offset += next.m_spans[at].count;
                        }
                        next.m_spans.insert(at, {native, 0, -2});
                    } else if (operation != "hide") {
                        error = "Unsupported model operation";
                        return false;
                    }
                }
            } else
                next.insert(at, {-1, 1, i});
            pending.remove(i);
            progressed = true;
        }
        if (!progressed) {
            error = "Model anchor or target is absent, ambiguous, or cyclic";
            return false;
        }
    }
    *this = std::move(next);
    error.clear();
    return true;
}
} // namespace EchoMusic
