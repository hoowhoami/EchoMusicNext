#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QQmlParserStatus>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <QColor>
#include <functional>
namespace EchoMusic
{
/// Engine-owned value resolver. It borrows scoped callbacks, never media state.
class EchoCoverRegistry final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit EchoCoverRegistry(QQmlEngine &);
    static EchoCoverRegistry *create(QQmlEngine *, QJSEngine *);
    static EchoCoverRegistry *forEngine(QQmlEngine &);
    bool put(const QString &token, int priority, std::function<QUrl(const QVariantMap &)>);
    void remove(const QString &token);
    void invalidate(const QString &token);
    QUrl resolve(const QVariantMap &, const QSet<QUrl> &rejected);
    static QUrl themedSvgUrl(const QString &body, double width, double height,
                             const QColor &accent);
    static QUrl themedIconUrl(const QString &glyph, const QColor &accent);
    int count() const
    {
        return m_entries.size();
    }
signals:
    void changed();

private:
    struct Entry
    {
        QString token;
        int priority;
        std::function<QUrl(const QVariantMap &)> resolve;
    };
    QHash<QString, Entry> m_entries;
    bool m_resolving = false;
};
/// One live cover's error/fallback selection. Inactive pooled views hold no
/// registry subscription or decoded source; retries are bounded by revision.
class EchoCoverSource : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY inputChanged)
    Q_PROPERTY(QVariantMap context READ context WRITE setContext NOTIFY inputChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY inputChanged)
    Q_PROPERTY(QUrl resolvedSource READ resolvedSource NOTIFY changed)
public:
    explicit EchoCoverSource(QObject *parent = nullptr) : QObject(parent) {}
    QUrl source() const
    {
        return m_source;
    }
    QVariantMap context() const
    {
        return m_context;
    }
    bool active() const
    {
        return m_active;
    }
    QUrl resolvedSource() const
    {
        return m_resolved;
    }
    void setSource(const QUrl &);
    void setContext(const QVariantMap &);
    void setActive(bool);
    void classBegin() override {}
    void componentComplete() override;
    Q_INVOKABLE void reject(const QUrl &source);
signals:
    void inputChanged();
    void changed();

private:
    void schedule(bool reset);
    void resolve();
    QUrl m_source, m_resolved;
    QVariantMap m_context;
    QSet<QUrl> m_rejected;
    QPointer<EchoCoverRegistry> m_registry;
    QMetaObject::Connection m_connection;
    bool m_active = true, m_complete = false, m_queued = false;
};
} // namespace EchoMusic
