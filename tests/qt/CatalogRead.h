#pragma once
#include "SongCatalogModel.h"
#include <QtTest>
#include <optional>
namespace EchoMusic
{
inline PlaybackTracks readCatalogTracks(SongCatalogModel *model, bool selected = false)
{
    std::optional<PlaybackTracks> result;
    QObject consumer;
    model->requestTracks(selected, &consumer,
                         [&](PlaybackTracks value) { result = std::move(value); });
    if (!QTest::qWaitFor([&] { return result.has_value(); }, 5000)) {
        QTest::qFail("Catalog snapshot did not complete", __FILE__, __LINE__);
        return {};
    }
    return std::move(*result);
}
} // namespace EchoMusic
