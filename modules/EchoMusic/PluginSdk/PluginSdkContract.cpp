#include "PluginSdkContract.h"
#include "EchoPluginContext.h"
#include "EchoPluginTask.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginUiStyles.h"
#include "EchoUiPartMount.h"
#include <QJsonArray>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMetaEnum>

namespace EchoMusic
{
namespace
{
QJsonObject describe(const QMetaObject &meta)
{
    QJsonArray properties, methods, enums;
    for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
        const auto property = meta.property(i);
        properties.append(QJsonObject{{"name", property.name()},
                                      {"type", property.typeName()},
                                      {"writable", property.isWritable()},
                                      {"constant", property.isConstant()}});
    }
    for (int i = meta.methodOffset(); i < meta.methodCount(); ++i) {
        const auto method = meta.method(i);
        if (method.access() != QMetaMethod::Public ||
            method.methodType() == QMetaMethod::Constructor)
            continue;
        QJsonArray parameters;
        for (const auto &parameter : method.parameterTypes())
            parameters.append(QString::fromUtf8(parameter));
        methods.append(QJsonObject{{"name", QString::fromUtf8(method.name())},
                                   {"returnType", method.typeName()},
                                   {"parameters", parameters},
                                   {"signal", method.methodType() == QMetaMethod::Signal}});
    }
    for (int i = meta.enumeratorOffset(); i < meta.enumeratorCount(); ++i) {
        const auto value = meta.enumerator(i);
        QJsonObject keys;
        for (int key = 0; key < value.keyCount(); ++key)
            keys.insert(value.key(key), value.value(key));
        enums.append(QJsonObject{{"name", value.name()}, {"values", keys}});
    }
    return {{"properties", properties}, {"methods", methods}, {"enums", enums}};
}
} // namespace
QJsonObject pluginSdkContract()
{
    QJsonObject types;
    const QMetaObject *publicTypes[] = {&AppearanceConfiguration::staticMetaObject,
                                        &EchoPluginAppearance::staticMetaObject,
                                        &EchoPluginAppearanceHandle::staticMetaObject,
                                        &EchoPluginFonts::staticMetaObject,
                                        &EchoPluginViewContext::staticMetaObject,
                                        &EchoPluginWindows::staticMetaObject,
                                        &EchoPluginUiParts::staticMetaObject,
                                        &EchoPluginUiStyles::staticMetaObject,
                                        &EchoUiPartRef::staticMetaObject,
                                        &EchoUiStyleRule::staticMetaObject,
                                        &EchoUiPartMount::staticMetaObject,
                                        &EchoPluginContext::staticMetaObject,
                                        &EchoPluginPlayer::staticMetaObject,
                                        &EchoPluginReadModel::staticMetaObject,
                                        &EchoPluginUi::staticMetaObject,
                                        &EchoPluginIo::staticMetaObject,
                                        &EchoPluginOperation::staticMetaObject,
                                        &EchoPluginRegistration::staticMetaObject,
                                        &EchoPluginTask::staticMetaObject,
                                        &EchoPluginRegistry::staticMetaObject,
                                        &EchoPluginAudio::staticMetaObject,
                                        &EchoPluginSpectrum::staticMetaObject,
                                        &EchoPluginSpectrumSubscription::staticMetaObject};
    for (const auto *meta : publicTypes)
        types.insert(QString::fromUtf8(meta->className()).section("::", -1), describe(*meta));
    return {{"schemaVersion", 1},
            {"hostVersion", PluginPackage::hostVersion().toString()},
            {"sdkVersion", PluginPackage::sdkVersion().toString()},
            {"packageFormat", "echomod"},
            {"qtVersion", QT_VERSION_STR},
            {"module", "echo:plugin-sdk"},
            {"qmlModule", "EchoMusic.PluginSdk"},
            {"contexts", QJsonArray{"main"}},
            {"features", QJsonArray::fromStringList(PluginPackage::supportedFeatures())},
            {"capabilities", QJsonArray::fromStringList(PluginPackage::supportedCapabilities())},
            {"types", types}};
}
} // namespace EchoMusic
