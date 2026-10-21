#pragma once
#include "ApiClient.h"
#include "PlaybackTrack.h"
#include <QHash>
namespace EchoMusic
{
class CloudAudioIndex final : public QObject
{
    Q_OBJECT
public:
    explicit CloudAudioIndex(ApiClient &, QObject *parent = nullptr);
    ~CloudAudioIndex() override;
    void ensure();
    void invalidate();
    bool loading() const
    {
        return m_request != 0;
    }
    QJsonObject source(const PlaybackTrack &) const;
    /// Cloud file record (with presentation facts) for an audio hash.
    QJsonObject byHash(const QString &hash) const;
    static QJsonObject fromRow(const QJsonObject &);
signals:
    void settled();

private:
    void accept(quint64, const QByteArray &);
    void next();
    ApiClient &m_api;
    quint64 m_request = 0, m_revision = 0, m_loaded = 0;
    quint32 m_page = 0;
    bool m_ready = false;
    QHash<QString, QJsonObject> m_index, m_pending;
};
} // namespace EchoMusic
