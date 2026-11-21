#pragma once
#include "SongCatalogModel.h"
#include "SongCatalogActions.h"
#include "SongSourceCache.h"
#include <array>
namespace EchoMusic
{
struct SearchSongStorage
{
    struct Bucket
    {
        SongCatalogModel *model = nullptr;
        SongCatalogActions *actions = nullptr;
        std::unique_ptr<SongSourceCache> cache;
        QList<SongDatasetStore::CachedRow> records;
        QJsonArray pending;
        QList<QJsonObject> presentations;
        QPointer<QObject> write;
        quint64 total = 0;
        quint32 page = 0;
        qsizetype received = 0;
    };
    struct Menu
    {
        QPointer<QObject> consumer;
        QVariantMap context;
    };
    std::array<Bucket, 6> buckets;
    QHash<QString, Menu> menus;
    quint64 ticket = 0, playIntent = 0;
    QPointer<QObject> playRead;
};
} // namespace EchoMusic
