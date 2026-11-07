#pragma once
#include <QQuickItem>
#include <QStringList>
#include <QVariantMap>

namespace EchoMusic::EchoUiLayout
{
// One description for inspection, insertion, moving and property application.
// Positioners retain their native implicit-size behavior; they are never
// replaced with a different layout just because an extension is present.
QString kind(const QQuickItem *item);
QString engine(const QQuickItem *item);
QStringList itemProperties(const QQuickItem *container);
QStringList insertionPositions(const QQuickItem *item);
bool validate(const QQuickItem *container, const QVariantMap &values, QString &error);
bool validateInsertion(const QQuickItem *container, const QVariantMap &values, QString &error);
// Decoration is a visual child, never a row/grid/layout participant.
void setDecoration(QQuickItem *item, bool decoration);
} // namespace EchoMusic::EchoUiLayout
