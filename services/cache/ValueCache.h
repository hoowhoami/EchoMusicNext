#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <functional>
#include <limits>
#include <list>
#include <optional>

namespace EchoMusic
{
/// Owner-thread value cache. Loaders, request generations and account lifetimes
/// stay with their domain. Empty values are hits; failures need not be inserted.
/// Cost describes cache-owned values, not copies retained by consumers or RSS.
template <typename Key, typename Value> class ValueCache
{
public:
    using Clock = std::function<qint64()>;
    static constexpr qint64 NoExpiry = std::numeric_limits<qint64>::max();
    struct Statistics
    {
        quint64 hits = 0, misses = 0, expired = 0, evicted = 0;
        quint64 cost = 0;
        qsizetype entries = 0;
    };
    explicit ValueCache(qsizetype maximumEntries,
                        quint64 maximumCost = std::numeric_limits<quint64>::max(), Clock clock = {})
        : m_maximumEntries(maximumEntries), m_maximumCost(maximumCost), m_clock(std::move(clock))
    {
        m_timer.start();
    }
    ValueCache(const ValueCache &) = delete;
    ValueCache &operator=(const ValueCache &) = delete;
    /// Inspect without extending the lifetime or promoting a value. Useful for
    /// const projections and failure ordering that must follow insertion time.
    std::optional<Value> peek(const Key &key) const
    {
        const auto it = m_values.constFind(key);
        return it != m_values.cend() && it->expires > now() ? std::optional(it->value)
                                                            : std::nullopt;
    }
    std::optional<Value> lookup(const Key &key)
    {
        auto it = m_values.find(key);
        if (it != m_values.end() && it->expires <= now()) {
            erase(it);
            ++m_statistics.expired;
            it = m_values.end();
        }
        if (it == m_values.end()) {
            ++m_statistics.misses;
            return std::nullopt;
        }
        ++m_statistics.hits;
        m_order.splice(m_order.begin(), m_order, it->position);
        return it->value;
    }
    bool insert(const Key &key, Value value, qint64 ttlMs, quint64 cost = 1)
    {
        remove(key);
        if (m_maximumEntries <= 0 || !cost || cost > m_maximumCost || ttlMs <= 0)
            return false;
        while (m_values.size() >= m_maximumEntries || m_statistics.cost > m_maximumCost - cost) {
            remove(m_order.back());
            ++m_statistics.evicted;
        }
        const auto time = now();
        const auto limit = std::numeric_limits<qint64>::max();
        const auto expires = ttlMs > limit - time ? limit : time + ttlMs;
        m_order.push_front(key);
        m_values.insert(key, {std::move(value), expires, cost, m_order.begin()});
        m_statistics.cost += cost;
        return true;
    }
    /// Replace a decoded failure's surviving alternatives without extending TTL
    /// or changing LRU order. The caller preserves the entry's declared cost.
    bool update(const Key &key, Value value)
    {
        auto it = m_values.find(key);
        if (it == m_values.end())
            return false;
        if (it->expires <= now()) {
            erase(it);
            ++m_statistics.expired;
            return false;
        }
        it->value = std::move(value);
        return true;
    }
    bool remove(const Key &key)
    {
        const auto it = m_values.find(key);
        if (it == m_values.end())
            return false;
        erase(it);
        return true;
    }
    void clear()
    {
        m_values.clear();
        m_order.clear();
        m_statistics.cost = 0;
    }
    qsizetype size() const
    {
        return m_values.size();
    }
    QList<Key> keys() const
    {
        return QList<Key>(m_order.begin(), m_order.end());
    }
    Statistics statistics() const
    {
        auto result = m_statistics;
        result.entries = size();
        return result;
    }

private:
    struct Entry
    {
        Value value;
        qint64 expires;
        quint64 cost;
        typename std::list<Key>::iterator position;
    };
    qint64 now() const
    {
        return m_clock ? m_clock() : m_timer.elapsed();
    }
    void erase(typename QHash<Key, Entry>::iterator it)
    {
        m_statistics.cost -= it->cost;
        m_order.erase(it->position);
        m_values.erase(it);
    }
    qsizetype m_maximumEntries;
    quint64 m_maximumCost;
    Clock m_clock;
    QElapsedTimer m_timer;
    std::list<Key> m_order;
    QHash<Key, Entry> m_values;
    Statistics m_statistics;
};
} // namespace EchoMusic
