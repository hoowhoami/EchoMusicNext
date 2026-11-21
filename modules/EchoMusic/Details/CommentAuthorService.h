#pragma once
#include "ApiClient.h"
#include <QJsonArray>
#include "ValueCache.h"
#include <QSet>
namespace EchoMusic
{
/// Shared bounded author cache. No credentials in keys or QML properties.
class CommentAuthorService final : public QObject
{
    Q_OBJECT
public:
    CommentAuthorService(ApiClient &, bool network, QObject *parent = nullptr);
    ~CommentAuthorService() override;
    void ensure(const QJsonArray &comments);
    bool readyFor(const QJsonArray &comments) const;
    QJsonArray products(const QString &id) const;
    void shutdown();
signals:
    void changed();

private:
    void reset();
    void finish(quint64, const QByteArray &, bool success);
    ApiClient &m_api;
    ValueCache<QString, QJsonArray> m_cache{500};
    QSet<QString> m_inflight;
    QHash<quint64, QStringList> m_pending;
    bool m_network, m_closed = false;
};
} // namespace EchoMusic
