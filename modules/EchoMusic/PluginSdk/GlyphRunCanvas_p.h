// Internal Canvas sampling data. Contains no font engine or text-layout objects.
#pragma once
#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QVariantList>
#include <QTransform>
#include <array>
#include <atomic>
#include <memory>
#include <functional>
class QMatrix4x4;
class QQuickWindow;
class QSGNode;
namespace EchoMusic::GlyphCanvas
{
// Finite halo support and the projection of its current render target.
bool isHaloOutsideTarget(const QRectF &, const QMatrix4x4 &);
constexpr qint64 NativeBudget = 128LL * 1024 * 1024;
constexpr qint64 InkBudget = 32LL * 1024 * 1024;
constexpr qint64 EffectsBudget = 32LL * 1024 * 1024;
// A lease remains charged until the last reader, including a cancelled worker,
// destroys it. Replacing an item's generation does not release its old work.
class Memory final
{
public:
    Memory() = default;
    ~Memory();
    bool resize(qint64 bytes);
    qint64 size() const
    {
        return bytes_;
    }
    static qint64 total();
    static qint64 highWater();

private:
    qint64 bytes_ = 0;
};
struct Ink
{
    QImage image;
    QByteArray key;
    std::shared_ptr<Memory> memory;
};
struct Outline
{
    QPainterPath path;
    QByteArray key;
    std::shared_ptr<Memory> memory;
};
struct Fragment
{
    std::array<std::shared_ptr<const Ink>, 4> ink;
    std::array<QRectF, 4> rect;
    std::shared_ptr<const Outline> outline;
    QPointF pathOrigin;
    QRectF bounds;
    bool color = false;
};
struct Prepared
{
    QList<Fragment> fragments;
    std::shared_ptr<Memory> memory;
    qint64 bytes = 0;
};
struct Layer
{
    qreal radius = 0, opacity = 1;
    QColor color = Qt::white;
};
struct Draw
{
    int fragment = 0;
    QTransform transform;
    QColor color = Qt::white, tint = Qt::white;
    qreal opacity = 1, blur = 0, glow = 0;
    QList<Layer> halos;
    QRectF clip;
    bool clipped = false;
};
struct Status
{
    std::atomic<quint64> uploads{0};
    std::atomic<int> batches{0};
    std::atomic<qint64> gpuBytes{0}, sharedBytes{0};
    std::atomic<bool> ready{false};
    QString sharedKey;
    std::function<void(const QString &)> changed;
};
QSGNode *updateNode(QSGNode *, QQuickWindow *, const std::shared_ptr<const Prepared> &,
                    const QList<Draw> &, const std::shared_ptr<Status> &, bool screen,
                    qreal maximumRasterScale);
} // namespace EchoMusic::GlyphCanvas
