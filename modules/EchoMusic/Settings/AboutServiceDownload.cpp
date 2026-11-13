#include "AboutService.h"
#include "ExecutablePath.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
namespace EchoMusic
{
void AboutService::download()
{
    if (m_closed || m_resultState != UpdateAvailable || m_asset.name.isEmpty() ||
        m_downloadState == Downloading || m_downloadState == Downloaded ||
        m_downloadState == Installing)
        return;
    if (manualDownload()) {
        QDesktopServices::openUrl(m_policy.acceleratedGithubUrl(m_asset.url));
        return;
    }
    cancelDownload();
    const auto root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
    m_cacheDirectory = root + '/' + QUuid::createUuid().toString(QUuid::Id128);
    if (!QDir().mkpath(m_cacheDirectory)) {
        downloadFailed(tr("无法创建更新缓存目录"));
        return;
    }
    QFile::setPermissions(m_cacheDirectory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    m_packagePath = m_cacheDirectory + '/' + m_asset.name;
    m_downloadState = Downloading;
    m_total = m_asset.size;
    m_received = 0;
    m_downloadError.clear();
    const auto target = m_policy.acceleratedGithubUrl(m_asset.url);
    beginDownload(target, target != m_asset.url, ++m_revision);
    emit changed();
}
void AboutService::beginDownload(const QUrl &url, bool fallback, quint64 revision)
{
    m_file = std::make_unique<QSaveFile>(m_packagePath);
    m_file->setDirectWriteFallback(false);
    if (!m_file->open(QIODevice::WriteOnly)) {
        downloadFailed(tr("无法保存更新文件"));
        return;
    }
    m_file->setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha512);
    m_received = 0;
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("User-Agent", "EchoMusicNext/" ECHO_APP_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(request);
    reply->setReadBufferSize(256 * 1024);
    m_reply = reply;
    const auto consume = [this, reply, revision] {
        if (m_closed || revision != m_revision || !m_file)
            return;
        while (reply->bytesAvailable()) {
            const auto bytes = reply->read(256 * 1024);
            if (bytes.isEmpty())
                break;
            if (m_received + bytes.size() > m_total || m_file->write(bytes) != bytes.size()) {
                m_downloadError = tr("更新文件大小异常或磁盘写入失败");
                reply->abort();
                return;
            }
            m_hash->addData(bytes);
            m_received += bytes.size();
        }
        emit changed();
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [this, reply, revision, fallback, consume] {
        consume();
        reply->deleteLater();
        if (m_closed || revision != m_revision)
            return;
        m_reply = nullptr;
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        if (!ok && fallback && m_downloadError.isEmpty()) {
            m_file->cancelWriting();
            m_file.reset();
            beginDownload(m_asset.url, false, revision);
            return;
        }
        if (!ok || !m_downloadError.isEmpty() || m_received != m_total) {
            downloadFailed(m_downloadError.isEmpty() ? tr("更新下载失败，请重试")
                                                     : m_downloadError);
            return;
        }
        if (m_hash->result().toHex() != m_asset.sha512) {
            downloadFailed(tr("更新文件校验失败，请重新下载"));
            return;
        }
        if (!m_file->commit()) {
            downloadFailed(tr("无法保存已校验的更新文件"));
            return;
        }
        m_file.reset();
        m_hash.reset();
        m_downloadState = Downloaded;
        emit changed();
    });
}
void AboutService::downloadFailed(const QString &error)
{
    if (m_file)
        m_file->cancelWriting();
    m_file.reset();
    m_hash.reset();
    m_downloadError = error;
    m_downloadState = DownloadFailed;
    emit changed();
}
void AboutService::cancelDownload()
{
    if (m_committed || m_downloadState == Installing)
        return;
    ++m_revision;
    if (m_reply)
        m_reply->abort();
    m_reply = nullptr;
    if (m_file)
        m_file->cancelWriting();
    m_file.reset();
    m_hash.reset();
    if (!m_cacheDirectory.isEmpty())
        QDir(m_cacheDirectory).removeRecursively();
    m_cacheDirectory.clear();
    m_packagePath.clear();
    m_downloadState = Idle;
    m_downloadError.clear();
    m_received = m_total = 0;
    emit changed();
}
void AboutService::install()
{
    if (m_closed || !canInstall() || m_downloadState != Downloaded ||
        !QFileInfo(m_packagePath).isFile())
        return;
    m_installDirectory =
        m_cacheDirectory + "/handoff-" + QUuid::createUuid().toString(QUuid::Id128);
#ifdef Q_OS_WIN
    const QString helperName = "echo-update-helper.exe";
#else
    const QString helperName = "echo-update-helper";
#endif
    const auto helper = m_installDirectory + '/' + helperName;
    if (!QDir().mkpath(m_installDirectory) ||
        !QFile::copy(executableDirectory() + '/' + helperName, helper) ||
        !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        m_downloadError = tr("无法准备更新安装器，请重新下载或手动安装");
        emit changed();
        return;
    }
    QStringList args{"--parent",
                     QString::number(QCoreApplication::applicationPid()),
                     "--handoff",
                     m_installDirectory,
                     "--package",
                     m_packagePath,
                     "--sha512",
                     QString::fromLatin1(m_asset.sha512),
                     "--kind",
                     m_asset.kind,
                     "--target",
                     m_asset.kind == "appimage" ? qEnvironmentVariable("APPIMAGE")
                                                : QCoreApplication::applicationFilePath(),
                     "--silent",
                     m_silent ? "1" : "0"};
    QProcess process;
    process.setProgram(helper);
    process.setArguments(args);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    if (!process.startDetached()) {
        m_downloadError = tr("更新安装器启动失败");
        emit changed();
        return;
    }
    m_downloadError.clear();
    m_downloadState = Installing;
    m_installPoll.start();
    m_installDeadline.start(90000);
    emit changed();
}
void AboutService::pollInstaller()
{
    if (m_closed || m_committed)
        return;
    if (QFile::exists(m_installDirectory + "/failed")) {
        m_installPoll.stop();
        m_installDeadline.stop();
        m_downloadState = Downloaded;
        m_downloadError = tr("更新安装准备失败，请重试或手动安装");
        emit changed();
        return;
    }
    if (QFile::exists(m_installDirectory + "/committed")) {
        QSaveFile authorize(m_installDirectory + "/quit-authorized");
        if (!authorize.open(QIODevice::WriteOnly) || !authorize.commit()) {
            m_installPoll.stop();
            m_installDeadline.stop();
            m_downloadState = Downloaded;
            m_downloadError = tr("无法确认更新交接，应用尚未退出");
            emit changed();
            return;
        }
        m_committed = true;
        m_installPoll.stop();
        m_installDeadline.stop();
        emit readyToQuit();
    } else if (QFile::exists(m_installDirectory + "/ready") &&
               !QFile::exists(m_installDirectory + "/commit")) {
        QSaveFile commit(m_installDirectory + "/commit");
        if (commit.open(QIODevice::WriteOnly))
            commit.commit();
    }
}
} // namespace EchoMusic
