#pragma once
#include "PlaybackTrack.h"
#include "LyricParser.h"
#include <QObject>
#include <QVariantMap>
#include <functional>
namespace EchoMusic
{
/// GUI-thread resolver registry; the shared C++ parser owns its bounded worker.
/// Callback jobs and their I/O die with the requesting track/scope.
class LyricExtensions final : public QObject
{
    Q_OBJECT
public:
    using Reply = std::function<void(bool, const QVariant &)>;
    using Parsed = std::function<void(QJsonObject, QString)>;
    struct Resolver
    {
        QString key, revision;
        int order = 1000;
        bool match = false;
        std::function<bool()> valid;
        std::function<void(const QString &, const QVariantMap &, QObject *, Reply)> invoke;
        std::function<void(const QString &)> report;
    };
    struct Result
    {
        QString key, revision, source;
        QJsonObject lyrics;
    };
    explicit LyricExtensions(QObject *parent);
    bool add(Resolver resolver);
    bool hasResolvers() const
    {
        return !m_resolvers.isEmpty();
    }
    void remove(const QString &key, const QString &revision);
    bool contains(const QString &key, const QString &revision = {}) const;
    QObject *resolve(const PlaybackTrack &track, bool force, bool preserveCurrent,
                     const QString &only, QObject *owner, std::function<void(Result)> done);
    QObject *parse(const QVariantMap &document, QObject *owner, Parsed done);
    QObject *parseDownloaded(const QVariantMap &, QObject *owner, Parsed done);
    QObject *parseEmbedded(const QString &, QObject *owner, Parsed done);
    static QString validate(const QVariantMap &document);
signals:
    void changed();

private:
    LyricParser m_parser;
    QList<Resolver> m_resolvers;
};
} // namespace EchoMusic
