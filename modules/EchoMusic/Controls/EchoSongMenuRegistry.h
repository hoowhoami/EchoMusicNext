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
    /// Per-engine registry, parented to its engine. A QML_SINGLETON built from
    /// a default constructor goes through `QQmlElement<T>`, which derives from
    /// T and therefore cannot wrap a `final` class; requiring the engine makes
    /// Qt fall back to create() below (the same arrangement as ThemeStore and
    /// EchoUiPartRegistry).
    explicit EchoSongMenuRegistry(QQmlEngine &engine);
    /// QML singleton entry point. The engine owns the instance, so C++ and
    /// script always see exactly the object forEngine() hands out.
    static EchoSongMenuRegistry *create(QQmlEngine *engine, QJSEngine *scriptEngine);
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
