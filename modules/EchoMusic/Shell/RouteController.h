#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class SidebarLayoutModel;
/// One mounted page's identity and lifecycle. QML owns only visual state; services
/// must stop page-scoped work when active becomes false and on destruction.
class RouteContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by RouteController")
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString instanceId READ instanceId CONSTANT)
    Q_PROPERTY(QString resourceId READ resourceId CONSTANT)
    Q_PROPERTY(QUrl component READ component CONSTANT)
    Q_PROPERTY(QVariantMap query READ query NOTIFY queryChanged)
    Q_PROPERTY(QVariantMap initialProperties READ initialProperties CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool pluginOwned READ pluginOwned CONSTANT)
public:
    QString name() const
    {
        return m_name;
    }
    QString key() const
    {
        return m_key;
    }
    QString instanceId() const
    {
        return m_instance;
    }
    QString resourceId() const
    {
        return m_resource;
    }
    QUrl component() const
    {
        return m_component;
    }
    QVariantMap initialProperties() const
    {
        return m_initialProperties;
    }
    QVariantMap query() const
    {
        return m_query;
    }
    bool pluginOwned() const
    {
        return m_pluginOwned;
    }
    bool active() const
    {
        return m_active;
    }
signals:
    void activeChanged();
    void queryChanged();

private:
    friend class RouteController;
    explicit RouteContext(QObject *parent) : QObject(parent) {}
    QString m_name, m_key, m_instance, m_canonical, m_resource;
    QUrl m_component;
    QVariantMap m_query, m_initialProperties;
    bool m_active = false, m_pluginOwned = false;
    quint64 m_used = 0;
};

/// GUI-thread router and bounded LRU page cache. It caches mounted objects, not
/// JSON/visual clones, and never makes an unavailable business page into a stub.
class RouteController : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::RouteContext *current READ current NOTIFY routeChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY routeChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY routeChanged)
    Q_PROPERTY(int mountedCount READ mountedCount NOTIFY cacheChanged)
    Q_PROPERTY(bool keepAliveEnabled READ keepAliveEnabled NOTIFY cacheChanged)
    Q_PROPERTY(int keepAliveMax READ keepAliveMax NOTIFY cacheChanged)
public:
    struct Definition
    {
        QString name, path;
        QUrl component;
        QStringList tabQueryKeys;
        bool cache = true, accountScoped = true, skipHistory = false;
        // Trusted page-specific QObject dependencies; no global context properties.
        QVariantMap initialProperties;
        bool pluginOwned = false;
    };
    explicit RouteController(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    RouteContext *current() const
    {
        return m_current;
    }
    bool canGoBack() const
    {
        return m_cursor > 0;
    }
    bool canGoForward() const
    {
        return m_cursor + 1 < m_history.size();
    }
    int mountedCount() const
    {
        return int(m_pages.size());
    }
    bool keepAliveEnabled() const
    {
        return m_enabled;
    }
    int keepAliveMax() const
    {
        return m_max;
    }
    /// Trusted application/plugin registration only. QML cannot supply URLs.
    bool registerPage(Definition definition);
    /// Only external plugin definitions can be unregistered. Evicts mounted
    /// items and history before their injected plugin context is destroyed.
    quint64 registerPluginPage(Definition definition);
    bool unregisterPage(const QString &name, quint64 revision);
    Q_INVOKABLE bool available(const QString &name) const;
    Q_INVOKABLE bool navigate(const QString &name, const QVariantMap &query = {},
                              const QString &resourceId = {});
    Q_INVOKABLE bool replace(const QString &name, const QVariantMap &query = {},
                             const QString &resourceId = {});
    Q_INVOKABLE void replaceQuery(const QVariantMap &query);
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void refresh();
    void setCachePolicy(bool enabled, int maximum);
    /// Read the first shortcut from its existing owner at navigation time.
    void setHomeLayout(SidebarLayoutModel *layout);
    QString homeRoute() const;
    bool navigateHome(bool replaceCurrent = false);
    /// Explicit login, account switching and logout reset to the current first card.
    void invalidateAccount();
    /// Startup restoration keeps its landing location, but recreates every page
    /// and resets history/epoch exactly like normal account invalidation.
    void invalidateAccountPreservingRoute();
    void shutdown();
signals:
    void registryChanged();
    void routeChanged();
    void cacheChanged();
    void settingsRequested();
    void dismissOverlays();
    void navigationRejected(const QString &name);
    void pageActivated(EchoMusic::RouteContext *page);

private:
    QString homeResourceId() const;
    struct Location
    {
        QString name, resource;
        QVariantMap query;
    };
    bool goTo(const QString &name, const QVariantMap &query, const QString &resourceId,
              bool replace);
    void resetAccount(bool preserveRoute);
    bool valid(const Location &location) const;
    QString canonical(const Location &location) const;
    void present(const Location &location);
    void removePage(int index);
    void prune();
    bool cacheable(const RouteContext *page) const;
    QPointer<SidebarLayoutModel> m_homeLayout;
    QHash<QString, Definition> m_definitions;
    QList<Location> m_history;
    QList<RouteContext *> m_pages;
    QHash<QString, quint64> m_revisions, m_pluginPageRevisions;
    quint64 m_registrationRevision = 0;
    RouteContext *m_current = nullptr;
    int m_cursor = -1, m_max = 20;
    bool m_enabled = true, m_changing = false, m_closed = false;
    quint64 m_clock = 0, m_instance = 0, m_accountEpoch = 0;
};
} // namespace EchoMusic
