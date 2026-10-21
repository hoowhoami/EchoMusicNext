#pragma once
#include "ApiClient.h"
#include "TaskCenterModel.h"
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <memory>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class RouteController;
class CloudUploadServiceTestAccess;
/// Application-owned upload run, independent from disposable dialog/pickers.
/// One network stage at a time; all metadata lookups share a monotonic limiter.
class CloudUploadService final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Cloud-owned upload service")
    Q_PROPERTY(QString step READ step NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool canResume READ canResume NOTIFY changed)
    Q_PROPERTY(bool dialogRequested READ dialogRequested NOTIFY changed)
    Q_PROPERTY(bool searching READ searching NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString statusLabel READ statusLabel NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int completed READ completed NOTIFY changed)
    Q_PROPERTY(int succeeded READ succeeded NOTIFY changed)
    Q_PROPERTY(int failed READ failed NOTIFY changed)
    Q_PROPERTY(int instant READ instant NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QVariantMap manualFile READ manualFile NOTIFY changed)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
    Q_PROPERTY(bool backgroundReminderMuted MEMBER m_reminderMuted NOTIFY reminderChanged)
public:
    enum Role
    {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        FileNameRole,
        SizeRole,
        StateRole,
        ErrorRole,
        MatchedRole,
        PercentRole,
        InstantRole
    };
    struct File
    {
        QString path, name, title, artist, album, ext, hash;
        qint64 size = 0, modified = 0, duration = 0;
        QString state = "pending", error;
        QJsonObject match;
        QString standardHash;
        bool hashChecked = false, instant = false;
        qint64 sent = 0;
    };
    explicit CloudUploadService(ApiClient &, bool network, QObject *parent = nullptr);
    ~CloudUploadService() override;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_files.size());
    }
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString step() const
    {
        return m_step;
    }
    QString phase() const
    {
        return m_phase;
    }
    bool busy() const
    {
        return m_phase == "scanning" || m_phase == "matching" || m_phase == "uploading" || paused();
    }
    bool paused() const
    {
        return m_phase == "paused";
    }
    bool canResume() const
    {
        return paused() && !m_request && m_clock.elapsed() >= m_resumeAt;
    }
    bool dialogRequested() const
    {
        return m_dialogRequested;
    }
    bool searching() const
    {
        return m_lookupKind == Manual;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString statusLabel() const;
    int total() const
    {
        return int(m_files.size());
    }
    int completed() const
    {
        return succeeded() + failed();
    }
    int succeeded() const;
    int failed() const;
    int instant() const;
    double progress() const;
    QVariantMap manualFile() const;
    QVariantList candidates() const;
    Q_INVOKABLE void open(bool detail = false);
    Q_INVOKABLE void acknowledgeOpen();
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void selectFiles(const QList<QUrl> &files, bool manual = false);
    Q_INVOKABLE void selectFolder(const QUrl &folder);
    Q_INVOKABLE void searchManual(const QString &title, const QString &artist);
    Q_INVOKABLE void uploadMatch(int index);
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void reset();
    void attachTasks(TaskCenterModel &, RouteController &);
    void shutdown();
signals:
    void cancelled();
    void changed();
    void reminderChanged();
    void cloudChanged();
    void accountInvalidated();
    void notice(const QString &message, bool warning);

private:
    friend class CloudUploadServiceTestAccess;
    enum Lookup
    {
        None,
        Automatic,
        Manual,
        Hash
    };
    void scan(QList<QUrl>, bool folder, bool manual);
    void matchNext();
    void startLookup(Lookup, const QString &key);
    void dispatchLookup();
    void applyLookup(const QJsonObject &);
    void uploadNext();
    void uploadStage(ApiClient::CloudUploadStage, QJsonObject);
    void accept(quint64, const QByteArray &);
    void reject(quint64, const QByteArray &);
    void finishFile(bool success, const QString &error = {});
    void pause(const QString &message, int seconds);
    void notify();
    void publishTask();
    void cancelPending();
    static QStringList keywords(const File &);
    static double score(const File &, const QJsonObject &, bool *acceptable);
    ApiClient &m_api;
    QList<File> m_files;
    QFutureWatcher<QList<File>> m_scan;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QTimer m_timer, m_cooldown;
    QElapsedTimer m_clock;
    qint64 m_lastLookup = -2000, m_resumeAt = 0;
    quint64 m_request = 0, m_run = 0, m_scanRun = 0, m_session = 0;
    int m_index = 0, m_keyword = 0, m_part = 0;
    double m_bestScore = -1;
    QJsonObject m_best, m_transfer;
    QString m_authorization, m_storedHash;
    QHash<QString, QJsonObject> m_cache;
    Lookup m_lookupKind = None;
    QString m_lookupKey, m_step = "pick", m_phase = "idle", m_resumePhase, m_error;
    QJsonArray m_candidates;
    bool m_network = false, m_closed = false, m_manual = false, m_dialogRequested = false,
         m_reminderMuted = false;
    TaskCenterModel *m_tasks = nullptr;
    RouteController *m_router = nullptr;
    TaskCenterModel::Handle m_task;
    bool m_taskFinished = false;
    ApiClient::CloudUploadStage m_stage = ApiClient::CloudUploadStage::Auth;
};
} // namespace EchoMusic
