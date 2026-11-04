#include "ProxyResolver.h"
#include "ProxyRouting.h"
#ifdef Q_OS_LINUX
#include "LinuxSystemProxy.h"
#endif
#include "vendor/pac_js_library.h"
#include <QEventLoop>
#include <QFile>
#include <QHostInfo>
#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QTimer>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <QElapsedTimer>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace EchoMusic
{
namespace
{
std::atomic<quint64> pacRevision{0};
QUrl autoPacAddress()
{
#ifdef Q_OS_WIN
    // Chromium's DHCP PAC fetcher is Windows-only. Resolve option 252 before
    // the DNS WPAD fallback; this blocking OS discovery stays on a worker.
    wchar_t *address = nullptr;
    if (WinHttpDetectAutoProxyConfigUrl(WINHTTP_AUTO_DETECT_TYPE_DHCP, &address)) {
        const auto url =
            NetworkConfiguration::normalizedPacScript(QString::fromWCharArray(address));
        GlobalFree(address);
        if (!url.isEmpty())
            return QUrl(url);
    }
#endif
    return QUrl(QStringLiteral("http://wpad/wpad.dat"));
}
QStringList localAddresses()
{
    QStringList addresses;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            iface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        for (const auto &entry : iface.addressEntries())
            if (!entry.ip().isNull() && !entry.ip().isLoopback())
                addresses.append(entry.ip().toString());
    }
    return addresses;
}
QString loadPac(const QUrl &url, const ProxyResolveCancellation &cancelled)
{
    constexpr qsizetype maximum = 1024 * 1024;
    if (url.isLocalFile()) {
        QFile file(url.toLocalFile());
        if (file.open(QIODevice::ReadOnly) && file.size() <= maximum)
            return QString::fromUtf8(file.read(maximum));
        return {};
    }
    QNetworkAccessManager network;
    // Bootstrapping a PAC must not recursively enter the application proxy.
    network.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = network.get(request);
    QEventLoop loop;
    QTimer deadline;
    QTimer cancellation;
    cancellation.setInterval(20);
    QObject::connect(&cancellation, &QTimer::timeout, reply, [reply, cancelled] {
        if (cancelled && cancelled->load(std::memory_order_acquire))
            reply->abort();
    });
    cancellation.start();
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > maximum)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(10000);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError || reply->bytesAvailable() > maximum)
        return {};
    return QString::fromUtf8(reply->readAll());
}
QString cachedPac(const QUrl &url, const ProxyResolveCancellation &cancelled)
{
    struct Cache
    {
        QUrl address;
        QString script;
        QElapsedTimer age;
        quint64 revision = 0;
    };
    thread_local Cache cache;
    const auto revision = pacRevision.load(std::memory_order_acquire);
    if (cache.address == url && cache.revision == revision && cache.age.isValid() &&
        cache.age.elapsed() < 300000)
        return cache.script;
    const auto script = loadPac(url, cancelled);
    if (cancelled && cancelled->load(std::memory_order_acquire))
        return {};
    cache.address = url;
    cache.script = script;
    cache.revision = revision;
    cache.age.start();
    return cache.script;
}
} // namespace
QList<QHostAddress> ProxyPacDns::lookup(const QString &host)
{
    const auto stopped = [this] {
        return m_deadline.hasExpired() ||
               (m_cancelled && m_cancelled->load(std::memory_order_acquire));
    };
    if (stopped())
        return {};
    // PAC DNS helpers resolve addresses, not reverse names. In particular a
    // literal IPv6 address must not wait for an unrelated PTR lookup.
    QHostAddress literal;
    if (literal.setAddress(host))
        return {literal};
    QEventLoop loop;
    QList<QHostAddress> addresses;
    bool finished = false;
    const int id = QHostInfo::lookupHost(host, &loop, [&](const QHostInfo &result) {
        finished = true;
        addresses = result.addresses();
        loop.quit();
    });
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (stopped())
            loop.quit();
    });
    poll.start();
    if (!finished && !stopped())
        loop.exec();
    // This releases our worker and suppresses the late callback. Qt/the OS may
    // still finish an already running resolver operation in its own DNS pool.
    if (!finished)
        QHostInfo::abortHostLookup(id);
    return stopped() ? QList<QHostAddress>() : addresses;
}
QString ProxyPacDns::dnsResolve(const QString &host)
{
    for (const auto &address : lookup(host))
        if (address.protocol() == QAbstractSocket::IPv4Protocol)
            return address.toString();
    return {};
}
QString ProxyPacDns::dnsResolveEx(const QString &host)
{
    QStringList addresses;
    for (const auto &address : lookup(host))
        addresses.append(address.toString());
    return addresses.join(';');
}
QString ProxyPacDns::myIpAddress()
{
    for (const auto &address : localAddresses())
        if (QHostAddress(address).protocol() == QAbstractSocket::IPv4Protocol)
            return address;
    return QStringLiteral("127.0.0.1");
}
QString ProxyPacDns::myIpAddressEx()
{
    return localAddresses().join(';');
}
bool ProxyPacDns::isInNetEx(const QString &address, const QString &prefix)
{
    const auto subnet = QHostAddress::parseSubnet(prefix);
    return subnet.second >= 0 && QHostAddress(address).isInSubnet(subnet);
}
ProxyResolution ProxyResolver::evaluatePac(const QString &script, QUrl target,
                                           ProxyResolveCancellation cancelled)
{
    QJSEngine engine;
    ProxyPacDns dns(cancelled);
    QJSEngine::setObjectOwnership(&dns, QJSEngine::CppOwnership);
    engine.globalObject().setProperty("__dns", engine.newQObject(&dns));
    engine.evaluate(
        QStringLiteral("function dnsResolve(h){var r=__dns.dnsResolve(h);return r||null;}"
                       "function dnsResolveEx(h){return __dns.dnsResolveEx(h);}"
                       "function myIpAddress(){return __dns.myIpAddress();}"
                       "function myIpAddressEx(){return __dns.myIpAddressEx();}"
                       "function isInNetEx(a,p){return __dns.isInNetEx(a,p);}"
                       "function isPlainHostName(h){return h.indexOf('.')===-1;}"
                       "function alert(){}"));
    engine.evaluate(QString::fromLatin1(PAC_JS_LIBRARY PAC_JS_LIBRARY_EX));
    // setInterrupted is explicitly thread-safe. A private watchdog bounds user
    // JavaScript without granting the PAC any host/plugin/filesystem API.
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!condition.wait_for(lock, std::chrono::milliseconds(20), [&] { return finished; })) {
            if ((cancelled && cancelled->load(std::memory_order_acquire)) ||
                std::chrono::steady_clock::now() >= deadline) {
                engine.setInterrupted(true);
                break;
            }
        }
    });
    const auto finish = [&] {
        {
            std::lock_guard lock(mutex);
            finished = true;
        }
        condition.notify_one();
        watchdog.join();
    };
    auto evaluated = engine.evaluate(script);
    target.setUserInfo({});
    target.setFragment({});
    if (target.scheme() == "https" || target.scheme() == "wss") {
        target.setPath("/");
        target.setQuery(QString{});
    }
    auto finder = engine.globalObject().property("FindProxyForURL");
    QJSValue result;
    if (!evaluated.isError() && finder.isCallable())
        result = finder.call({target.toString(QUrl::FullyEncoded), target.host()});
    finish();
    if (engine.isInterrupted() || evaluated.isError() || result.isError() || !result.isString())
        return {{}, QObject::tr("PAC 脚本执行失败")};
    auto endpoints = ProxyRouting::pacResult(result.toString());
    return {endpoints, endpoints.isEmpty() ? QObject::tr("PAC 没有返回可用的代理") : QString()};
}
ProxyResolution ProxyResolver::resolve(const NetworkConfiguration &config, QUrl target,
                                       ProxyResolveCancellation cancelled)
{
    if (cancelled && cancelled->load(std::memory_order_acquire))
        return {{}, QObject::tr("代理解析已取消")};
    target.setUserInfo({});
    target.setFragment({});
    if (target.scheme() == "https" || target.scheme() == "wss") {
        target.setPath("/");
        target.setQuery(QString{});
    }
    using Mode = NetworkConfiguration::Mode;
    if (config.mode == Mode::Direct || ProxyRouting::implicitBypass(target)) {
        if (config.mode != Mode::FixedServers)
            return {{{}}, {}};
    }
    if (config.mode == Mode::FixedServers) {
        auto endpoints = ProxyRouting::manual(config, target);
        return {endpoints, endpoints.isEmpty() ? QObject::tr("代理规则无效") : QString()};
    }
    if (config.mode == Mode::PacScript || config.mode == Mode::AutoDetect) {
        // Explicit PAC uses exactly the supplied address. DNS WPAD discovery is
        // deliberately distinct from following the user's system proxy mode.
        const QUrl address =
            config.mode == Mode::PacScript ? QUrl(config.pacScript) : autoPacAddress();
        auto script = cachedPac(address, cancelled);
        if (script.isEmpty() && config.mode == Mode::AutoDetect &&
            address != QUrl("http://wpad/wpad.dat"))
            script = cachedPac(QUrl("http://wpad/wpad.dat"), cancelled);
        if (cancelled && cancelled->load(std::memory_order_acquire))
            return {{}, QObject::tr("代理解析已取消")};
        // EchoMusic leaves Chromium's pacMandatory=false: failed download or
        // script execution falls back to DIRECT. Manual lists remain fail-closed
        // unless DIRECT was explicitly listed (or the scheme is unmapped).
        if (script.isEmpty())
            return {{{}}, {}};
        auto resolved = evaluatePac(script, target, cancelled);
        return resolved.error.isEmpty() ? resolved : ProxyResolution{{{}}, {}};
    }
#ifdef Q_OS_LINUX
    return resolveLinuxSystemProxy(target, cancelled);
#else
    QList<NetworkProxyEndpoint> endpoints;
    for (const auto &proxy :
         QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(target))) {
        if (proxy.type() == QNetworkProxy::NoProxy) {
            endpoints.append(NetworkProxyEndpoint{});
            continue;
        }
        QString scheme;
        if (proxy.type() == QNetworkProxy::HttpProxy ||
            proxy.type() == QNetworkProxy::HttpCachingProxy)
            scheme = "http";
        if (proxy.type() == QNetworkProxy::Socks5Proxy)
            scheme = "socks5";
        if (scheme.isEmpty())
            continue;
        QUrl url;
        url.setScheme(scheme);
        url.setHost(proxy.hostName());
        url.setPort(proxy.port());
        endpoints.append({scheme == "http" ? NetworkProxyEndpoint::Kind::Http
                                           : NetworkProxyEndpoint::Kind::Socks5,
                          url});
    }
    return {endpoints, endpoints.isEmpty() ? QObject::tr("系统没有返回可用的代理") : QString()};
#endif
}
void ProxyResolver::invalidate()
{
    pacRevision.fetch_add(1, std::memory_order_acq_rel);
}
} // namespace EchoMusic
