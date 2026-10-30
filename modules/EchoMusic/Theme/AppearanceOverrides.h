#pragma once
#include <QObject>
#include <QVariantMap>
#include <QHash>
namespace EchoMusic
{
/// Ephemeral per-property contributions above the currently selected theme.
/// No preferences are captured or written; equal priorities use publish order.
class AppearanceOverrides final : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    bool put(const QString &token, QVariantMap definition, int priority, QVariantMap owner);
    void remove(const QString &token);
    QVariantMap resolve(QVariantMap base, bool dark) const;
    QVariantList records() const;
signals:
    void changed();

private:
    struct Entry
    {
        QVariantMap definition, owner;
        int priority;
        quint64 order;
    };
    QList<Entry> ordered() const;
    QHash<QString, Entry> m_entries;
    quint64 m_order = 0;
};
} // namespace EchoMusic
