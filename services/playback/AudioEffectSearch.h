#pragma once
#include <QVariantMap>
#include <QStringList>
namespace EchoMusic::AudioEffectSearch
{
QString normalize(const QString &text);
void prepare(QVariantMap &row);
/// Negative means no match. Lower ranks are more relevant; ties keep source order.
int rank(const QVariantMap &row, const QStringList &terms);
} // namespace EchoMusic::AudioEffectSearch
