#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <memory>

namespace EchoMusic
{
/// Incremental public metadata cache. SQL and serialization belong to one
/// worker; credentials, signed download URLs and authorization never enter it.
class AudioEffectCatalogCache final : public QObject
{
    Q_OBJECT
public:
    explicit AudioEffectCatalogCache(QString path, QObject *parent = nullptr);
    ~AudioEffectCatalogCache() override;
    void load();
    void save(const QString &scope, const QJsonObject &metadata, const QJsonArray &rows,
              bool complete);
    void remove(const QString &scope);
    void shutdown();
signals:
    void loaded(const QJsonObject &scopes);
    void failed(const QString &message);
    void closed();

private:
    struct State;
    QThread m_thread;
    QObject *m_worker;
    std::shared_ptr<State> m_state;
    bool m_closing = false;
};
} // namespace EchoMusic
