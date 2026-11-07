#pragma once
#include <QVariant>
namespace EchoMusic::EchoUiValue
{
// Bounded JSON values for public UI state/model facades. Never exports QObjects.
QVariant json(const QVariant &, qsizetype &budget, int depth = 0);
} // namespace EchoMusic::EchoUiValue
