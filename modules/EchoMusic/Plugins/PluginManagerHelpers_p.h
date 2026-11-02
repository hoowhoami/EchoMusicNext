#pragma once
#include "SettingsKeys.h"
#include "PluginManager.h"
#include "SharingCommands.h"
#include "NetworkPreferences.h"
#include "PluginPackage.h"
#include "PluginMarketplaceStats.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QDateTime>
#include <QCryptographicHash>

namespace EchoMusic::PluginManagerDetail
{
inline const QString statisticsEndpoint =
    "https://echomusic-plugin-marketplace.hoowhoami.dpdns.org";
inline const QString official = "https://github.com/hoowhoami/EchoMusicNextPlugins";
inline QString localPluginId(const QString &directory)
{
    QFile file(directory + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024)
        return {};
    return PluginPackage::inspectManifest(file.readAll()).id;
}
inline QString raw(const QVariantMap &source, const QString &path)
{
    return "https://raw.githubusercontent.com/" + source.value("repository").toString() + "/HEAD/" +
           path;
}
inline QUrl freshGithubContentUrl(const QUrl &url)
{
    if (url.host().compare("raw.githubusercontent.com", Qt::CaseInsensitive) != 0)
        return url;
    const auto parts = url.path(QUrl::FullyEncoded).split('/', Qt::SkipEmptyParts);
    if (parts.size() < 4)
        return url;
    // GitHub resolves a moving ref and redirects to its immutable commit URL.
    // A query nonce on raw.githubusercontent.com can still hit the same CDN entry.
    auto fresh = QUrl::fromEncoded(
        ("https://github.com/" + parts[0] + '/' + parts[1] + "/raw/" + parts.mid(2).join('/'))
            .toUtf8());
    fresh.setQuery(url.query());
    return fresh;
}
inline bool sameMarketplaceSource(const QVariantMap &installed, const QVariantMap &entry)
{
    const auto origin = installed.value("origin").toMap();
    return origin.value("kind") == "marketplace" && origin.value("id") == entry.value("sourceId") &&
           origin.value("repository") == entry.value("repository");
}
inline bool availableUpdate(const QVariantMap &installed, const QVariantMap &entry)
{
    return !installed.isEmpty() && entry.value("compatible").toBool() &&
           sameMarketplaceSource(installed, entry) &&
           QVersionNumber::fromString(entry.value("version").toString()) >
               QVersionNumber::fromString(installed.value("version").toString());
}
} // namespace EchoMusic::PluginManagerDetail
