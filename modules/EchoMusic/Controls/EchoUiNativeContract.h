#pragma once
#include <QMap>
#include <QQuickItem>
#include <QVariantMap>
namespace EchoMusic::EchoUiNativeContract
{
QStringList stateProperties(QQuickItem *);
QVariantMap state(QQuickItem *);
QStringList actions(QQuickItem *);
bool invoke(QQuickItem *, const QString &, const QVariantMap &);
QMap<QString, QString> visuals(QQuickItem *);
} // namespace EchoMusic::EchoUiNativeContract
