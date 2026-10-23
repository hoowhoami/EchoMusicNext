#include "EchoPluginNetwork.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSslConfiguration>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>
namespace EchoMusic
{
namespace
{
bool integer(const QVariantMap &options, const QString &key, qint64 fallback, qint64 maximum,
             qint64 &value)
{
    bool ok = true;
    const auto n = options.contains(key) ? options.value(key).toDouble(&ok) : double(fallback);
    if (!ok || !std::isfinite(n) || n < 0 || n > double(maximum) || std::floor(n) != n)
        return false;
    value = qint64(n);
    return true;
}
struct Request
{
    QNetworkRequest request;
    QByteArray method, body;
    QString responseType;
    qint64 limit, timeout;
    bool verify;
};
} // namespace
class PluginNetworkWorker final : public QObject
{
public:
    explicit PluginNetworkWorker(EchoPluginNetwork &owner) : owner(owner) {}
    void start(const QString &id, Request input)
    {
        if (!network)
            network = new QNetworkAccessManager(this);
        auto *reply = network->sendCustomRequest(input.request, input.method, input.body);
        replies.insert(id, reply);
        reply->setReadBufferSize(128 * 1024);
        auto bytes = std::make_shared<QByteArray>();
        auto code = std::make_shared<QString>();
        if (!input.verify)
            connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->ignoreSslErrors(); });
        if (input.timeout) {
            auto *timer = new QTimer(reply);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, reply, [reply, code] {
                *code = "Timeout";
                reply->abort();
            });
            timer->start(int(input.timeout));
        }
        const auto read = [reply, bytes, code, limit = input.limit] {
            while (reply->isOpen() && reply->bytesAvailable() > 0) {
                // Read one byte beyond the budget to detect overflow without
                // materializing an arbitrarily large readyRead buffer.
                bytes->append(reply->read(qMin<qint64>(128 * 1024, limit + 1 - bytes->size())));
                if (bytes->size() > limit) {
                    *code = "ResponseTooLarge";
                    reply->abort();
                    break;
                }
            }
        };
        connect(reply, &QNetworkReply::readyRead, reply, read);
        connect(
            reply, &QNetworkReply::finished, this,
            [this, id, reply, bytes, code, read, type = input.responseType] {
                read();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                auto error = *code;
                QString message = error == "ResponseTooLarge"
                                      ? "HTTP response exceeds maxResponseBytes"
                                  : error == "Timeout" ? "HTTP request timed out"
                                                       : QString();
                // HTTP 4xx/5xx are responses; TLS, timeout, broken streams and
                // redirect failures are transport errors even with a status.
                const auto e = reply->error();
                const bool httpError = status && ((e >= QNetworkReply::ContentAccessDenied &&
                                                   e <= QNetworkReply::UnknownContentError) ||
                                                  (e >= QNetworkReply::InternalServerError &&
                                                   e <= QNetworkReply::UnknownServerError));
                if (error.isEmpty() && e != QNetworkReply::NoError && !httpError) {
                    error = "NetworkError";
                    message = reply->errorString();
                }
                QVariant data;
                if (error.isEmpty()) {
                    if (type == "arrayBuffer")
                        data = *bytes;
                    else if (type == "text")
                        data = QString::fromUtf8(*bytes);
                    else if (!bytes->isEmpty()) {
                        QJsonParseError parse;
                        const auto doc = QJsonDocument::fromJson('[' + *bytes + ']', &parse);
                        if (parse.error != QJsonParseError::NoError || doc.array().size() != 1) {
                            error = "DecodeError";
                            message = "HTTP response is not valid JSON";
                        } else
                            data = doc.array().first().toVariant();
                    }
                }
                QVariantMap headers;
                for (const auto &[name, value] : reply->rawHeaderPairs()) {
                    const auto key = QString::fromLatin1(name.toLower());
                    const auto text = QString::fromLatin1(value);
                    if (!headers.contains(key))
                        headers[key] = text;
                    else {
                        auto list = headers.value(key).metaType().id() == QMetaType::QStringList
                                        ? headers.value(key).toStringList()
                                        : QStringList{headers.value(key).toString()};
                        list.append(text);
                        headers[key] = list;
                    }
                }
                QVariantMap response{
                    {"url", reply->url().toString()},
                    {"status", status},
                    {"statusText", reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute)},
                    {"headers", headers},
                    {"data", data}};
                replies.remove(id);
                reply->deleteLater();
                QMetaObject::invokeMethod(&owner, [target = &owner, id, error, message, response] {
                    auto op = target->m_pending.take(id);
                    if (!op || op->finished())
                        return;
                    if (error.isEmpty())
                        op->succeed(response);
                    else
                        op->fail(error, message);
                });
            });
    }
    void cancel(const QString &id)
    {
        if (auto *reply = replies.value(id))
            reply->abort();
    }
    void shutdown()
    {
        const auto live = replies;
        for (auto *reply : live)
            reply->abort();
        thread()->quit();
    }
    EchoPluginNetwork &owner;
    QNetworkAccessManager *network = nullptr;
    QHash<QString, QNetworkReply *> replies;
};
EchoPluginNetwork::EchoPluginNetwork(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope), m_worker(new PluginNetworkWorker(*this))
{
    m_tcp = new EchoPluginTcp(scope);
    connect(m_tcp, &EchoPluginTcp::drained, this, &EchoPluginNetwork::drained);
    m_thread.setObjectName("PluginNetwork-" + scope.installationId());
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, [this] {
        m_pending.clear();
        m_drained = true;
        emit drained();
    });
}
EchoPluginNetwork::~EchoPluginNetwork()
{
    shutdown();
    m_thread.wait();
    if (!m_thread.isRunning() && !m_drained)
        m_drained = true;
}
EchoPluginOperation *EchoPluginNetwork::request(const QVariantMap &options)
{
    if (!m_scope.package().capabilities.contains("unrestrictedNetwork"))
        return m_scope.rejected("CapabilityDenied", "Declare unrestrictedNetwork capability");
    const QStringList allowed{"url",          "method",    "headers",          "body",
                              "responseType", "timeoutMs", "maxResponseBytes", "maxRedirects",
                              "decompress",   "tls",       "revision"};
    for (auto it = options.begin(); it != options.end(); ++it)
        if (!allowed.contains(it.key()))
            return m_scope.rejected("InvalidArgument", "Unknown request option: " + it.key());
    const auto invalid = [this] {
        return m_scope.rejected("InvalidArgument", "Invalid HTTP request options");
    };
    const QUrl url(options.value("url").toString());
    const auto method = options.value("method", "GET").toString().toLatin1();
    const auto type = options.value("responseType", "json").toString();
    qint64 timeout, limit, redirects;
    if (!url.isValid() || !QStringList{"http", "https"}.contains(url.scheme()) ||
        url.host().isEmpty() || !url.userInfo().isEmpty() || url.toEncoded().size() > 8192 ||
        method.isEmpty() || method.size() > 32 ||
        !QRegularExpression("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$")
             .match(QString::fromLatin1(method))
             .hasMatch() ||
        !QStringList{"json", "text", "arrayBuffer"}.contains(type) ||
        !integer(options, "timeoutMs", 30000, 24 * 60 * 60 * 1000, timeout) ||
        !integer(options, "maxResponseBytes", 32 * 1024 * 1024, 128 * 1024 * 1024, limit) ||
        !limit || !integer(options, "maxRedirects", 5, 32, redirects))
        return invalid();
    QNetworkRequest request(url);
    request.setTransferTimeout(int(timeout));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         redirects ? QNetworkRequest::NoLessSafeRedirectPolicy
                                   : QNetworkRequest::ManualRedirectPolicy);
    request.setMaximumRedirectsAllowed(int(redirects));
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    if (options.contains("headers") &&
        options.value("headers").metaType().id() != QMetaType::QVariantMap)
        return invalid();
    qsizetype headerBytes = 0;
    const auto headers = options.value("headers").toMap();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (!QRegularExpression("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$").match(it.key()).hasMatch() ||
            it.value().metaType().id() != QMetaType::QString)
            return invalid();
        const auto value = it.value().toString();
        if (value.contains('\r') || value.contains('\n') || value.contains('\0'))
            return invalid();
        headerBytes += it.key().size() + value.toUtf8().size();
        request.setRawHeader(it.key().toLatin1(), value.toUtf8());
    }
    if (headerBytes > 32768)
        return invalid();
    if (options.contains("decompress") &&
        options.value("decompress").metaType().id() != QMetaType::Bool)
        return invalid();
    // Qt negotiates and decompresses when it supplies Accept-Encoding. A caller
    // explicitly setting that header owns the corresponding wire decoding.
    if (!options.value("decompress", true).toBool() && !request.hasRawHeader("Accept-Encoding"))
        request.setRawHeader("Accept-Encoding", "identity");
    const auto tls = options.value("tls").toMap();
    if (options.contains("tls") && options.value("tls").metaType().id() != QMetaType::QVariantMap)
        return invalid();
    for (auto it = tls.begin(); it != tls.end(); ++it)
        if (it.key() != "rejectUnauthorized" && it.key() != "servername")
            return invalid();
    if (tls.contains("rejectUnauthorized") &&
        tls.value("rejectUnauthorized").metaType().id() != QMetaType::Bool)
        return invalid();
    if (tls.contains("servername")) {
        const auto name = tls.value("servername").toString();
        if (name.isEmpty() || name.size() > 253 ||
            name.contains(QRegularExpression("[\\s/\\r\\n]")))
            return invalid();
        request.setPeerVerifyName(name);
    }
    QByteArray body;
    if (options.contains("body")) {
        const auto input = options.value("body");
        if (input.metaType().id() == QMetaType::QByteArray)
            body = input.toByteArray();
        else if (input.metaType().id() == QMetaType::QString)
            body = input.toString().toUtf8();
        else if (input.metaType().id() == QMetaType::QVariantMap ||
                 input.metaType().id() == QMetaType::QVariantList) {
            body = QJsonDocument::fromVariant(input).toJson(QJsonDocument::Compact);
            if (!request.hasRawHeader("Content-Type"))
                request.setRawHeader("Content-Type", "application/json");
        } else
            return invalid();
        if (body.size() > 8 * 1024 * 1024)
            return m_scope.rejected("QuotaExceeded", "Request body exceeds 8 MiB");
    }
    const auto revision = options.value("revision").toString();
    if (options.contains("revision") &&
        (options.value("revision").metaType().id() != QMetaType::QString ||
         revision != m_scope.player()->revision()))
        return m_scope.rejected("StaleTrack", "Playback track changed");
    if (m_closed)
        return m_scope.rejected("ScopeClosed", "Network scope has ended");
    if (m_pending.size() >= 16)
        return m_scope.rejected("Busy", "Too many pending HTTP requests");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (!m_thread.isRunning()) {
        m_worker->moveToThread(&m_thread);
        m_thread.start();
    }
    const auto id = op->id();
    m_pending.insert(id, op);
    op->start([this, id] {
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, id] { worker->cancel(id); });
    });
    if (options.contains("revision"))
        connect(&m_scope.host().playback, &PlaybackService::changed, op, [this, op, revision] {
            if (!op->finished() && revision != m_scope.player()->revision())
                op->cancel();
        });
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, id,
                   input = Request{request, method, body, type, limit, timeout,
                                   tls.value("rejectUnauthorized", true).toBool()}]() mutable {
            worker->start(id, std::move(input));
        });
    return op;
}
void EchoPluginNetwork::shutdown()
{
    m_tcp->shutdown();
    if (std::exchange(m_closed, true))
        return;
    if (!m_thread.isRunning()) {
        delete m_worker;
        m_worker = nullptr;
        m_drained = true;
        emit drained();
        return;
    }
    for (auto op : std::as_const(m_pending))
        if (op)
            op->cancel();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->shutdown(); });
}
} // namespace EchoMusic
