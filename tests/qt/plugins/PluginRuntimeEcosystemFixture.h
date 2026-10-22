#pragma once
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginTask.h"
#include "EchoPluginUiParts.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoSongMenuRegistry.h"
#include <QQmlComponent>
using namespace EchoMusic;
using namespace EchoMusic::PluginTest;
namespace EchoMusic::PluginTest
{
inline EchoPluginContext *ecosystem(Host &host, const QByteArray &body,
                                    const QString &id = "ecosystem", const QByteArray &qml = {})
{
    const auto path = pluginPackage(
        host.temporary.path(), id, "export function activate(c){" + body + "}",
        {{"capabilities", QJsonArray{"lyricEffects", "kugouApi", "kugouVerification"}}});
    if (!qml.isEmpty())
        write(path + "/Effect.qml", qml);
    host.runtime.importDirectory(path);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId(id)) ==
                       PluginRuntime::Active;
            },
            5000))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId(id));
}
inline QJSValue js(Host &host, const QString &code)
{
    auto value = host.engine.evaluate(code);
    if (value.isError())
        qFatal("JS error: %s", qPrintable(value.toString()));
    return value;
}
inline std::unique_ptr<QObject> ecosystemWindow(Host &host, const QByteArray &body,
                                                const QVariantMap &props = {})
{
    QQmlComponent c(&host.engine);
    c.setData(body, QUrl("qrc:/ecosystem-window.qml"));
    auto object = std::unique_ptr<QObject>(c.createWithInitialProperties(props));
    if (!object)
        qWarning().noquote() << c.errorString();
    return object;
}
inline QQuickItem *ecosystemVisual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *c : root->childItems())
        if (auto *found = ecosystemVisual(c, name))
            return found;
    return nullptr;
}
} // namespace EchoMusic::PluginTest
