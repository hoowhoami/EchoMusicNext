#pragma once
#include <QList>
#include <QString>
#include <QVariantMap>
#include <functional>

namespace EchoMusic
{
// A sparse sequence: native ranges are retained as ranges, never copied into
// a second business model. Only explicit extension boundaries split a range.
class EchoUiModelOrder
{
public:
    struct Span
    {
        int first = 0, count = 0, extra = -1;
    };
    using NativeLookup = std::function<int(const QString &)>;
    bool build(int nativeCount, const QList<QVariantMap> &entries, const NativeLookup &, QString &);
    int count() const
    {
        return m_count;
    }
    int nativeAt(int position) const;
    int extraAt(int position) const;
    int positionOfNative(int index) const;
    int positionOfExtra(int index) const;

private:
    void insert(int position, Span);
    void erase(int position);
    QList<Span> m_spans;
    int m_count = 0;
};
} // namespace EchoMusic
