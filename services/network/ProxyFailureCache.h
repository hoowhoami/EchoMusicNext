#pragma once
#include "NetworkConfiguration.h"
#include "ValueCache.h"
namespace EchoMusic
{
/// Worker-only transient state. Failed proxies move behind healthy candidates
/// for five minutes; they are never removed and DIRECT is never invented.
class ProxyFailureCache
{
public:
    void clear()
    {
        m_failed.clear();
    }
    void failed(const NetworkProxyEndpoint &endpoint)
    {
        if (endpoint.kind == NetworkProxyEndpoint::Kind::Direct)
            return;
        m_failed.insert(key(endpoint), true, 300000);
    }
    QList<NetworkProxyEndpoint> prioritize(const QList<NetworkProxyEndpoint> &endpoints)
    {
        QList<NetworkProxyEndpoint> healthy, bad;
        for (const auto &endpoint : endpoints)
            (m_failed.peek(key(endpoint)).has_value() ? bad : healthy).append(endpoint);
        healthy += bad;
        return healthy;
    }

private:
    static QString key(const NetworkProxyEndpoint &endpoint)
    {
        const auto defaultPort = endpoint.kind == NetworkProxyEndpoint::Kind::Http    ? 80
                                 : endpoint.kind == NetworkProxyEndpoint::Kind::Https ? 443
                                                                                      : 1080;
        return QString::number(int(endpoint.kind)) + '/' + endpoint.address.host().toLower() + ':' +
               QString::number(endpoint.address.port(defaultPort));
    }
    ValueCache<QString, bool> m_failed{512};
};
} // namespace EchoMusic
