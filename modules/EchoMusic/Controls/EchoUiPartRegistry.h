#pragma once
#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiPartRegistration;
class EchoUiPartRegistry final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    /// Per-engine registry, parented to its engine. A QML_SINGLETON built from
    /// a default constructor goes through `QQmlElement<T>`, which derives from
    /// T and therefore cannot wrap a `final` class; requiring the engine makes
    /// Qt fall back to create() below (the same arrangement as ThemeStore).
    explicit EchoUiPartRegistry(QQmlEngine &engine);
    /// QML singleton entry point. The engine owns the instance, so C++ and
    /// script always see exactly the object forEngine() hands out.
    static EchoUiPartRegistry *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    static EchoUiPartRegistry *forEngine(QQmlEngine &);
    void attach(EchoUiPartRegistration *);
    void requestSessionReset();
    void detach(EchoUiPartRegistration *);
    EchoUiPartRegistration *find(const QString &) const;
    QList<EchoUiPartRegistration *> query(const QVariantMap &) const;
    static bool validSelector(const QVariantMap &);
    static bool matches(const EchoUiPartRegistration &, const QVariantMap &);
    static bool normalize(QVariantMap &, QString &error);
    bool putStyle(const QString &token, const QString &owner, const QString &id, int priority,
                  const QVariantMap &selector, const QVariantMap &values);
    void removeStyle(const QString &token);
    void putReplacement(const QString &token, const QString &part, const QString &owner,
                        const QString &id, int priority, const QString &position = "replace");
    QStringList mountOrder(const QString &part, const QString &position) const;
    void removeReplacement(const QString &token);
    void setMountReady(const QString &token, bool ready);
    QString windowId(QQuickWindow *) const;
    int partCount() const
    {
        return m_parts.size();
    }
    int styleCount() const
    {
        return m_styles.size();
    }
signals:
    void added(EchoMusic::EchoUiPartRegistration *part);
    void removed(const QString &identity);
    void partChanged(EchoMusic::EchoUiPartRegistration *part);

private:
    struct Style
    {
        QString token, owner, id;
        int priority = 0;
        QVariantMap selector, values;
    };
    void resolve(EchoUiPartRegistration *);
    void resolveReplacement(EchoUiPartRegistration *);
    QHash<QString, QPointer<EchoUiPartRegistration>> m_parts;
    QHash<QString, Style> m_styles;
    QHash<QString, Style> m_replacements;
    mutable QHash<QQuickWindow *, QString> m_windows;
    mutable quint64 m_windowSequence = 0;
    quint64 m_sequence = 0;
    bool m_sessionResetPending = false;
};
} // namespace EchoMusic
