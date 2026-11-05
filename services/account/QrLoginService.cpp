#include "QrLoginService.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QPainter>
#include <utility>
namespace EchoMusic
{
namespace
{
QString displayImage(const QImage &image)
{
    // QQ, Kugou and WeChat QR images have different exterior margins.
    // Trim only whitespace, preserving every code pixel, then add a shared quiet
    // zone to the image. The QML view adds no separate card or border.
    QRect content;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) > 0 && qGray(image.pixel(x, y)) < 128)
                content |= QRect(x, y, 1, 1);
    if (content.isEmpty())
        return {};
    const int side = qMax(content.width(), content.height());
    // 204px image / 162px visible code preserves the existing QQ code size.
    // This white scan margin is image content, not a styled UI container.
    const int inset = qRound(side * 21.0 / 162.0);
    QImage canvas(side + 2 * inset, side + 2 * inset, QImage::Format_RGB32);
    canvas.fill(Qt::white);
    {
        QPainter painter(&canvas);
        painter.drawImage(
            QPoint(inset + (side - content.width()) / 2, inset + (side - content.height()) / 2),
            image.copy(content));
    }
    QByteArray png;
    QBuffer output(&png);
    if (!output.open(QIODevice::WriteOnly) || !canvas.save(&output, "PNG"))
        return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}
} // namespace
QrLoginService::QrLoginService(ApiClient &api, QObject *parent) : QObject(parent), m_api(api)
{
    m_timer.setInterval(3000);
    m_timer.setSingleShot(true); // Schedule after completion; never overlap checks.
    connect(&m_timer, &QTimer::timeout, this, &QrLoginService::poll);
    connect(&api, &ApiClient::succeeded, this, &QrLoginService::succeed);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &payload) { fail(id, payload); });
    connect(&api, &ApiClient::sessionChanged, this, [this](quint64 revision) {
        if (m_revision && revision != m_revision)
            cancel();
    });
    connect(&api, &ApiClient::closed, this, &QrLoginService::cancel);
}
QrLoginService::~QrLoginService()
{
    clear();
}
void QrLoginService::clear()
{
    m_timer.stop();
    const auto create = std::exchange(m_create, 0);
    const auto check = std::exchange(m_check, 0);
    const auto exchange = std::exchange(m_exchange, 0);
    const auto finish = std::exchange(m_finish, 0);
    m_revision = 0;
    m_key.clear();
    m_providerSession = {};
    m_image.clear();
    m_error.clear();
    m_authorization.clear();
    // Clear ownership before cancellation; direct signals cannot resume this flow.
    if (create)
        m_api.cancel(create);
    if (check)
        m_api.cancel(check);
    if (exchange)
        m_api.cancel(exchange);
    if (finish)
        m_api.cancel(finish);
}
void QrLoginService::setProvider(ApiClient::QrProvider provider)
{
    cancel();
    m_provider = provider;
}
void QrLoginService::pause()
{
    clear();
    m_paused = true;
    m_state = State::Idle;
    emit changed();
}
void QrLoginService::resume()
{
    start();
}
void QrLoginService::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    clear();
    m_revision = m_api.sessionRevision();
    m_paused = false;
    m_state = State::Creating;
    m_create = m_api.createProviderQr(m_provider);
    if (!m_create) {
        m_state = State::Error;
        m_error = QStringLiteral("无法创建二维码会话");
    }
    emit changed();
}
void QrLoginService::cancel()
{
    clear();
    m_state = State::Idle;
    emit changed();
}
void QrLoginService::poll()
{
    if (m_paused || (m_state != State::Waiting && m_state != State::Scanned) || m_check ||
        m_revision != m_api.sessionRevision())
        return;
    m_check = m_api.checkProviderQr(m_provider, m_provider == ApiClient::QrProvider::Qq
                                                    ? m_providerSession
                                                    : QJsonObject{{"key", m_key}});
    if (!m_check) {
        m_state = State::Error;
        m_error = QStringLiteral("无法检查二维码状态");
        emit changed();
    }
}
void QrLoginService::succeed(quint64 id, const QByteArray &payload)
{
    if (!id || !m_revision || m_revision != m_api.sessionRevision())
        return;
    if (id == m_exchange && id) {
        m_exchange = 0;
        m_finish = m_api.finishProviderLogin(m_provider, QJsonDocument::fromJson(payload).object());
        if (!m_finish) {
            m_state = State::Error;
            m_error = tr("无法完成授权登录");
        }
        emit changed();
        return;
    }
    if (id == m_finish && id) {
        m_finish = 0;
        clear();
        m_authorization = payload;
        m_state = State::Idle;
        emit changed();
        emit authorized();
        return;
    }
    if (id == m_create) {
        m_create = 0;
        const auto object = QJsonDocument::fromJson(payload).object();
        const auto session = object.value("session").toObject();
        const auto key = m_provider == ApiClient::QrProvider::Qq ? session.value("qrsig").toString()
                                                                 : object.value("key").toString();
        const auto image = object.value("image_data_url").toString();
        const auto prefix = m_provider == ApiClient::QrProvider::Wechat ? "data:image/jpeg;base64,"
                                                                        : "data:image/png;base64,";
        // Validate PNG/JPEG dimensions before decoding or exposing the data URL to QML.
        const auto encoded = image.mid(qsizetype(qstrlen(prefix))).toLatin1();
        auto decoded =
            QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
        QBuffer buffer(&decoded.decoded);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, m_provider == ApiClient::QrProvider::Wechat ? "jpeg" : "png");
        const auto size = reader.size();
        if (key.isEmpty() || !image.startsWith(QLatin1StringView(prefix)) ||
            image.size() > 256 * 1024 || !decoded || size.width() <= 0 || size.height() <= 0 ||
            size.width() > 1024 || size.height() > 1024) {
            m_state = State::Error;
            m_error = QStringLiteral("二维码图片无效");
            emit changed();
            return;
        }
        const auto decodedImage = reader.read();
        const auto displayed = decodedImage.isNull() ? QString{} : displayImage(decodedImage);
        if (displayed.isEmpty()) {
            m_state = State::Error;
            m_error = QStringLiteral("二维码图片无效");
            emit changed();
            return;
        }
        m_key = key;
        m_providerSession = session;
        m_image = displayed;
        m_state = State::Waiting;
        emit changed();
        // The reference checks immediately after QR creation. Only subsequent
        // waiting/scanned responses schedule the three-second completion delay.
        if (!m_paused)
            poll();
    } else if (id == m_check) {
        m_check = 0;
        const auto object = QJsonDocument::fromJson(payload).object();
        const auto value = object.value("state").toString();
        if (value == QStringLiteral("waiting") || value == QStringLiteral("scanned")) {
            m_state = value == QStringLiteral("scanned") ? State::Scanned : State::Waiting;
            if (!m_paused)
                m_timer.start();
            emit changed();
        } else if (value == QStringLiteral("provider_code") &&
                   m_provider == ApiClient::QrProvider::Wechat) {
            m_state = State::Authorizing;
            m_exchange = m_api.exchangeWechatCode(object.value("code").toString());
            if (!m_exchange) {
                m_state = State::Error;
                m_error = tr("无法完成微信授权");
            }
            emit changed();
        } else if (value == QStringLiteral("provider_authorized") &&
                   m_provider == ApiClient::QrProvider::Qq) {
            m_state = State::Authorizing;
            m_finish =
                m_api.finishProviderLogin(m_provider, object.value("authorization").toObject());
            if (!m_finish) {
                m_state = State::Error;
                m_error = tr("无法完成QQ授权");
            }
            emit changed();
        } else if (value == QStringLiteral("authorized") &&
                   object.value("authorization").isObject()) {
            const auto authorization = QJsonDocument(object.value("authorization").toObject())
                                           .toJson(QJsonDocument::Compact);
            clear();
            m_authorization = authorization;
            m_state = State::Idle;
            emit changed();
            emit authorized();
        } else if (value == QStringLiteral("expired")) {
            m_key.clear();
            m_providerSession = {};
            m_state = State::Expired;
            emit changed();
        } else {
            m_state = State::Error;
            m_error = QStringLiteral("二维码状态异常，请重新获取");
            emit changed();
        }
    }
}
void QrLoginService::fail(quint64 id, const QByteArray &payload)
{
    if (!id || !m_revision || m_revision != m_api.sessionRevision() ||
        (id != m_create && id != m_check && id != m_exchange && id != m_finish))
        return;
    m_create = m_check = m_exchange = m_finish = 0;
    m_timer.stop();
    m_state = State::Error;
    m_error = ApiClient::errorMessage(payload, tr("二维码请求失败"));
    emit changed();
}
} // namespace EchoMusic
