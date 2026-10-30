// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QtQuick/private/qquickshadereffectmesh_p.h>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Bounded immutable point vertices for ShaderEffect, owned by its scene graph.
/// Requires qt_Vertex at location 0 and qt_MultiTexCoord0 at location 1.
class PointMesh : public QQuickShaderEffectMesh
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int pointCount READ pointCount WRITE setPointCount NOTIFY pointCountChanged)
    Q_PROPERTY(qint64 vertexBytes READ vertexBytes NOTIFY pointCountChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
public:
    static constexpr int MaxPointCount = 4096;
    explicit PointMesh(QObject *parent = nullptr);
    int pointCount() const
    {
        return m_count;
    }
    qint64 vertexBytes() const;
    QString errorString() const
    {
        return m_error;
    }
    void setPointCount(int);
    bool validateAttributes(const QList<QByteArray> &, int *) override;
    QSGGeometry *updateGeometry(QSGGeometry *, int, int, const QRectF &, const QRectF &) override;
    QString log() const override
    {
        return m_log;
    }
signals:
    void pointCountChanged();
    void errorChanged();

private:
    int m_count = 1;
    QString m_error, m_log;
};
} // namespace EchoMusic
