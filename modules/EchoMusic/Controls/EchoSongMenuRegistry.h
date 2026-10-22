#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QPointer>
#include <functional>
namespace EchoMusic
{
/// Host-owned contribution directory. Views keep immutable generation tokens;
/// there is no script, service QObject or writable catalog in menu row data.
class EchoSongMenuRegistry final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit EchoSongMenuRegistry(QObject *parent = nullptr) : QObject(parent) {}
    static EchoSongMenuRegistry *forEngine(QQmlEngine &engine);
    struct Entry
    {
        QString key, label;
        int order = 0;
        bool danger = false;
        QPointer<QObject> owner;
        std::function<QString()> epoch;
        std::function<bool(const QVariantMap &)> visible, enabled;
        std::function<void(const QVariantMap &)> invoke;
        quint64 generation = 0;
    };
    quint64 put(Entry);
    void remove(const QString &, quint64);
    Q_INVOKABLE QVariantList entries(const QVariantMap &context) const;
    Q_INVOKABLE bool invoke(const QString &key, const QString &token, const QVariantMap &context);
signals:
    void changed();

private:
    QHash<QString, Entry> m_entries;
    quint64 m_generation = 0;
};
} // namespace EchoMusic
