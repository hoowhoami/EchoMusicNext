#pragma once
#include <QtQuick/private/qquickshadereffect_p.h>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Qt's shader bindings and mesh, with explicit premultiplied additive composition.
/// The implementation is pinned to the application's exact Qt 6.12 build.
class NativeShaderEffect : public QQuickShaderEffect
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString composition READ composition WRITE setComposition NOTIFY compositionChanged)
public:
    explicit NativeShaderEffect(QQuickItem *parent = nullptr) : QQuickShaderEffect(parent) {}
    QString composition() const
    {
        return m_additive ? QStringLiteral("additive") : QStringLiteral("source-over");
    }
    void setComposition(const QString &);
signals:
    void compositionChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;

private:
    bool m_additive = false;
};
} // namespace EchoMusic
