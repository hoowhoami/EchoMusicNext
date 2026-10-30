#include "GpuRhi_p.h"
#include "../EchoGpuGlyphAtlas.h"
namespace EchoMusic
{
namespace
{
void publish(QObject *relay, const std::shared_ptr<GpuGlyphAtlasState> &state, quint64 generation,
             QVariantList pages, QVariantList fragments, quint64 uploads, QString error)
{
    QMetaObject::invokeMethod(
        relay,
        [state, generation, pages = std::move(pages), fragments = std::move(fragments), uploads,
         error = std::move(error)] {
            if (state->owner)
                state->owner->publish(state, generation, pages, fragments, uploads, error);
        },
        Qt::QueuedConnection);
}
} // namespace
GpuGlyphAtlasAllocation::~GpuGlyphAtlasAllocation()
{
    if (state->generation != generation)
        return;
    state->ready = false;
    QMetaObject::invokeMethod(
        relay.get(),
        [s = state, g = generation] {
            if (s->owner)
                s->owner->nativeLost(s, g);
        },
        Qt::QueuedConnection);
}
bool GpuRenderState::updateGlyphAtlases(QRhiResourceUpdateBatch *updates)
{
    std::erase_if(glyphAtlases, [this](const auto &entry) {
        return !entry.second->state->active || !snapshot.glyphAtlases.contains(entry.second->state);
    });
    bool ready = true;
    for (const auto &state : snapshot.glyphAtlases) {
        if (!state->active || !state->preparation || !*state->preparation->valid) {
            ready = false;
            continue;
        }
        auto &native = glyphAtlases[state.get()];
        if (!native) {
            auto next = std::make_shared<GpuGlyphAtlasAllocation>();
            next->state = state;
            next->relay = bridge;
            next->generation = ++state->generation;
            QString message;
            next->atlas = state->preparation->acquire(window, updates, message);
            QVariantList pages;
            for (auto *texture : next->atlas.textures) {
                if (!texture || texture->rhi() != rhi) {
                    message = "Glyph atlas belongs to another RHI device";
                    break;
                }
                pages.append(QVariantMap{
                    {"dimension", "2d"},
                    {"width", texture->pixelSize().width()},
                    {"height", texture->pixelSize().height()},
                    {"format", texture->format() == QRhiTexture::RGBA16F ? "rgba16f" : "rgba8"},
                    {"usage", QStringList{"sampled"}}});
            }
            if (!message.isEmpty()) {
                next->error = message;
                native = std::move(
                    next); // Keep partially uploaded pages alive until this frame retires.
                publish(bridge.get(), state, native->generation, {}, {}, 0, message);
                error = message;
                return false;
            }
            native = std::move(next);
            state->native = native;
            state->ready = true;
            publish(bridge.get(), state, native->generation, pages, native->atlas.fragments,
                    native->atlas.uploads, {});
        }
        if (!native->error.isEmpty()) {
            error = native->error;
            return false;
        }
        ready &= state->acknowledged == native->generation;
    }
    return ready;
}
} // namespace EchoMusic
