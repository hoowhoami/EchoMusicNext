#pragma once
#include "SongCatalogActions.h"
#include <QtTest>
#include <optional>
namespace EchoMusic
{
inline QVariantMap readMenuContext(SongCatalogActions &actions, const QString &id)
{
    QObject consumer;
    std::optional<QVariantMap> result;
    QString ticket;
    QObject::connect(&actions, &SongCatalogActions::pluginMenuContextReady, &consumer,
                     [&](const QString &ready, const QVariantMap &value) {
                         if (ready == ticket)
                             result = value;
                     });
    ticket = actions.requestPluginMenuContext(id, &consumer);
    if (!QTest::qWaitFor([&] { return result.has_value(); }, 5000)) {
        QTest::qFail("Menu context did not complete", __FILE__, __LINE__);
        return {};
    }
    return std::move(*result);
}
} // namespace EchoMusic
