#include "EchoItemTranslation.h"
#include <QMatrix4x4>

namespace EchoMusic
{
EchoItemTranslation::EchoItemTranslation(QObject *parent) : QQuickTransform(parent) {}
EchoItemTranslation::~EchoItemTranslation()
{
    detach();
}
void EchoItemTranslation::detach()
{
    if (!m_target)
        return;
    auto transforms = m_target->transform();
    QList<QQuickTransform *> retained;
    for (qsizetype i = 0; i < transforms.count(&transforms); ++i)
        if (auto *entry = transforms.at(&transforms, i); entry != this)
            retained.append(entry);
    transforms.clear(&transforms);
    for (auto *entry : retained)
        transforms.append(&transforms, entry);
    m_target = nullptr;
}
void EchoItemTranslation::setTarget(QQuickItem *target)
{
    if (m_target == target)
        return;
    detach();
    m_target = target;
    if (target)
        appendToItem(target);
    emit targetChanged();
}
void EchoItemTranslation::setX(qreal x)
{
    if (m_x == x)
        return;
    m_x = x;
    update();
    emit xChanged();
}
void EchoItemTranslation::setY(qreal y)
{
    if (m_y == y)
        return;
    m_y = y;
    update();
    emit yChanged();
}
void EchoItemTranslation::applyTo(QMatrix4x4 *matrix) const
{
    matrix->translate(m_x, m_y);
}
} // namespace EchoMusic
