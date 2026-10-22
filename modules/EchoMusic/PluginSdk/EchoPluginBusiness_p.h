#pragma once
#include "ApiClient.h"
#include <QVariantMap>
#include <QVariantList>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <limits>
#include <QRegularExpression>
#include <functional>
namespace EchoMusic::PluginBusiness
{
class Arguments
{
public:
    explicit Arguments(const QVariantMap &v) : values(v) {}
    static bool identifier(const QString &key)
    {
        return key == "id" || key == "ids" || key == "files" || key.endsWith("Id") ||
               key.endsWith("Ids");
    }
    QList<quint64> identifiers(const QString &key)
    {
        QList<quint64> out;
        for (const auto &value : array(key)) {
            Arguments a({{"id", value}});
            out.append(a.number("id"));
            valid = a.done() && valid;
        }
        return out;
    }
    QVariantMap values;
    QSet<QString> used;
    bool valid = true;
    QString text(const QString &k, const QString &fallback = {})
    {
        used.insert(k);
        if (!values.contains(k))
            return fallback;
        auto v = values[k];
        if (v.metaType().id() != QMetaType::QString || v.toString().size() > 16384)
            valid = false;
        return v.toString();
    }
    quint64 number(const QString &k, quint64 fallback = 0, quint64 max = 9007199254740991ULL)
    {
        if (values.contains(k) && values[k].metaType().id() == QMetaType::QString &&
            identifier(k)) {
            used.insert(k);
            const auto text = values[k].toString();
            bool ok = false;
            auto n = text.toULongLong(&ok);
            if (!ok || text.isEmpty() || text.size() > 19 ||
                text.contains(QRegularExpression("[^0-9]")) ||
                n > quint64(std::numeric_limits<qint64>::max()))
                valid = false;
            return valid ? n : 0;
        }
        used.insert(k);
        if (QStringList{"page", "pageSize", "source", "gender", "appId", "clientVersion", "pool",
                        "remaining", "position", "privacy", "capacity", "playMode"}
                .contains(k))
            max = qMin<quint64>(max, 4294967295ULL);
        if (!values.contains(k))
            return fallback;
        bool ok;
        double v = values[k].toDouble(&ok);
        if (!ok || !std::isfinite(v) || v < 0 || v > double(max) || std::floor(v) != v)
            valid = false;
        return valid ? quint64(v) : 0;
    }
    qint64 signedNumber(const QString &k, qint64 fallback = 0)
    {
        used.insert(k);
        if (!values.contains(k))
            return fallback;
        bool ok;
        double v = values[k].toDouble(&ok);
        if (!ok || !std::isfinite(v) || std::abs(v) > 9007199254740991. || std::floor(v) != v)
            valid = false;
        return valid ? qint64(v) : 0;
    }
    bool boolean(const QString &k, bool fallback = false)
    {
        used.insert(k);
        if (!values.contains(k))
            return fallback;
        if (values[k].metaType().id() != QMetaType::Bool)
            valid = false;
        return values[k].toBool();
    }
    QStringList strings(const QString &k)
    {
        used.insert(k);
        auto v = values.value(k);
        if (!v.isValid())
            return {};
        if (!v.canConvert<QVariantList>() && !v.canConvert<QStringList>()) {
            valid = false;
            return {};
        }
        auto a = v.toList();
        if (a.size() > 1000)
            valid = false;
        QStringList out;
        for (const auto &i : a) {
            if (i.metaType().id() != QMetaType::QString || i.toString().size() > 16384)
                valid = false;
            out.append(i.toString());
        }
        return out;
    }
    QByteArray bytes(const QString &k)
    {
        used.insert(k);
        if (values[k].metaType().id() != QMetaType::QByteArray ||
            values[k].toByteArray().size() > 16 * 1024 * 1024)
            valid = false;
        return values[k].toByteArray();
    }
    QVariantMap object(const QString &k)
    {
        used.insert(k);
        if (values[k].metaType().id() != QMetaType::QVariantMap)
            valid = false;
        return values[k].toMap();
    }
    QVariantList array(const QString &k)
    {
        used.insert(k);
        if (!values.contains(k))
            return {};
        if (values[k].metaType().id() != QMetaType::QVariantList ||
            values[k].toList().size() > 1000)
            valid = false;
        return values[k].toList();
    }
    template <class T>
    T enumeration(const QString &key, const QStringList &choices, const QString &fallback)
    {
        auto value = text(key, fallback);
        int index = choices.indexOf(value);
        if (index < 0) {
            valid = false;
            index = 0;
        }
        return T(index);
    }
    bool done()
    {
        for (auto i = values.begin(); i != values.end(); ++i)
            if (!used.contains(i.key()))
                valid = false;
        return valid;
    }
};
ApiClient::CommentQuery parseCommentQuery(const QVariantMap &, bool &);
ApiClient::CommentWrite parseCommentWrite(const QVariantMap &, bool &);
ApiClient::ProfilePatch parseProfilePatch(const QVariantMap &, bool &);
ApiClient::PreferencePatch parsePreferencePatch(const QVariantMap &, bool &);
ApiClient::LoginDeviceTarget parseLoginDeviceTarget(const QVariantMap &, bool &);
ApiClient::CloudDeleteTarget parseCloudDeleteTarget(const QVariantMap &, bool &);
ApiClient::PlaylistTrackInput parsePlaylistTrackInput(const QVariantMap &, bool &);
ApiClient::PlaylistTrackPosition parsePlaylistTrackPosition(const QVariantMap &, bool &);
ApiClient::PlaylistPosition parsePlaylistPosition(const QVariantMap &, bool &);
ApiClient::PlaylistUpdate parsePlaylistUpdate(const QVariantMap &, bool &);
ApiClient::CollectionTarget parseCollectionTarget(const QVariantMap &, bool &);
ApiClient::PrivilegeResource parsePrivilegeResource(const QVariantMap &, bool &);
ApiClient::FmRequest parseFmRequest(const QVariantMap &, bool &);
ListenRoomAudio parseListenRoomAudio(const QVariantMap &, bool &);
ListenRoomProgress parseListenRoomProgress(const QVariantMap &, bool &);
ListenRoomRequest parseListenRoomRequest(const QVariantMap &, bool &);
ListenMusicRequest parseListenMusicRequest(const QVariantMap &, bool &);
ListenChatRequest parseListenChatRequest(const QVariantMap &, bool &);
bool catalog(const QString &, Arguments &, ApiClient &, std::function<quint64()> &);
bool library(const QString &, Arguments &, ApiClient &, std::function<quint64()> &);
bool social(const QString &, Arguments &, ApiClient &, std::function<quint64()> &);
bool account(const QString &, Arguments &, ApiClient &, std::function<quint64()> &);
bool media(const QString &, Arguments &, ApiClient &, std::function<quint64()> &);
} // namespace EchoMusic::PluginBusiness
