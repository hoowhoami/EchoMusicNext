#include "ImpulseResponseInstaller.h"
#include "StorageFence.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
namespace EchoMusic
{
namespace
{
constexpr qint64 limit = 64 * 1024 * 1024;
bool permitted(const QUrl &url)
{
    const auto host = url.host().toLower();
    return url.scheme() == "https" && url.userName().isEmpty() && url.password().isEmpty() &&
           (host == "kugou.com" || host.endsWith(".kugou.com")) && url.port(443) == 443;
}
class InstallWorker final : public QObject
{
    Q_OBJECT
public:
    void install(quint64 generation, const QStringList &urls, const QString &destination)
    {
        cancel();
        m_generation = generation;
        m_destination = destination;
        m_storage = StorageFence::write(destination);
        if (!m_storage) {
            complete(false, tr("备份进行中，请稍后下载音效"));
            return;
        }
        for (const auto &url : urls)
            if (permitted(QUrl(url)) && m_urls.size() < 16)
                m_urls.append(url);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
            complete(false, tr("无法创建音效目录"));
            return;
        }
        if (!m_network)
            m_network = new QNetworkAccessManager(this);
        next();
    }
    void cancel()
    {
        if (m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        m_file.reset();
        m_storage.reset();
        m_urls.clear();
        m_url = 0;
    }
signals:
    void progress(quint64 generation, double value);
    void finished(quint64 generation, bool success, const QString &path, const QString &error);

private:
    bool consume()
    {
        if (!m_reply || !m_file)
            return false;
        while (m_reply->bytesAvailable() > 0) {
            const auto chunk = m_reply->read(65536);
            m_bytes += chunk.size();
            if (m_bytes > limit || m_file->write(chunk) != chunk.size())
                return false;
        }
        return true;
    }
    void next()
    {
        if (m_url >= m_urls.size()) {
            complete(false, tr("音效资源下载失败"));
            return;
        }
        m_file = std::make_unique<QSaveFile>(m_destination);
        if (!m_file->open(QIODevice::WriteOnly)) {
            complete(false, tr("无法保存音效资源"));
            return;
        }
        m_bytes = 0;
        m_throttle.start();
        QNetworkRequest request{QUrl(m_urls[m_url++])};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(20000);
        auto *reply = m_network->get(request);
        m_reply = reply;
        reply->setReadBufferSize(65536);
        connect(reply, &QNetworkReply::readyRead, this, [this] {
            if (!consume())
                m_reply->abort();
        });
        connect(reply, &QNetworkReply::downloadProgress, this,
                [this](qint64 received, qint64 total) {
                    if (total > limit) {
                        m_reply->abort();
                        return;
                    }
                    if (m_throttle.elapsed() >= 100) {
                        m_throttle.restart();
                        emit progress(m_generation, total > 0 ? double(received) / total : 0.);
                    }
                });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const bool valid =
                reply->error() == QNetworkReply::NoError &&
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                consume() && m_bytes > 0 && m_bytes <= limit;
            m_reply = nullptr;
            reply->deleteLater();
            if (valid && m_file->commit()) {
                complete(true, {});
                return;
            }
            m_file.reset();
            next();
        });
    }
    void complete(bool success, const QString &error)
    {
        const auto generation = m_generation;
        const auto path = m_destination;
        cancel();
        emit finished(generation, success, path, error);
    }
    std::shared_ptr<StorageFence> m_storage;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QSaveFile> m_file;
    QStringList m_urls;
    QString m_destination;
    QElapsedTimer m_throttle;
    quint64 m_generation = 0;
    qint64 m_bytes = 0;
    int m_url = 0;
};
} // namespace
ImpulseResponseInstaller::ImpulseResponseInstaller(QObject *parent)
    : QObject(parent), m_worker(new InstallWorker)
{
    m_thread.setObjectName("echo-effect-install");
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, &ImpulseResponseInstaller::closed);
    auto *worker = static_cast<InstallWorker *>(m_worker);
    connect(worker, &InstallWorker::progress, this, &ImpulseResponseInstaller::progress);
    connect(worker, &InstallWorker::finished, this, &ImpulseResponseInstaller::finished);
    m_thread.start();
}
ImpulseResponseInstaller::~ImpulseResponseInstaller()
{
    shutdown();
    m_thread.wait();
}
void ImpulseResponseInstaller::install(quint64 generation, const QStringList &urls,
                                       const QString &destination)
{
    if (!m_closing)
        QMetaObject::invokeMethod(
            m_worker, [worker = static_cast<InstallWorker *>(m_worker), generation, urls,
                       destination] { worker->install(generation, urls, destination); });
}
void ImpulseResponseInstaller::cancel()
{
    if (!m_closing)
        QMetaObject::invokeMethod(
            m_worker, [worker = static_cast<InstallWorker *>(m_worker)] { worker->cancel(); });
}
void ImpulseResponseInstaller::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    QMetaObject::invokeMethod(m_worker, [worker = static_cast<InstallWorker *>(m_worker)] {
        worker->cancel();
        QThread::currentThread()->quit();
    });
}
} // namespace EchoMusic
#include "ImpulseResponseInstaller.moc"
