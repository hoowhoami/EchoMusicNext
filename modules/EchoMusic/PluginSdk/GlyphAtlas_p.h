#pragma once
// Host-only bridge. QML receives resources and scalar metadata, never these handles.
#include <QVariantList>
#include <QQuickWindow>
#include <QPointer>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
class QRhiTexture;
class QRhiResourceUpdateBatch;
namespace EchoMusic
{
class EchoGpuGlyphAtlas;
class EchoGpuSurface;
struct NativeGlyphAtlas
{
    std::vector<QRhiTexture *> textures;
    std::shared_ptr<void> retained;
    QVariantList fragments;
    quint64 uploads = 0;
};
struct GlyphAtlasPreparation
{
    std::shared_ptr<std::atomic<bool>> valid = std::make_shared<std::atomic<bool>>(true);
    quint64 revision = 0;
    qreal pixelRatio = 1;
    QVariantList metrics;
    std::function<NativeGlyphAtlas(QQuickWindow *, QRhiResourceUpdateBatch *, QString &)> acquire;
};
struct GpuGlyphAtlasAllocation;
struct GpuGlyphAtlasState
{
    QPointer<EchoGpuGlyphAtlas> owner;
    QPointer<EchoGpuSurface> surface;
    std::shared_ptr<const GlyphAtlasPreparation> preparation;
    // Accessed only by this window's render thread. GUI never retains QRhi allocations.
    std::weak_ptr<GpuGlyphAtlasAllocation> native;
    std::atomic<bool> active{true}, ready{false};
    std::atomic<quint64> generation{0}, acknowledged{0};
};
struct GpuGlyphAtlasAllocation
{
    std::shared_ptr<QObject> relay;
    QString error;
    NativeGlyphAtlas atlas;
    std::shared_ptr<GpuGlyphAtlasState> state;
    quint64 generation = 0;
    ~GpuGlyphAtlasAllocation();
};
} // namespace EchoMusic
