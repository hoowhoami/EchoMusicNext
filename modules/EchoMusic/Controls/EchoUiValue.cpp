#include "EchoUiValue.h"
#include <QJSValue>
#include <QJSValueIterator>
#include <cmath>
namespace EchoMusic::EchoUiValue
{
QVariant json(const QVariant &input, qsizetype &budget, int depth)
{
    if (depth > 16 || budget < 4)
        return {};
    const auto take = [&](qsizetype bytes, const QVariant &value) -> QVariant {
        if (bytes > budget)
            return {};
        budget -= bytes;
        return value;
    };
    const auto type = input.metaType();
    if (type.flags().testFlag(QMetaType::PointerToQObject))
        return {};
    if (type == QMetaType::fromType<QJSValue>()) {
        const auto value = input.value<QJSValue>();
        if (value.isQObject() || value.isCallable() || value.isUndefined())
            return {};
        if (value.isNull())
            return take(4, QVariant::fromValue(nullptr));
        if (value.isBool())
            return take(4, value.toBool());
        if (value.isNumber())
            return json(value.toNumber(), budget, depth + 1);
        if (value.isString())
            return json(value.toString(), budget, depth + 1);
        if (value.isArray()) {
            const auto count = value.property("length").toUInt();
            if (count > uint(budget / 4))
                return {};
            QVariantList result;
            for (uint i = 0; i < count; ++i) {
                auto child = json(QVariant::fromValue(value.property(i)), budget, depth + 1);
                if (!child.isValid())
                    child = take(4, QVariant::fromValue(nullptr));
                if (!child.isValid())
                    return {};
                result.append(child);
            }
            return take(2, result);
        }
        if (value.isObject()) {
            QVariantMap result;
            QJSValueIterator it(value);
            int fields = 0;
            while (it.hasNext() && budget >= 4 && fields++ < 1024) {
                budget -= 2;
                it.next();
                const auto key = it.name();
                if (key.size() > budget / 2)
                    continue;
                auto child = json(QVariant::fromValue(it.value()), budget, depth + 1);
                if (child.isValid() && key.size() * 2 + 2 <= budget) {
                    budget -= key.size() * 2 + 2;
                    result.insert(key, child);
                }
            }
            return take(2, result);
        }
        return {};
    }
    switch (type.id()) {
    case QMetaType::Nullptr:
        return take(4, QVariant::fromValue(nullptr));
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return take(8, input);
    case QMetaType::Double:
    case QMetaType::Float:
        return std::isfinite(input.toDouble()) ? take(16, input.toDouble()) : QVariant();
    case QMetaType::QString:
        return take(input.toString().size() * 2 + 2, input);
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
        const auto values = input.toList();
        if (values.size() > budget / 4)
            return {};
        QVariantList result;
        for (const auto &value : values) {
            auto child = json(value, budget, depth + 1);
            if (!child.isValid())
                child = take(4, QVariant::fromValue(nullptr));
            if (!child.isValid())
                return {};
            result.append(child);
        }
        return take(2, result);
    }
    case QMetaType::QVariantMap: {
        QVariantMap result;
        const auto values = input.toMap();
        for (auto it = values.begin(); it != values.end() && budget >= 4; ++it) {
            if (it.key().size() > budget / 2)
                continue;
            auto child = json(it.value(), budget, depth + 1);
            if (child.isValid() && it.key().size() * 2 + 2 <= budget) {
                budget -= it.key().size() * 2 + 2;
                result.insert(it.key(), child);
            }
        }
        return take(2, result);
    }
    default:
        return type.flags().testFlag(QMetaType::IsEnumeration) ? take(8, input.toInt())
                                                               : QVariant();
    }
}
} // namespace EchoMusic::EchoUiValue
