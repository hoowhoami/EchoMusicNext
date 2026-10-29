#pragma once
#include "NetworkPreferences.h"
#include "ReleaseManifest.h"
#include <QCryptographicHash>
#include <QSaveFile>
#include <QTimer>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include "SettingsStore.h"
#include <functional>
#include <memory>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Bounded, asynchronous release discovery on the same network policy as the
/// rest of the app. An empty published feed is not reported as "up to date".
class AboutService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned release service")
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString changelog READ changelog CONSTANT)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)
    Q_PROPERTY(bool prerelease READ prerelease WRITE setPrerelease NOTIFY changed)
    Q_PROPERTY(bool silentUpdate READ silentUpdate WRITE setSilentUpdate NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString releaseVersion READ releaseVersion NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(ResultState resultState READ resultState NOTIFY changed)
    Q_PROPERTY(bool resultOpen READ resultOpen NOTIFY changed)
    Q_PROPERTY(DownloadState downloadState READ downloadState NOTIFY changed)
    Q_PROPERTY(double downloadPercent READ downloadPercent NOTIFY changed)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY changed)
    Q_PROPERTY(bool manualDownload READ manualDownload NOTIFY changed)
    Q_PROPERTY(QString downloadLabel READ downloadLabel NOTIFY changed)
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
public:
    enum ResultState
    {
        NoResult,
        UpdateAvailable,
        AlreadyLatest,
        NoPublishedRelease,
        CheckFailed
    };
    Q_ENUM(ResultState)
    enum DownloadState
    {
        Idle,
        Downloading,
        Downloaded,
        Installing,
        DownloadFailed
    };
    Q_ENUM(DownloadState)
    AboutService(EchoMusic::SettingsStore &settings, NetworkPreferences &network,
                 QObject *parent = nullptr);
    ~AboutService() override;
    QString version() const;
    QString changelog() const;
    bool autoCheck() const
    {
        return m_autoCheck;
    }
    bool prerelease() const
    {
        return m_prerelease;
    }
    bool silentUpdate() const
    {
        return m_silent;
    }
    bool checking() const
    {
        return m_checking;
    }
    QString statusText() const
    {
        return m_status;
    }
    QString releaseVersion() const
    {
        return m_release.version;
    }
    QString releaseNotes() const
    {
        return m_release.notes;
    }
    ResultState resultState() const
    {
        return m_resultState;
    }
    bool resultOpen() const
    {
        return m_resultOpen;
    }
    DownloadState downloadState() const
    {
        return m_downloadState;
    }
    double downloadPercent() const
    {
        return m_total ? 100.0 * m_received / m_total : 0;
    }
    QString downloadError() const
    {
        return m_downloadError;
    }
    bool manualDownload() const;
    QString downloadLabel() const;
    QString updateMessage() const;
    bool canInstall() const;
    Q_INVOKABLE void closeResult()
    {
        m_resultOpen = false;
        emit changed();
    }
    void setAutoCheck(bool enabled);
    void setPrerelease(bool enabled);
    void setSilentUpdate(bool enabled);
    Q_INVOKABLE void check(bool automatic = false);
    Q_INVOKABLE void openRepository();
    Q_INVOKABLE void openRelease();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void install();
    void shutdown();
signals:
    void changed();
    void notice(const QString &message, bool failure);
    void updateAvailable();
    void readyToQuit();

private:
    friend class AboutServiceTestAccess;
    void read(const QUrl &url, qsizetype limit, quint64 revision,
              std::function<void(QByteArray, QString)> complete);
    void readAttempt(const QUrl &origin, const QUrl &url, qsizetype limit, quint64 revision,
                     bool fallback, std::function<void(QByteArray, QString)> complete);
    void completeCheck(bool automatic, const QString &error = {});
    void beginDownload(const QUrl &url, bool fallback, quint64 revision);
    void downloadFailed(const QString &error);
    void pollInstaller();
    EchoMusic::SettingsStore &m_settings;
    NetworkPreferences &m_policy;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    PublishedRelease m_release;
    UpdateAsset m_asset;
    QUrl m_releaseEndpoint{
        "https://api.github.com/repos/hoowhoami/EchoMusicNext/releases?per_page=100"};
    std::unique_ptr<QSaveFile> m_file;
    std::unique_ptr<QCryptographicHash> m_hash;
    QTimer m_installPoll, m_installDeadline;
    QString m_downloadError, m_cacheDirectory, m_installDirectory, m_packagePath, m_packageKind;
    qint64 m_received = 0, m_total = 0;
    DownloadState m_downloadState = Idle;
    bool m_distribution = false, m_committed = false;
    QString m_status;
    quint64 m_revision = 0;
    bool m_autoCheck, m_prerelease, m_silent, m_checking = false, m_closed = false;
    ResultState m_resultState = NoResult;
    bool m_resultOpen = false;
};
} // namespace EchoMusic
