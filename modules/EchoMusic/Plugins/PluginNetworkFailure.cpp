#include "PluginNetworkFailure.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QUrlQuery>
namespace EchoMusic::PluginNetworkFailure
{
QString address(const QUrl &input)
{
    auto url = input.adjusted(QUrl::RemoveUserInfo);
    const QUrlQuery query(url);
    QUrlQuery cleaned;
    bool filtered = false;
    const QStringList credentials{"token",  "accesstoken",   "accesskey", "apikey",
                                  "key",    "authorization", "password",  "passwd",
                                  "secret", "signature",     "sig",       "credential"};
    for (const auto &item : query.queryItems(QUrl::FullyDecoded)) {
        auto name = item.first.toLower();
        name.remove('_');
        name.remove('-');
        const bool credential = credentials.contains(name);
        filtered |= credential;
        cleaned.addQueryItem(item.first, credential ? QStringLiteral("[redacted]") : item.second);
    }
    if (filtered)
        url.setQuery(cleaned);
    return url.toString();
}
namespace
{
QString safeMessage(QString text)
{
    // Keep ordinary URLs and diagnostics intact. Only URL credentials require
    // filtering before crossing into the renderer, even in an error string.
    static const QRegularExpression addresses("https?://[^\\s<>\"']+",
                                              QRegularExpression::CaseInsensitiveOption);
    const auto matches = addresses.globalMatch(text);
    QList<QRegularExpressionMatch> urls;
    for (auto it = matches; it.hasNext();)
        urls.append(it.next());
    for (auto it = urls.crbegin(); it != urls.crend(); ++it)
        text.replace(it->capturedStart(), it->capturedLength(), address(QUrl(it->captured())));
    return text.left(4096);
}
QVariantMap detail(const QString &category, const QString &label, const QString &title,
                   const QString &hint, const QUrl &url, const QString &message, int status = 0)
{
    return {{"category", category}, {"categoryLabel", label},
            {"title", title},       {"hint", hint},
            {"url", address(url)},  {"message", safeMessage(message)},
            {"httpStatus", status}, {"failedAt", QDateTime::currentMSecsSinceEpoch()}};
}
QString tr(const char *text)
{
    return QCoreApplication::translate("PluginNetworkFailure", text);
}
} // namespace
QVariantMap classify(QNetworkReply::NetworkError error, int status, const QUrl &url,
                     const QString &message, bool limited, bool rateLimited, bool offline)
{
    QString category, label, title, hint;
    if (limited) {
        category = "size-limit";
        label = tr("响应过大");
        title = tr("插件资源超过大小限制");
        hint = tr("请检查插件源地址和安装包是否正确。");
    } else if ((error >= QNetworkReply::ProxyConnectionRefusedError &&
                error <= QNetworkReply::UnknownProxyError) ||
               status == 407) {
        category = "proxy";
        label = tr("代理不可用");
        title = tr("代理服务器无法访问插件资源");
        hint = tr("请检查「设置 → 网络」中的代理地址、端口和认证信息。");
    } else if (error == QNetworkReply::HostNotFoundError) {
        category = "dns";
        label = tr("域名解析失败");
        title = tr("无法解析插件资源地址");
        hint = tr("请检查 DNS 和网络连接；访问 GitHub 不稳定时可配置代理或 GitHub 加速地址。");
    } else if (error == QNetworkReply::TimeoutError) {
        category = "timeout";
        label = tr("连接超时");
        title = tr("连接插件资源超时");
        hint = tr("请稍后重试，或检查「设置 → 网络」中的代理和 GitHub 加速地址。");
    } else if (error == QNetworkReply::SslHandshakeFailedError ||
               error == QNetworkReply::InsecureRedirectError) {
        category = "tls";
        label = tr("安全连接失败");
        title = tr("无法与插件服务器建立安全连接");
        hint = tr("请检查系统时间、证书和代理配置。");
    } else if (error == QNetworkReply::ConnectionRefusedError) {
        category = "refused";
        label = tr("连接被拒绝");
        title = tr("插件服务器拒绝了连接");
        hint = tr("请检查源地址、防火墙和代理端口是否正确。");
    } else if ((offline && status == 0) || error == QNetworkReply::NetworkSessionFailedError) {
        category = "offline";
        label = tr("网络未连接");
        title = tr("当前无法连接网络");
        hint = tr("请恢复网络连接后再刷新，已缓存的插件列表仍可浏览。");
    } else if (error == QNetworkReply::RemoteHostClosedError ||
               error == QNetworkReply::TemporaryNetworkFailureError) {
        category = "reset";
        label = tr("连接被中断");
        title = tr("与插件服务器的连接被中断");
        hint = tr("网络链路可能不稳定，请重试或检查代理与 GitHub 加速地址。");
    } else if (status == 429 || (status == 403 && rateLimited)) {
        category = "rate-limit";
        label = tr("请求被限流");
        title = tr("插件资源请求触发限流");
        hint = tr("请稍后重试，或检查当前使用的 GitHub 加速服务。");
    } else if (status == 403 || status == 401) {
        category = "forbidden";
        label = tr("访问被拒绝");
        title = tr("插件服务器拒绝访问此资源");
        hint = tr("请确认仓库和资源是否公开，并检查代理或加速服务。");
    } else if (status == 404) {
        category = "not-found";
        label = tr("资源不存在");
        title =
            url.fileName() == "echo-plugins.json" ? tr("未找到插件索引文件") : tr("未找到插件资源");
        hint = tr("请确认插件源仓库、分支、索引及安装包路径是否正确。");
    } else if (status >= 300) {
        category = "http";
        label = tr("服务端响应异常");
        title = tr("插件服务器返回异常响应");
        hint = tr("请稍后刷新；若持续失败，请检查插件源或加速服务。");
    } else if (error == QNetworkReply::OperationCanceledError) {
        category = "cancelled";
        label = tr("请求已取消");
        title = tr("插件资源请求已取消");
        hint = tr("可在需要时重新刷新或下载。");
    } else {
        category = "unknown";
        label = tr("加载失败");
        title = tr("插件资源加载失败");
        hint = tr("请查看错误详情，确认网络与插件源状态后重试。");
    }
    if (status > 0)
        title += tr("（HTTP %1）").arg(status);
    auto result = detail(category, label, title, hint, url, message, status);
    result["networkError"] = int(error);
    return result;
}
QVariantMap content(const QString &message, const QUrl &url)
{
    return detail("invalid-content", tr("插件资料无效"), tr("插件索引或清单无效"),
                  tr("请检查源仓库的索引格式、插件清单和内容是否完整。"), url, message);
}
} // namespace EchoMusic::PluginNetworkFailure
