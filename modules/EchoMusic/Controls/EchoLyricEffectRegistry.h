#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QPointer>
#include <functional>
namespace EchoMusic
{
/// Optional auto-follow arbitration; never runs on audio/render threads.
class EchoLyricEffectRegistry final : public QObject
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
    explicit EchoLyricEffectRegistry(QQmlEngine &engine);
    /// QML singleton entry point. The engine owns the instance, so C++ and
    /// script always see exactly the object forEngine() hands out.
    static EchoLyricEffectRegistry *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    static EchoLyricEffectRegistry *forEngine(QQmlEngine &engine);
    struct ScrollHandler
    {
        QString key;
        QStringList scopes;
        int order = 0;
        QPointer<QObject> owner;
        std::function<bool(const QString &, const QVariantMap &)> invoke;
        quint64 generation = 0;
    };
    quint64 put(ScrollHandler);
    void remove(const QString &, quint64);
    Q_INVOKABLE bool hasHandlers(const QString &scope) const;
    Q_INVOKABLE bool requestScroll(const QString &host, const QVariantMap &request);
signals:
    void changed();

private:
    QHash<QString, ScrollHandler> m_handlers;
    quint64 m_generation = 0;
};
} // namespace EchoMusic
