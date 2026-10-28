#pragma once
#include "QrLoginService.h"
namespace EchoMusic
{
/// Canonical control/presentation seam only; not an upstream response fixture.
class QrLoginServiceTestAccess
{
public:
    static void authorize(QrLoginService &service, const QByteArray &payload)
    {
        service.m_authorization = payload;
        emit service.authorized();
    }
    static void present(QrLoginService &service, QrLoginService::State state, QString image = {})
    {
        service.clear();
        service.m_paused = true;
        service.m_state = state;
        service.m_image = std::move(image);
        if (state == QrLoginService::State::Error)
            service.m_error = QStringLiteral("二维码请求失败，请重试");
        emit service.changed();
    }
    static quint64 create(const QrLoginService &service)
    {
        return service.m_create;
    }
    static void fail(QrLoginService &service, quint64 id, const QByteArray &payload)
    {
        service.fail(id, payload);
    }
    static quint64 prepare(QrLoginService &service, ApiClient &api)
    {
        service.clear();
        service.m_revision = api.sessionRevision();
        service.m_state = QrLoginService::State::Creating;
        service.m_create = api.capabilities();
        return service.m_create;
    }
    static void succeed(QrLoginService &service, quint64 id, const QByteArray &payload)
    {
        service.succeed(id, payload);
    }
    static quint64 check(const QrLoginService &service)
    {
        return service.m_check;
    }
    static quint64 exchange(const QrLoginService &service)
    {
        return service.m_exchange;
    }
    static quint64 finish(const QrLoginService &service)
    {
        return service.m_finish;
    }
    static bool waitingForNextCheck(const QrLoginService &service)
    {
        return service.m_timer.isActive() && service.m_timer.interval() == 3000;
    }
};
} // namespace EchoMusic
