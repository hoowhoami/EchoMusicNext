#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

namespace EchoMusic
{
struct ApiInterceptReply
{
    int status = 0;
    QByteArray payload;
};
using ApiInterceptDone = std::function<void(ApiInterceptReply)>;
using ApiInterceptNext = std::function<void(QJsonObject, ApiInterceptDone)>;
struct ApiInterceptor
{
    quint64 sequence = 0;
    double priority = 0;
    bool active = true;
    // Return a callback that invalidates plugin delivery without cancelling a forwarded write.
    std::function<std::function<void()>(const QJsonObject &, ApiInterceptNext, ApiInterceptDone)>
        invoke;
};
/// One GUI-thread logical request. A snapshot chain and memoized next prevent
/// registration changes or faulty middleware from submitting a write twice.
class ApiInterceptChain final : public QObject
{
public:
    ApiInterceptChain(QList<std::shared_ptr<ApiInterceptor>>, QObject *parent);
    ~ApiInterceptChain() override;
    void run(QJsonObject request, ApiInterceptNext transport, ApiInterceptDone complete);
    void cancel();
    static QJsonObject publicRequest(const QJsonObject &);
    static QJsonObject applyQueryPatch(QJsonObject request, const QJsonObject &patch);

private:
    struct Frame;
    void invoke(int index, QJsonObject request, ApiInterceptDone complete);
    QList<std::shared_ptr<ApiInterceptor>> m_entries;
    QList<std::shared_ptr<Frame>> m_frames;
    ApiInterceptNext m_transport;
    bool m_active = true;
};
} // namespace EchoMusic
