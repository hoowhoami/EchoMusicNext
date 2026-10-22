#pragma once
#include <QTemporaryDir>
#include "EchoPluginContext.h"
#include "PluginDependencyGraph.h"
#include <QAbstractListModel>
#include <QSettings>
#include <QTimer>
#include <memory>
#include <vector>
namespace EchoMusic
{
/// Private Promise completion bridge. Invalidation drops callbacks; JS delivery
/// catches access to a retired QObject wrapper, so late results safely no-op.
class PluginSettlement final : public QObject
{
    Q_OBJECT
public:
    explicit PluginSettlement(QObject *parent) : QObject(parent) {}
    Q_INVOKABLE void resolved();
    Q_INVOKABLE void rejected(const QString &message);
    void invalidate();
    std::function<void(QString)> complete;
    QTimer timeout;

private:
    bool m_done = false;
};
/// Application-owned manager and QQmlEngine runtime. Every entry has a single
/// scoped context; SQL workers drain before retirement and before host shutdown.
class PluginRuntime final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY safeModeChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(int count READ rowCount NOTIFY catalogChanged)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY catalogChanged)
    Q_PROPERTY(QStringList supportedFeatures READ supportedFeatures CONSTANT)
public:
    enum State
    {
        Discovered,
        Disabled,
        Activating,
        Active,
        Deactivating,
        Failed,
        Invalid,
        Incompatible,
        Blocked
    };
    Q_ENUM(State)
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        StateRole,
        EnabledRole,
        ErrorRole,
        InstallationRole,
        RecordRole
    };
    PluginRuntime(QQmlEngine &engine, QSettings &preferences, PluginHost host, QString root,
                  bool safeMode = false);
    ~PluginRuntime() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool loading() const;
    bool safeMode() const
    {
        return m_safeMode;
    }
    QStringList supportedFeatures() const
    {
        return PluginPackage::supportedFeatures();
    }
    void start(const QStringList &imports = {});
    /// Explicit source import is authorized by the application's local plugin
    /// command/management entry. Persisted installs load even without the source.
    void importDirectory(const QString &source, bool enabled = true);
    Q_INVOKABLE bool setEnabled(const QString &installationId, bool enabled);
    Q_INVOKABLE bool reload(const QString &installationId);
    Q_INVOKABLE bool retry(const QString &installationId);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *executeCommand(const QString &installationId,
                                                               const QString &id,
                                                               const QVariantMap &arguments = {});
    EchoPluginContext *context(const QString &installationId) const;
    State state(const QString &installationId) const;
    QString directory() const
    {
        return m_root;
    }
    int enabledCount() const;
    PluginFileService *fileService() const
    {
        return m_fileService;
    }
    Q_INVOKABLE bool setSafeMode(bool enabled);
    /// Discovers direct root children, excluding internal stores, then serializes
    /// transactional local updates and reloads. Uninstall removes owned inputs.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap record(const QString &installation) const;
    QString installationForId(const QString &id) const;
    /// Copies/validates before retiring an existing scope. Failed activation rolls
    /// back the immutable package, identity and enabled preference after SQL drain.
    bool installDirectory(const QString &directory, const QVariantMap &origin = {},
                          const QVariantMap &expectedManifest = {},
                          QSharedPointer<QTemporaryDir> sourceLease = {});
    Q_INVOKABLE bool uninstall(const QString &installation, bool removeData);
    Q_INVOKABLE EchoMusic::EchoPluginContext *settingsContext(const QString &installation) const
    {
        return context(installation);
    }
    Q_INVOKABLE bool clearFailure(const QString &installation);
    void shutdown();
    /// A reversible application-backup barrier. Retire contexts and drain SQL,
    /// jobs and external resources without changing the user's enabled flags.
    bool beginMaintenance(std::function<void(bool)> ready);
    void endMaintenance();
    QVariantList backupEntries() const;
signals:
    void closed();
    void safeModeChanged();
    void catalogChanged();
    void managementStarted(const QString &installation, const QString &pluginId);
    void managementFinished(const QString &installation, bool success, const QString &error);
    void loadingChanged();
    void pluginFailed(const QString &id, const QString &message);
    void notice(const QString &message, bool warning);

private:
    struct Entry
    {
        PluginPackage package, pendingPackage, previousPackage;
        QVariantMap origin, previousOrigin, failure;
        enum Mutation
        {
            None,
            Updating,
            Rollback,
            Removing
        } mutation = None;
        bool snapshotReady = false, dataRestored = false, applying = false, newInstall = false,
             removeData = false;
        QString previousSource, transactionError;
        QString source, error;
        bool reloadPending = false;
        bool retirementPending = false;
        bool enabled = false, deactivateDone = false;
        State state = Discovered;
        QPointer<EchoPluginContext> context;
        QJSValue module;
        QPointer<PluginSettlement> settlement;
        State afterRetire = Disabled;
    };
    void discoverRoot(bool reloadInstalled);
    void advanceRefresh();
    void importResolved(const QString &directory, const QString &source,
                        const QString &installation, bool enabled);
    void load(Entry &entry, const QString &directory);
    Entry *find(const QString &installation) const;
    void changed(Entry &entry);
    void persist();
    void activate(Entry &entry);
    PluginDependencyGraph dependencyPlan(const PluginPackage *replacement = nullptr) const;
    void queueReconcile();
    void reconcile();
    bool dependsOn(const Entry &consumer, const QString &provider) const;
    void advanceRetirements();
    void retire(Entry &entry, State terminal, QString error = {});
    void finishRetire(Entry &entry);
    void settle(Entry &entry, const QJSValue &value, std::function<void(QString)> complete);
    void checkClosed();
    void checkMaintenance();
    void applyMutation(Entry &entry);
    void finishMutation(Entry &entry);
    void removeEntry(Entry &entry);
    void discardSnapshot(const QString &backup, std::function<void()> complete = {});
    QQmlEngine &m_engine;
    QSettings &m_preferences;
    PluginHost m_host;
    PluginFileService *m_fileService = nullptr;
    bool m_fileShutdown = false;
    QString m_root, m_sdkError;
    bool m_safeMode = false, m_started = false, m_closing = false, m_closed = false;
    bool m_loadingNotification = false, m_refreshing = false, m_startScanPending = false;
    bool m_refreshNotice = false;
    bool m_reconcileQueued = false;
    QStringList m_refreshSources, m_refreshInstallations;
    int m_imports = 0;
    bool m_maintenance = false;
    std::function<void(bool)> m_maintenanceReady;
    quint64 m_generation = 0;
    std::vector<std::unique_ptr<Entry>> m_entries;
};
} // namespace EchoMusic
