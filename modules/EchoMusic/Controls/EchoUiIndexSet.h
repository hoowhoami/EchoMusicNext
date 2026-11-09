#pragma once
#include <QSet>
#include <optional>
#include <iterator>

namespace EchoMusic
{
// Most reverse-index buckets contain one identity. Keep it in the outer hash
// node; use Qt's shared hash only for genuinely multiple members. Dense role
// and scope buckets retain hashed lookup rather than becoming linear lists.
template <typename T> class EchoUiIndexSet
{
public:
    class const_iterator
    {
    public:
        using value_type = T;
        using difference_type = qsizetype;
        using iterator_category = std::forward_iterator_tag;
        using pointer = const T *;
        using reference = const T &;
        const_iterator() = default;
        const T &operator*() const
        {
            return m_hashed ? *m_many : *m_single;
        }
        const T *operator->() const
        {
            return &**this;
        }
        const_iterator &operator++()
        {
            if (m_hashed)
                ++m_many;
            else
                m_single = nullptr;
            return *this;
        }
        const_iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }
        bool operator==(const const_iterator &other) const
        {
            return m_hashed == other.m_hashed &&
                   (m_hashed ? m_many == other.m_many : m_single == other.m_single);
        }
        bool operator!=(const const_iterator &other) const
        {
            return !(*this == other);
        }

    private:
        friend class EchoUiIndexSet;
        explicit const_iterator(const T *single) : m_single(single) {}
        explicit const_iterator(typename QSet<T>::const_iterator many)
            : m_many(many), m_hashed(true)
        {
        }
        const T *m_single = nullptr;
        typename QSet<T>::const_iterator m_many;
        bool m_hashed = false;
    };
    bool isEmpty() const
    {
        return !m_single && m_many.isEmpty();
    }
    qsizetype size() const
    {
        return m_single ? 1 : m_many.size();
    }
    bool contains(const T &value) const
    {
        return m_single ? *m_single == value : m_many.contains(value);
    }
    void insert(const T &value)
    {
        if (m_single) {
            if (*m_single == value)
                return;
            m_many.insert(*m_single);
            m_single.reset();
        } else if (m_many.isEmpty()) {
            m_single = value;
            return;
        }
        m_many.insert(value);
    }
    bool remove(const T &value)
    {
        if (m_single) {
            if (*m_single != value)
                return false;
            m_single.reset();
            return true;
        }
        if (!m_many.remove(value))
            return false;
        if (m_many.size() == 1) {
            m_single = *m_many.cbegin();
            m_many = {};
        }
        return true;
    }
    const_iterator begin() const
    {
        return m_many.isEmpty() ? const_iterator(m_single ? &*m_single : nullptr)
                                : const_iterator(m_many.cbegin());
    }
    const_iterator end() const
    {
        return m_many.isEmpty() ? const_iterator(nullptr) : const_iterator(m_many.cend());
    }

private:
    std::optional<T> m_single;
    QSet<T> m_many;
};
} // namespace EchoMusic
