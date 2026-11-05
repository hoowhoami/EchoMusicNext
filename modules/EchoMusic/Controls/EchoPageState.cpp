#include "EchoPageState.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QtQml/qqml.h>
#include <QQmlProperty>
#include <QJSValueIterator>
#include <cmath>

namespace EchoMusic
{
namespace
{
bool normalizeValue(QVariant &value, int depth = 0)
{
    if (depth > 8)
        return false;
    switch (value.metaType().id()) {
    case QMetaType::UnknownType:
    case QMetaType::Nullptr:
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::QString:
    case QMetaType::QStringList:
        return true;
    case QMetaType::Double:
    case QMetaType::Float:
        return std::isfinite(value.toDouble());
    case QMetaType::QVariantList: {
        auto list = value.toList();
        for (auto &entry : list)
            if (!normalizeValue(entry, depth + 1))
                return false;
        value = list;
        return true;
    }
    case QMetaType::QVariantMap: {
        auto map = value.toMap();
        for (auto &entry : map)
            if (!normalizeValue(entry, depth + 1))
                return false;
        value = map;
        return true;
    }
    default: {
        if (value.metaType() != QMetaType::fromType<QJSValue>())
            return false;
        // QML's AOT compiler can leave nested var properties as QJSValue in a
        // QVariantMap. Copy only their plain values; never retain an engine,
        // QObject, callable or cyclic JavaScript graph in the route snapshot.
        const auto script = value.value<QJSValue>();
        if (script.isQObject() || script.isCallable() || script.isQMetaObject() ||
            script.isDate() || script.isRegExp() || script.isError())
            return false;
        if (!script.isObject()) {
            if (!script.isUndefined() && !script.isNull() && !script.isBool() &&
                !script.isNumber() && !script.isString())
                return false;
            value = script.toVariant();
            return normalizeValue(value, depth);
        }
        if (script.isArray()) {
            const auto size = script.property("length").toUInt();
            if (size > 65536)
                return false;
            QVariantList list;
            for (quint32 i = 0; i < size; ++i) {
                QVariant entry = QVariant::fromValue(script.property(i));
                if (!normalizeValue(entry, depth + 1))
                    return false;
                list.append(entry);
            }
            value = list;
        } else {
            QVariantMap map;
            QJSValueIterator entries(script);
            while (entries.hasNext()) {
                entries.next();
                QVariant entry = QVariant::fromValue(entries.value());
                if (map.size() >= 65536 || !normalizeValue(entry, depth + 1))
                    return false;
                map.insert(entries.name(), entry);
            }
            value = map;
        }
        return true;
    }
    }
}
} // namespace
QVariantMap EchoPageStateStore::load(const QString &key) const
{
    return m_values.value(key);
}
bool EchoPageStateStore::save(const QString &key, const QVariantMap &value)
{
    if (!m_writable || key.isEmpty() || key.size() > 256 ||
        (!m_values.contains(key) && m_values.size() >= 64))
        return false;
    QVariant normalized = value;
    if (!normalizeValue(normalized))
        return false;
    const auto snapshot = normalized.toMap();
    if (m_values.value(key) == snapshot)
        return true;
    // This budget bounds snapshots, not the separately owned domain models.
    const auto cost =
        QJsonDocument(QJsonObject::fromVariantMap(snapshot)).toJson(QJsonDocument::Compact).size() +
        key.size() * 2;
    if (m_bytes - m_costs.value(key) + cost > 65536)
        return false;
    m_bytes += cost - m_costs.value(key);
    m_costs.insert(key, cost);
    m_values.insert(key, snapshot);
    return true;
}
EchoPageState *EchoPageState::qmlAttachedProperties(QObject *object)
{
    return new EchoPageState(object);
}
EchoPageStateStore *EchoPageState::scope() const
{
    if (m_scope)
        return m_scope;
    auto *item = qobject_cast<QQuickItem *>(parent());
    for (item = item ? item->parentItem() : nullptr; item; item = item->parentItem()) {
        auto *attached =
            qobject_cast<EchoPageState *>(qmlAttachedPropertiesObject<EchoPageState>(item, false));
        if (attached && attached->m_scope)
            return attached->m_scope;
    }
    return nullptr;
}
void EchoPageState::setScope(EchoPageStateStore *scope)
{
    if (m_scope == scope)
        return;
    m_scope = scope;
    emit scopeChanged();
}
EchoPagePresentation *EchoPageState::presentation() const
{
    if (m_presentation)
        return m_presentation;
    auto *item = qobject_cast<QQuickItem *>(parent());
    for (item = item ? item->parentItem() : nullptr; item; item = item->parentItem()) {
        auto *attached =
            qobject_cast<EchoPageState *>(qmlAttachedPropertiesObject<EchoPageState>(item, false));
        if (attached && attached->m_presentation)
            return attached->m_presentation;
    }
    return nullptr;
}
void EchoPageState::setPresentation(EchoPagePresentation *presentation)
{
    if (m_presentation == presentation)
        return;
    m_presentation = presentation;
    emit presentationChanged();
}
QVariantMap EchoPageState::load(const QString &key) const
{
    const auto *store = scope();
    return store ? store->load(key) : QVariantMap{};
}
bool EchoPageState::save(const QString &key, const QVariantMap &value)
{
    auto *store = scope();
    return store && store->save(key, value);
}
void EchoPageState::setProperties(const QStringList &properties)
{
    if (m_properties == properties)
        return;
    disconnect(parent(), nullptr, this, nullptr);
    m_properties = properties.mid(0, 32);
    m_ready = false;
    emit propertiesChanged();
}
void EchoPageState::restore()
{
    if (m_ready || parent()->objectName().isEmpty())
        return;
    const auto saved = load("properties:" + parent()->objectName());
    for (const auto &name : m_properties) {
        QQmlProperty property(parent(), name);
        if (!property.isValid() || !property.isWritable())
            continue;
        if (saved.contains(name))
            property.write(saved.value(name));
        property.connectNotifySignal(this, SLOT(capture()));
    }
    m_ready = true;
}
void EchoPageState::capture()
{
    if (!m_ready)
        return;
    QVariantMap values;
    for (const auto &name : m_properties)
        values.insert(name, QQmlProperty(parent(), name).read());
    save("properties:" + parent()->objectName(), values);
}
} // namespace EchoMusic
