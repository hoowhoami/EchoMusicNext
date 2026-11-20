#pragma once
#include <QFont>
#include <QDataStream>
#include <QHash>
#include <QGuiApplication>
#include <QLocale>
#include <QVariantList>
#include <memory>

namespace EchoMusic::GlyphStaticLayout
{
struct Input
{
    QFont font, applicationFont;
    QByteArray serialized;
    Input() = default;
    Input(QFont requestedFont, QFont defaultFont, const QVariantList &runs, qreal dpr,
          qreal basePixels)
        : font(std::move(requestedFont)), applicationFont(std::move(defaultFont))
    {
        QDataStream stream(&serialized, QIODevice::WriteOnly);
        stream << font << applicationFont << runs << dpr << basePixels << QLocale().bcp47Name()
               << int(QGuiApplication::layoutDirection());
    }
    bool operator==(const Input &other) const
    {
        // QVariant numeric conversions must not make an invalid input equal
        // to a previously accepted string or differently typed run field.
        return font == other.font && applicationFont == other.applicationFont &&
               serialized == other.serialized;
    }
};
// The index owns no font, run or pixel data. Those values live only with the
// active consumers of an immutable result. A bucket match never proves equality.
template <typename Layout> class LiveResults
{
public:
    struct Result
    {
        Input input;
        Layout layout;
    };
    using Handle = std::shared_ptr<const Result>;
    Handle find(const Input &input, size_t hash)
    {
        auto it = buckets.find(hash);
        if (it == buckets.end())
            return {};
        for (auto candidate = it->begin(); candidate != it->end();) {
            if (auto value = candidate->lock()) {
                if (value->input == input)
                    return value;
                ++candidate;
            } else
                candidate = it->erase(candidate);
        }
        return {};
    }
    Handle insert(Input input, size_t hash, Layout layout)
    {
        prune();
        auto value = std::make_shared<const Result>(Result{std::move(input), std::move(layout)});
        buckets[hash].append(value);
        return value;
    }
    void clear()
    {
        buckets.clear();
    }
    void prune()
    {
        for (auto it = buckets.begin(); it != buckets.end();) {
            it->removeIf([](const auto &value) { return value.expired(); });
            if (it->isEmpty())
                it = buckets.erase(it);
            else
                ++it;
        }
    }
    qsizetype liveCount()
    {
        prune();
        qsizetype count = 0;
        for (const auto &values : buckets)
            count += values.size();
        return count;
    }

private:
    QHash<size_t, QList<std::weak_ptr<const Result>>> buckets;
};
struct Statistics
{
    quint64 hits = 0, builds = 0;
    qsizetype liveResults = 0;
};
// Private native regression/benchmark observation; not a QML or SDK interface.
Statistics statistics();
} // namespace EchoMusic::GlyphStaticLayout
