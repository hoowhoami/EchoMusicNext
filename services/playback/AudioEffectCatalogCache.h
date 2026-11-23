#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>

namespace EchoMusic
{
/// Persistent public cold source. The SQL worker owns serialization, search and
/// row identities; the GUI receives metadata and bounded windows only. Signed
/// URLs and authorization are returned with an accepted network page, never saved.
class AudioEffectCatalogCache final : public QObject
{
    Q_OBJECT
public:
    struct State;
    struct Projection
    {
        struct Change
        {
            enum Kind
            {
                Insert,
                Remove,
                Move
            };
            Kind kind;
            int first, last, destination = 0;
        };
        QList<Change> changes;
        int count = 0, anchor = -1;
        bool append = false, incremental = false;
        QString error;
    };
    using Rows = std::function<void(QJsonArray, QString)>;
    explicit AudioEffectCatalogCache(QString path, QObject *parent = nullptr);
    ~AudioEffectCatalogCache() override;
    void load();
    void ingest(QString scope, QJsonObject metadata, QJsonArray rows,
                std::function<void(QJsonObject, QJsonArray, QString)> done);
    void project(QString scope, QString view, QStringList terms, bool relevance, quint64 generation,
                 quint64 baseGeneration, QString anchor, std::shared_ptr<std::atomic_bool> canceled,
                 std::function<void(Projection)> done);
    void read(quint64 generation, int first, Rows done);
    void find(QString key, Rows done);
    void locate(quint64 generation, QString key, std::function<void(int)> done);
    void remove(const QString &scope);
    void shutdown();
signals:
    void loaded(const QJsonObject &scopes);
    void failed(const QString &message);
    /// Missing files were recreated; the projection is retired. Public metadata
    /// is reloaded, with empty scopes triggering the owner's original pagination.
    void invalidated();
    void closed();

private:
    friend class AudioEffectCatalogTestAccess;
    void work(std::function<void(State &)>);
    QThread m_thread;
    QObject *m_worker;
    std::shared_ptr<State> m_state;
    bool m_closing = false;
};
} // namespace EchoMusic
