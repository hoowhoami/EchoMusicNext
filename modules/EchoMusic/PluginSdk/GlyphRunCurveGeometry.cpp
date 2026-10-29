#define QT_USE_QSTRINGBUILDER
#include <QStringBuilder>
#include "GlyphRunCurveGeometry_p.h"
#include <QtQuick/private/qsgcurveprocessor_p.h>
#include <QtQuick/private/qsgcurvefillnode_p.h>
QSGCurveFillNode *makeGlyphCurveNode(const QPainterPath &path)
{
    auto *fill = new QSGCurveFillNode;
    fill->setColor(Qt::white);
    auto quad = QQuadPath::fromPainterPath(path);
    quad.setFillRule(Qt::WindingFill);
    quad = quad.subPathsClosed();
    quad.addCurvatureData();
    QSGCurveProcessor::solveOverlaps(quad);
    QSGCurveProcessor::processFill(quad, Qt::WindingFill,
                                   [fill](const std::array<QVector2D, 3> &vertices,
                                          const std::array<QVector2D, 3> &normals,
                                          QSGCurveProcessor::uvForPointCallback uv) {
                                       fill->appendTriangle(vertices, normals, uv);
                                   });
    fill->cookGeometry();
    return fill;
}
