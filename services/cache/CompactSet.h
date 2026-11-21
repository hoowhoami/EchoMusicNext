#pragma once
#include <QSet>
#include <iterator>
#include <utility>
#include <variant>

namespace EchoMusic
{
/// Identity relations are usually empty or singleton. Keep that case inline;
/// promote real fan-out to an implicitly shared hash set, preserving bounded
/// lookup cost for large artist/row groups. Iteration has no ordering contract.
template <typename T> class CompactSet
{
public:
    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = qsizetype;
        using pointer = const T *;
        using reference = const T &;
        const_iterator() = default;
        reference operator*() const
        {
            return m_isHash ? *m_hash : *m_single;
        }
        pointer operator->() const
        {
            return &operator*();
        }
        const_iterator &operator++()
        {
            if (m_isHash)
                ++m_hash;
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
            return m_isHash == other.m_isHash &&
                   (m_isHash ? m_hash == other.m_hash : m_single == other.m_single);
        }

    private:
        friend class CompactSet;
        explicit const_iterator(const T *single) : m_single(single) {}
        explicit const_iterator(typename QSet<T>::const_iterator hash)
            : m_hash(hash), m_isHash(true)
        {
        }
        const T *m_single = nullptr;
        typename QSet<T>::const_iterator m_hash;
        bool m_isHash = false;
    };

    bool isEmpty() const
    {
        return std::holds_alternative<std::monostate>(m_values);
    }
    qsizetype size() const
    {
        if (const auto *many = std::get_if<QSet<T>>(&m_values))
            return many->size();
        return isEmpty() ? 0 : 1;
    }
    bool usesHashStorage() const
    {
        return std::holds_alternative<QSet<T>>(m_values);
    }
    bool contains(const T &value) const
    {
        if (const auto *single = std::get_if<T>(&m_values))
            return *single == value;
        const auto *many = std::get_if<QSet<T>>(&m_values);
        return many && many->contains(value);
    }
    bool insert(T value)
    {
        if (isEmpty()) {
            m_values = std::move(value);
            return true;
        }
        if (const auto *single = std::get_if<T>(&m_values)) {
            if (*single == value)
                return false;
            QSet<T> many{*single, std::move(value)};
            m_values = std::move(many);
            return true;
        }
        auto &many = std::get<QSet<T>>(m_values);
        if (many.contains(value))
            return false;
        many.insert(std::move(value));
        return true;
    }
    bool remove(const T &value)
    {
        if (const auto *single = std::get_if<T>(&m_values)) {
            if (*single != value)
                return false;
            clear();
            return true;
        }
        auto *many = std::get_if<QSet<T>>(&m_values);
        if (!many || !many->remove(value))
            return false;
        if (many->size() == 1) {
            auto remaining = *many->cbegin();
            m_values = std::move(remaining);
        } else if (many->isEmpty())
            clear();
        return true;
    }
    void unite(const CompactSet &other)
    {
        if (this != &other)
            for (const auto &value : other)
                insert(value);
    }
    void clear()
    {
        m_values = std::monostate{};
    }
    const_iterator cbegin() const
    {
        if (const auto *single = std::get_if<T>(&m_values))
            return const_iterator(single);
        if (const auto *many = std::get_if<QSet<T>>(&m_values))
            return const_iterator(many->cbegin());
        return {};
    }
    const_iterator cend() const
    {
        if (const auto *many = std::get_if<QSet<T>>(&m_values))
            return const_iterator(many->cend());
        return {};
    }
    const_iterator begin() const
    {
        return cbegin();
    }
    const_iterator end() const
    {
        return cend();
    }

private:
    std::variant<std::monostate, T, QSet<T>> m_values;
};
} // namespace EchoMusic
