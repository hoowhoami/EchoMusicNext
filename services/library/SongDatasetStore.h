#pragma once
#include "SessionDatabase.h"
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>
#include <limits>

namespace EchoMusic
{
/// Rich payloads are immutable SQLite rows. Lightweight search columns are
/// independent of JSON, and query semantics remain the catalog's Qt semantics.
class SongDatasetStore final : public QObject
{
    Q_OBJECT
public:
    enum class QueryStyle
    {
        Catalog,
        SearchSong,
        SearchLyric
    };
    struct Lease;
    using Generation = std::shared_ptr<Lease>;
    struct Row
    {
        QString id, title, artist, album;
        quint64 duration = 0;
        qint64 ordinal = 0;
        QJsonObject song;
        QJsonObject presentation = {};
        QString extra = {};
    };
    struct Reference
    {
        qint64 record = 0, ordinal = 0;
        QString id;
        Generation source = {};
        bool operator==(const Reference &) const = default;
    };
    struct Snapshot
    {
        Generation generation;
        QList<Reference> rows;
    };
    struct Written
    {
        Generation generation;
        QList<Reference> rows;
        QString error;
    };
    struct Read
    {
        Generation generation;
        QList<QJsonObject> values;
        qsizetype next = 0;
        QString error;
        QList<QJsonObject> presentations = {};
        QList<Generation> leases = {};
    };
    struct View
    {
        Generation generation;
        QList<Reference> rows;
        QString error;
        QList<Generation> leases = {};
    };
    struct Identity
    {
        QString id, hash, path, completionId;
        quint64 songId = 0;
        bool playable = true, trackPlayable = false, incomplete = false;
    };
    struct CachedRow
    {
        Generation generation;
        Reference reference;
        Identity identity;
    };
    struct Presentation
    {
        qint64 record = 0;
        QString title, artist, album;
        quint64 duration = 0;
        QJsonObject value;
    };
    struct Updated
    {
        Generation generation;
        QString error;
    };
    explicit SongDatasetStore(SessionDatabase &, QObject *parent = nullptr);
    static quint64 estimateCost(const QJsonValue &);
    /// External durable sources keep only display/search placeholders here.
    /// A loader hydrates a prefix (up to 128 rows / its byte budget) on demand;
    /// it must complete on the GUI thread and honor the consumer lifetime.
    using Loader = std::function<void(QList<QJsonObject>, QObject *,
                                      std::function<void(QList<QJsonObject>, QString)>)>;
    Generation create(Loader = {});
    bool append(Generation, QList<Row>, QObject *, std::function<void(Written)>);
    bool read(Snapshot, qsizetype cursor, QObject *, std::function<void(Read)>,
              bool includePresentations = false);
    /// A viewport can read presentation JSON without decoding source resources.
    bool readPresentations(Snapshot, qsizetype cursor, QObject *, std::function<void(Read)>);
    /// Atomically replace derived presentation/columns; source JSON stays immutable.
    bool updatePresentations(Generation, QList<Presentation>, QObject *,
                             std::function<void(Updated)>);
    bool query(Generation, QString text, int field, int direction, QObject *,
               std::function<void(View)>, qint64 lastOrdinal = (std::numeric_limits<qint64>::max)(),
               QueryStyle style = QueryStyle::Catalog);
    bool query(Snapshot, QString text, int field, int direction, QObject *,
               std::function<void(View)>, QueryStyle style = QueryStyle::Catalog);

private:
    bool readValues(Snapshot, qsizetype, QObject *, std::function<void(Read)>,
                    bool presentationOnly, bool includePresentations);
    SessionDatabase &m_database;
};
} // namespace EchoMusic
