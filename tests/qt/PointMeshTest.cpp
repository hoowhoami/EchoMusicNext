// SPDX-License-Identifier: GPL-3.0-only
#include "PointMesh.h"
#include <QPointer>
#include <QSGGeometry>
#include <QSignalSpy>
#include <QtTest>
#include <memory>
#include <limits>

using EchoMusic::PointMesh;
class PointMeshTest final : public QObject
{
    Q_OBJECT
private slots:
    void boundedAtomicCount()
    {
        PointMesh mesh;
        QSignalSpy changed(&mesh, &PointMesh::geometryChanged);
        mesh.setPointCount(PointMesh::MaxPointCount);
        QCOMPARE(mesh.pointCount(), 4096);
        QCOMPARE(mesh.vertexBytes(), 65536);
        QCOMPARE(changed.size(), 1);
        for (int invalid : {-1, 4097, std::numeric_limits<int>::max()}) {
            mesh.setPointCount(invalid);
            QCOMPARE(mesh.pointCount(), 4096);
            QCOMPARE(changed.size(), 1);
            QVERIFY(!mesh.errorString().isEmpty());
        }
        mesh.setPointCount(4096);
        QVERIFY(mesh.errorString().isEmpty());
        QCOMPARE(changed.size(), 1);
        mesh.setPointCount(0);
        QCOMPARE(mesh.vertexBytes(), 0);
        QCOMPARE(changed.size(), 2);
    }
    void requiresDeclaredVertexLayout()
    {
        PointMesh mesh;
        int position = -1;
        QVERIFY(!mesh.validateAttributes({"qt_Vertex"}, &position));
        QVERIFY(!mesh.log().isEmpty());
        QVERIFY(!mesh.validateAttributes({"qt_MultiTexCoord0", "qt_Vertex"}, &position));
        QVERIFY(mesh.validateAttributes({"qt_Vertex", "qt_MultiTexCoord0"}, &position));
        QCOMPARE(position, 0);
        QVERIFY(mesh.log().isEmpty());
    }
    void indexedPointsAndRetirement()
    {
        auto mesh = std::make_unique<PointMesh>();
        mesh->setPointCount(150);
        std::unique_ptr<QSGGeometry> geometry(
            mesh->updateGeometry(nullptr, 2, 0, {}, QRectF(10, 20, 800, 400)));
        QVERIFY(geometry);
        QCOMPARE(geometry->drawingMode(), QSGGeometry::DrawPoints);
        QCOMPARE(geometry->indexCount(), 0);
        QCOMPARE(geometry->vertexCount(), 150);
        const auto *vertices = geometry->vertexDataAsTexturedPoint2D();
        for (int i = 0; i < 150; ++i) {
            QCOMPARE(vertices[i].x, 410.f);
            QCOMPARE(vertices[i].y, 220.f);
            QCOMPARE(vertices[i].tx, float(i));
            QCOMPARE(vertices[i].ty, 0.f);
        }
        const auto *identity = geometry.get();
        mesh->setPointCount(0);
        QCOMPARE(mesh->updateGeometry(geometry.get(), 2, 0, {}, {}), identity);
        QCOMPARE(geometry->vertexCount(), 0);
        QPointer<PointMesh> weak(mesh.get());
        mesh.reset();
        QVERIFY(!weak);
        // The render node, rather than the GUI mesh object, owns the geometry.
        QCOMPARE(geometry->vertexCount(), 0);
    }
    void reentrantOwnerRetirement()
    {
        auto *mesh = new PointMesh;
        QPointer<PointMesh> guard(mesh);
        connect(mesh, &PointMesh::pointCountChanged, mesh, [mesh] { delete mesh; });
        mesh->setPointCount(150);
        QVERIFY(!guard);
    }
};
QTEST_GUILESS_MAIN(PointMeshTest)
#include "PointMeshTest.moc"
