#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <cstdint>

struct wl_compositor;
struct wl_display;
struct wl_registry;
struct wl_surface;
struct ext_background_effect_manager_v1;
struct ext_background_effect_surface_v1;

namespace EchoMusic
{
class WaylandBackgroundEffect final : public QObject
{
    Q_OBJECT
public:
    explicit WaylandBackgroundEffect(QObject *parent = nullptr);
    ~WaylandBackgroundEffect() override;

    bool available() const;
    bool attach(QWindow *window);
    void setEnabled(bool enabled);
    void bindGlobal(wl_registry *registry, uint32_t name, const char *interface,
                    uint32_t version);
    void setCapabilities(uint32_t flags);

signals:
    void availableChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class WaylandBackgroundEffectTest;
    bool bindProtocol();
    void attachSurface();
    void detachSurface();
    void updateRegion();

    QPointer<QWindow> m_window;
    wl_display *m_display = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_registry *m_registry = nullptr;
    wl_surface *m_surface = nullptr;
    ext_background_effect_manager_v1 *m_manager = nullptr;
    ext_background_effect_surface_v1 *m_effect = nullptr;
    bool m_available = false;
    bool m_enabled = false;
};
} // namespace EchoMusic
