#pragma once
#include <QJSEngine>
#include <QVariant>

namespace EchoMusic::PluginScript
{
inline bool isContainer(const QVariant &value)
{
    const auto type = value.metaType().id();
    return type == QMetaType::QVariantMap || type == QMetaType::QVariantHash ||
           type == QMetaType::QVariantList || type == QMetaType::QStringList;
}
// QVariant sequence wrappers do not satisfy Array.isArray(). Construct native
// containers at SDK command boundaries, retaining Qt conversion for typed leaves.
inline QJSValue nativeValue(QJSEngine &engine, const QVariant &value)
{
    const auto type = value.metaType().id();
    if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
        const auto values = value.toList();
        auto array = engine.newArray(values.size());
        for (qsizetype i = 0; i < values.size(); ++i)
            array.setProperty(i, nativeValue(engine, values[i]));
        return array;
    }
    if (type == QMetaType::QVariantMap || type == QMetaType::QVariantHash) {
        auto object = engine.newObject();
        const auto prototype = object.prototype();
        // Treat every key as data, including __proto__, then restore normal
        // Object methods. No inherited setter should consume a JSON field.
        object.setPrototype(QJSValue::NullValue);
        const auto values = value.toMap();
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            object.setProperty(it.key(), nativeValue(engine, it.value()));
        object.setPrototype(prototype);
        return object;
    }
    return engine.toScriptValue(value);
}
} // namespace EchoMusic::PluginScript
