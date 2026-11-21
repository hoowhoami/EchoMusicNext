#pragma once
#include "SongDatasetStore.h"
namespace EchoMusic
{
/// One projection contract shared by page catalogs and account-owned datasets.
/// Complete source records remain independent of these derived public facts.
namespace SongPresentation
{
QJsonObject projection(const QJsonObject &song);
QJsonObject value(const QJsonObject &song);
SongDatasetStore::Row row(const QJsonObject &song, qint64 ordinal, const QString &viewId = {});
SongDatasetStore::CachedRow reference(const QJsonObject &song,
                                      SongDatasetStore::Generation generation,
                                      SongDatasetStore::Reference reference);
} // namespace SongPresentation
} // namespace EchoMusic
