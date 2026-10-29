#include "SettingsKeys.h"
#include "AboutService.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDateTime>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
namespace EchoMusic
{
namespace
{
const QUrl repository("https://github.com/hoowhoami/EchoMusicNext");
QString installKind()
{
#ifdef Q_OS_WIN
    return "nsis";
#elif defined(Q_OS_MACOS)
    return "dmg";
#else
    if (!qEnvironmentVariableIsEmpty("APPIMAGE"))
        return "appimage";
    QFile marker(QCoreApplication::applicationDirPath() + "/../share/echomusicnext/package-type");
    if (marker.open(QIODevice::ReadOnly)) {
        const auto kind = QString::fromUtf8(marker.read(32)).trimmed();
        if (kind == "deb" || kind == "rpm" || kind == "pacman")
            return kind;
    }
    return "tar";
#endif
}
} // namespace
AboutService::AboutService(EchoMusic::SettingsStore &settings, NetworkPreferences &network,
                           QObject *parent)
    : QObject(parent), m_settings(settings), m_policy(network), m_network(this),
      m_autoCheck(settings.value(EchoMusic::SettingsKeys::Updates::AutoCheck, true).toBool()),
      m_prerelease(settings.value(EchoMusic::SettingsKeys::Updates::Prerelease, false).toBool()),
      m_silent(settings.value(EchoMusic::SettingsKeys::Updates::Silent, true).toBool())
{
    m_distribution = ECHO_DISTRIBUTION_BUILD;
    m_packageKind = installKind();
    QTimer::singleShot(0, this, [this] {
        QDir root(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates");
        bool failed = false;
        for (const auto &entry :
             root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            if (!QRegularExpression("^[0-9a-f]{32}$").match(entry.fileName()).hasMatch())
                continue;
            const QDir cache(entry.absoluteFilePath());
            bool completed = false, failure = false, active = false;
            for (const auto &handoff : cache.entryInfoList(
                     {"handoff-*"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
                const auto path = handoff.absoluteFilePath();
                const bool done = QFile::exists(path + "/finished"),
                           error = QFile::exists(path + "/failed");
                completed |= done;
                failure |= error;
                active |= !done && !error &&
                          (QFile::exists(path + "/ready") || QFile::exists(path + "/committed")) &&
                          handoff.lastModified().secsTo(QDateTime::currentDateTime()) < 86400;
            }
            if (active)
                continue;
            failed |= failure && !completed;
            if (completed || failure ||
                entry.lastModified().secsTo(QDateTime::currentDateTime()) > 86400)
                QDir(entry.absoluteFilePath()).removeRecursively();
        }
        if (failed)
            emit notice(tr("上次更新安装未完成，请重新检查更新或前往发布页手动安装"), true);
    });
    m_installPoll.setInterval(50);
    m_installDeadline.setSingleShot(true);
    connect(&m_installPoll, &QTimer::timeout, this, &AboutService::pollInstaller);
    connect(&m_installDeadline, &QTimer::timeout, this, [this] {
        m_installPoll.stop();
        downloadFailed(tr("无法准备更新安装，应用尚未退出，请重试"));
    });
}
AboutService::~AboutService()
{
    shutdown();
}
QString AboutService::version() const
{
    return QStringLiteral(ECHO_APP_VERSION);
}
QString AboutService::changelog() const
{
    QFile file(":/settings/CHANGELOG.md");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return tr("无法读取更新日志");
    const auto text = QString::fromUtf8(file.readAll()).trimmed();
    return text.isEmpty() ? tr("暂无更新日志") : text;
}
void AboutService::setAutoCheck(bool enabled)
{
    if (m_autoCheck == enabled)
        return;
    m_autoCheck = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Updates::AutoCheck, enabled);
    emit changed();
}
void AboutService::setPrerelease(bool enabled)
{
    if (m_prerelease == enabled || m_downloadState == Installing)
        return;
    cancelDownload();
    m_prerelease = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Updates::Prerelease, enabled);
    ++m_revision;
    if (m_reply)
        m_reply->abort();
    m_reply = nullptr;
    m_checking = false;
    m_release = {};
    m_asset = {};
    m_status.clear();
    m_resultOpen = false;
    m_resultState = NoResult;
    emit changed();
}
void AboutService::setSilentUpdate(bool enabled)
{
    if (m_silent == enabled)
        return;
    m_silent = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Updates::Silent, enabled);
    emit changed();
}
bool AboutService::manualDownload() const
{
    return !m_distribution || m_packageKind == "dmg" || m_packageKind == "tar";
}
bool AboutService::canInstall() const
{
    return m_distribution && m_packageKind != "dmg" && m_packageKind != "tar";
}
QString AboutService::downloadLabel() const
{
    return m_packageKind == "dmg" ? tr("下载 DMG 手动更新")
           : manualDownload()     ? tr("下载后手动更新")
                                  : tr("下载更新");
}
QString AboutService::updateMessage() const
{
    if (!m_distribution)
        return tr("开发版本不支持应用内安装更新。");
    if (m_packageKind == "dmg")
        return tr("当前 macOS 发行版使用临时签名，请下载 DMG，退出 EchoMusicNext "
                  "后拖入「应用程序」替换旧版本。");
    if (m_packageKind == "tar")
        return tr("请下载适合当前系统的软件包，并手动替换安装目录。");
    if (m_packageKind == "deb" || m_packageKind == "rpm" || m_packageKind == "pacman")
        return tr("安装时将请求系统授权，通过当前软件包管理器完成更新。");
    return {};
}
void AboutService::read(const QUrl &origin, qsizetype limit, quint64 revision,
                        std::function<void(QByteArray, QString)> complete)
{
    const auto target = m_policy.acceleratedGithubUrl(origin);
    readAttempt(origin, target, limit, revision, target != origin, std::move(complete));
}
void AboutService::readAttempt(const QUrl &origin, const QUrl &url, qsizetype limit,
                               quint64 revision, bool fallback,
                               std::function<void(QByteArray, QString)> complete)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "EchoMusicNext/" ECHO_APP_VERSION);
    request.setRawHeader("Accept", "application/vnd.github+json");
    auto *reply = m_network.get(request);
    m_reply = reply;
    reply->setReadBufferSize(64 * 1024);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, limit] {
        bytes->append(reply->read(qMax(qsizetype(0), limit - bytes->size() + 1)));
        if (bytes->size() > limit)
            reply->abort();
    });
    connect(
        reply, &QNetworkReply::finished, this,
        [this, origin, reply, bytes, limit, revision, fallback, complete = std::move(complete)] {
            if (reply->isOpen())
                bytes->append(reply->read(qMax(qsizetype(0), limit - bytes->size() + 1)));
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok =
                reply->error() == QNetworkReply::NoError && status == 200 && bytes->size() <= limit;
            reply->deleteLater();
            if (revision != m_revision || m_closed)
                return;
            m_reply = nullptr;
            if (!ok && fallback) {
                readAttempt(origin, origin, limit, revision, false, std::move(complete));
                return;
            }
            complete(*bytes, ok ? QString() : tr("更新请求失败（HTTP %1）").arg(status));
        });
}

void AboutService::check(bool automatic)
{
    if (m_closed)
        return;
    if (m_downloadState == Downloading || m_downloadState == Downloaded ||
        m_downloadState == Installing || m_downloadState == DownloadFailed) {
        if (!automatic) {
            m_resultOpen = true;
            emit changed();
        }
        return;
    }
    if (checking())
        return;
    if (automatic && (!m_autoCheck || !m_distribution))
        return;
    m_checking = true;
    m_status.clear();
    const auto revision = ++m_revision;
    read(m_releaseEndpoint, 2 * 1024 * 1024, revision,
         [this, revision, automatic](QByteArray bytes, QString error) {
             m_release = {};
             m_asset = {};
             if (!error.isEmpty()) {
                 completeCheck(automatic, error);
                 return;
             }
             const auto releases = ReleaseManifest::releases(
                 bytes, ReleaseManifest::platform(), ReleaseManifest::architecture(), &error);
             for (const auto &release : releases) {
                 if (!m_prerelease &&
                     (release.prerelease || release.version.section('+', 0, 0).contains('-')))
                     continue;
                 m_release = release;
                 break;
             }
             if (!error.isEmpty() || m_release.version.isEmpty() ||
                 ReleaseManifest::compareVersions(m_release.version, version()) <= 0) {
                 completeCheck(automatic, error);
                 return;
             }
             if (m_release.manifestUrl.isEmpty()) {
                 completeCheck(automatic, tr("此版本的更新文件尚未发布完整"));
                 return;
             }
             read(m_release.manifestUrl, 128 * 1024, revision,
                  [this, automatic](QByteArray data, QString failure) {
                      if (failure.isEmpty()) {
                          const auto assets =
                              ReleaseManifest::assets(data, m_release, ReleaseManifest::platform(),
                                                      ReleaseManifest::architecture(), &failure);
                          for (const auto &asset : assets)
                              if (asset.kind == m_packageKind) {
                                  m_asset = asset;
                                  break;
                              }
                          if (failure.isEmpty() && m_asset.name.isEmpty())
                              failure = tr("此版本没有适合当前安装方式的更新包");
                      }
                      completeCheck(automatic, failure);
                  });
         });
    emit changed();
}
void AboutService::completeCheck(bool automatic, const QString &error)
{
    if (!error.isEmpty()) {
        m_status = error;
        m_resultState = CheckFailed;
    } else if (m_release.version.isEmpty()) {
        m_status = tr("当前还没有已发布的%1版本").arg(m_prerelease ? QString() : tr("正式"));
        m_resultState = NoPublishedRelease;
    } else if (ReleaseManifest::compareVersions(m_release.version, version()) > 0) {
        m_status = tr("发现新版本 v%1").arg(m_release.version);
        m_resultState = UpdateAvailable;
        emit updateAvailable();
    } else {
        m_status = tr("当前已是最新版本");
        m_resultState = AlreadyLatest;
    }
    m_checking = false;
    m_resultOpen = !automatic || m_resultState == UpdateAvailable;
    emit changed();
}
void AboutService::openRepository()
{
    QDesktopServices::openUrl(repository);
}
void AboutService::openRelease()
{
    if (!m_release.url.isEmpty())
        QDesktopServices::openUrl(m_release.url);
}
void AboutService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    ++m_revision;
    m_installPoll.stop();
    m_installDeadline.stop();
    if (m_reply)
        m_reply->abort();
    m_reply = nullptr;
    if (m_file)
        m_file->cancelWriting();
    m_file.reset();
    m_checking = false;
    m_resultOpen = false;
    if (!m_committed && !m_cacheDirectory.isEmpty())
        QDir(m_cacheDirectory).removeRecursively();
}
} // namespace EchoMusic
