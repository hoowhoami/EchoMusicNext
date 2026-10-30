// SPDX-License-Identifier: GPL-3.0-only
#include "PointMesh.h"
#include <QPointer>
#include <QSGGeometry>

namespace EchoMusic
{
PointMesh::PointMesh(QObject *parent) : QQuickShaderEffectMesh(parent) {}
qint64 PointMesh::vertexBytes() const
{
    return qint64(m_count) * sizeof(QSGGeometry::TexturedPoint2D);
}
void PointMesh::setPointCount(int value)
{
    if (value < 0 || value > MaxPointCount) {
        const auto error = QStringLiteral("pointCount must be between 0 and 4096");
        if (m_error != error) {
            m_error = error;
            emit errorChanged();
        }
        return;
    }
    const bool changed = m_count != value, cleared = !m_error.isEmpty();
    m_count = value;
    m_error.clear();
    QPointer<PointMesh> guard(this);
    if (cleared)
        emit errorChanged();
    if (!guard)
        return;
    if (changed) {
        emit pointCountChanged();
        if (guard)
            emit geometryChanged();
    }
}
bool PointMesh::validateAttributes(const QList<QByteArray> &attributes, int *position)
{
    if (attributes != QList<QByteArray>{"qt_Vertex", "qt_MultiTexCoord0"}) {
        m_log = QStringLiteral("PointMesh requires qt_Vertex at 0 and qt_MultiTexCoord0 at 1");
        return false;
    }
    m_log.clear();
    if (position)
        *position = 0;
    return true;
}
QSGGeometry *PointMesh::updateGeometry(QSGGeometry *geometry, int attributes, int position,
                                       const QRectF &, const QRectF &rect)
{
    if (attributes != 2 || position != 0)
        return nullptr;
    if (!geometry)
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), m_count);
    else if (geometry->vertexCount() != m_count || geometry->indexCount())
        geometry->allocate(m_count, 0);
    geometry->setDrawingMode(QSGGeometry::DrawPoints);
    geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
    auto *vertices = geometry->vertexDataAsTexturedPoint2D();
    const auto center = rect.center();
    for (int i = 0; i < m_count; ++i)
        vertices[i].set(float(center.x()), float(center.y()), float(i), 0);
    return geometry;
}
} // namespace EchoMusic
