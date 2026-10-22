#pragma once
#include "ShareTarget.h"
#include "PluginRuntime.h"
#include "PluginListModel.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QSet>
class PluginRuntimeTest;
namespace EchoMusic
{
class NetworkPreferences;
class SharingCommands;
/// GUI-thread source/catalog owner. Requests carry catalog generations; cache
/// fallback never turns a failed refresh into success. ZIP capture/extraction and
/// immutable installation run on workers, with retirement/rollback in Runtime.
class PluginManager final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::PluginRuntime *runtime READ runtime CONSTANT)
    Q_PROPERTY(EchoMusic::PluginFileService *fileGrants READ fileGrants CONSTANT)
    Q_PROPERTY(EchoMusic::PluginListModel *catalog READ catalog CONSTANT)
    Q_PROPERTY(EchoMusic::PluginListModel *sources READ sources CONSTANT)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int localInstallCount READ localInstallCount NOTIFY changed)
    Q_PROPERTY(bool updatingAll READ updatingAll NOTIFY changed)
    Q_PROPERTY(QString updateAllLabel READ updateAllLabel NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(int catalogCount READ catalogCount NOTIFY changed)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    Q_PROPERTY(QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY changed)
    Q_PROPERTY(QString sourceError READ sourceError NOTIFY changed)
    Q_PROPERTY(QString fetchedAt READ fetchedAt NOTIFY changed)
    Q_PROPERTY(QString installingKey READ installingKey NOTIFY changed)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY changed)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY changed)
    Q_PROPERTY(QStringList sourceNames READ sourceNames NOTIFY changed)
    Q_PROPERTY(QStringList sourceIds READ sourceIds NOTIFY changed)
    Q_PROPERTY(QString shareState READ shareState NOTIFY changed)
    Q_PROPERTY(QString shareError READ shareError NOTIFY changed)
    Q_PROPERTY(QVariantMap shareTarget READ shareTarget NOTIFY changed)
    Q_PROPERTY(QVariantMap shareRecord READ shareRecord NOTIFY changed)
    Q_PROPERTY(QString shareSourceUrl READ shareSourceUrl NOTIFY changed)
public:
    PluginManager(PluginRuntime &runtime, QSettings &preferences, QObject *parent = nullptr);
    void setNetworkPreferences(NetworkPreferences *network);
    ~PluginManager() override;
    PluginFileService *fileGrants()
    {
        return m_runtime.fileService();
    }
    PluginRuntime *runtime()
    {
        return &m_runtime;
    }
    PluginListModel *catalog()
    {
        return &m_catalog;
    }
    PluginListModel *sources()
    {
        return &m_sourceModel;
    }
    bool catalogLoading() const
    {
        return m_catalogLoading;
    }
    int localInstallCount() const
    {
        return m_localBatch ? m_localTotal : 1;
    }
    bool busy() const
    {
        return m_busy;
    }
    bool updatingAll() const
    {
        return m_batch;
    }
    QString updateAllLabel() const;
    bool loaded() const
    {
        return m_loaded;
    }
    int catalogCount() const;
    int updateCount() const;
    QString search() const
    {
        return m_search;
    }
    QString sourceFilter() const
    {
        return m_sourceFilter;
    }
    QString sourceError() const;
    QString fetchedAt() const
    {
        return m_fetchedAt;
    }
    QString installingKey() const
    {
        return m_installingKey;
    }
    QVariantList sourceOptions() const;
    QString sourceSummary() const;
    QStringList sourceNames() const;
    QStringList sourceIds() const;
    void setSearch(const QString &value);
    void setSourceFilter(const QString &value);
    Q_INVOKABLE void refreshCatalog(bool force = false);
    Q_INVOKABLE bool addSource(const QString &url, const QString &name);
    Q_INVOKABLE bool patchSource(const QString &id, const QString &name, bool enabled);
    Q_INVOKABLE bool removeSource(const QString &id);
    Q_INVOKABLE bool install(const QString &key);
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE bool installLocal(const QUrl &url);
    Q_INVOKABLE bool installLocalBatch(const QList<QUrl> &urls);
    Q_INVOKABLE bool openDirectory();
    Q_INVOKABLE bool openRepository(const QString &key);
    void attachSharing(SharingCommands &);
    Q_INVOKABLE void share(const QString &key);
    Q_INVOKABLE bool openShare(const QUrl &url);
    bool openShare(const ShareTarget &target);
    Q_INVOKABLE bool acceptShareSource();
    Q_INVOKABLE bool installShared();
    QString shareState() const
    {
        return m_shareState;
    }
    QString shareError() const
    {
        return m_shareError;
    }
    QVariantMap shareTarget() const
    {
        return m_shareTarget;
    }
    QVariantMap shareRecord() const
    {
        return m_shareRecord;
    }
    QString shareSourceUrl() const
    {
        return m_shareTarget.value("sourceUrl").toString();
    }
    Q_INVOKABLE void openDocumentation();
    Q_INVOKABLE EchoMusic::EchoPluginContext *requestSettings(const QString &installation);
    void shutdown();
signals:
    void changed();
    void sourceAdded(bool success);
    void shareOpened();
    void sharedInstalled();
    void notice(const QString &message, bool failure, bool informational = false);

private:
    friend class ::PluginRuntimeTest;
    std::function<QUrl(const QUrl &)> m_resolveUrl;
    QPointer<NetworkPreferences> m_networkPreferences;
    using Completion = std::function<void(QByteArray, QString)>;
    void getAttempt(const QUrl &url, const QUrl &origin, qint64 limit, Completion complete,
                    const QByteArray &body, int timeout, bool fallback);
    void get(const QUrl &url, qint64 limit, Completion complete, const QByteArray &body = {},
             int timeout = 30000);
    void fetchSource(QVariantMap source, quint64 generation,
                     std::function<void(QVariantMap, QVariantList)> complete);
    void rebuild();
    void refreshStatistics(bool force = false);
    void reportInstallation(const QVariantMap &record, bool success);
    void save();
    void finishInstall(bool success, const QString &error);
    void startLocalNext();
    void startUpdateNext();
    void resolveShared();
    void installArchive(const QByteArray &bytes, const QVariantMap &entry);
    void extractLocal(const QString &archive, const QVariantMap &entry);
    static QVariantMap normalizeSource(const QString &url, const QString &name);
    static bool safePath(const QString &path);
    QVariantMap entry(const QString &key) const;
    QPointer<SharingCommands> m_sharing;
    PluginRuntime &m_runtime;
    QSettings &m_preferences;
    PluginListModel m_catalog, m_sourceModel;
    QNetworkAccessManager m_network;
    QSet<QNetworkReply *> m_replies;
    QVariantList m_sources, m_entries;
    QString m_search, m_sourceFilter, m_fetchedAt, m_installingKey;
    QStringList m_updateQueue;
    QList<QUrl> m_localQueue;
    QString m_pendingPluginId, m_pendingInstallation;
    int m_localTotal = 0, m_localDone = 0;
    bool m_localBatch = false, m_localFailed = false;
    QSharedPointer<QTemporaryDir> m_staging;
    quint64 m_generation = 0;
    bool m_catalogLoading = false, m_busy = false, m_loaded = false, m_closing = false;
    bool m_updateFailed = false, m_statsLoading = false, m_installWasUpdate = false;
    QVariantMap m_stats, m_shareTarget, m_shareRecord;
    QString m_shareState, m_shareError, m_shareInstallError;
    bool m_shareInstalling = false;
    qint64 m_statsAt = 0, m_statsRetryAt = 0;
    int m_statsFailures = 0, m_batchTotal = 0, m_batchDone = 0;
    bool m_batch = false;
};
} // namespace EchoMusic

namespace EchoMusic
{
struct PluginFileGrantsForeign
{
    Q_GADGET
    QML_FOREIGN(PluginFileService)
    QML_NAMED_ELEMENT(PluginFileGrants)
    QML_UNCREATABLE("Owned by PluginRuntime")
};
} // namespace EchoMusic
