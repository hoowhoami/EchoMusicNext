#include "ApiDomains_p.h"
#include "echo_ffi.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

namespace EchoMusic
{
quint64 ApiClient::CloudClient::userCloud(quint32 page, quint32 pageSize)
{
    if (!api.m_userId || !page || pageSize > 100)
        return 0;
    return api.submit({{"operation", "user_cloud"},
                       {"query", QJsonObject{{"page", int(page)}, {"page_size", int(pageSize)}}}});
}

quint64 ApiClient::CloudClient::cloudMatch(const QString &hash)
{
    return api.m_userId ? api.submit({{"operation", "user_cloud_match"},
                                      {"query", QJsonObject{{"hash", hash}}}},
                                     true, {AuthenticationMode::CurrentAccount, true, false})
                        : 0;
}

quint64 ApiClient::CloudClient::cloudUpload(CloudUploadStage stage, QJsonObject parameters)
{
    static const QStringList stages = {"auth", "initiate", "part", "complete", "add"};
    parameters.insert("stage", stages.at(int(stage)));
    return api.m_userId ? api.submit({{"operation", "user_cloud_upload"}, {"query", parameters}},
                                     true, {AuthenticationMode::CurrentAccount, true, false})
                        : 0;
}

quint64 ApiClient::CloudClient::userCloudDelete(const QList<CloudDeleteTarget> &targets)
{
    if (!api.m_userId || targets.isEmpty() || targets.size() > 200)
        return 0;
    QJsonArray files;
    for (const auto &target : targets) {
        if (!target.kvId || target.kvId > quint64(std::numeric_limits<qint64>::max()))
            return 0;
        QJsonObject file{{"kv_id", qint64(target.kvId)}};
        if (target.albumAudioId &&
            target.albumAudioId <= quint64(std::numeric_limits<qint64>::max()))
            file.insert("album_audio_id", qint64(target.albumAudioId));
        files.append(file);
    }
    return api.submit({{"operation", "user_cloud_del"}, {"query", QJsonObject{{"files", files}}}});
}
} // namespace EchoMusic
