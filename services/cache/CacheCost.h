#pragma once
#include <QJsonArray>
#include <QJsonObject>

namespace EchoMusic
{
/// Conservative value accounting without serializing JSON on the GUI thread.
/// Shared copies, allocator pages and native resources are not process RAM bounds.
inline quint64 jsonValueCost(const QJsonValue &value)
{
    quint64 bytes = 16;
    if (value.isString())
        return bytes + quint64(value.toString().size()) * 2;
    if (value.isArray())
        for (const auto &entry : value.toArray())
            bytes += jsonValueCost(entry);
    if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            bytes += quint64(it.key().size()) * 2 + jsonValueCost(it.value());
    }
    return bytes;
}
} // namespace EchoMusic
