#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <qt_windows.h>
class QWindow;
namespace EchoMusic
{
/// Qt owns window state, normal geometry and its alpha swapchain. This adapter
/// scopes D3D surface policy, DWM material and fullscreen input to that HWND.
/// The custom expanded caption keeps client input in Qt Quick; only normal
/// resize borders use native non-client hit testing.
class WindowsWindowBackdrop final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    WindowsWindowBackdrop();
    ~WindowsWindowBackdrop() override;
    static Qt::WindowFlags windowFlags(int build);
    void attach(QWindow *window);
    bool supported() const;
    QString apply(bool enabled, bool dark, int transparency = 0);
    QVariantMap diagnostics() const;
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

signals:
    void completed(const QString &error);

private:
    enum class Applied
    {
        None,
        LegacyAccent
    };
    void clear();
    bool accent(int state);
    bool applyMaterialState(bool enabled, bool dark, int transparency);
    void releaseSurface();
    bool fullscreen() const;
    void logState(const char *reason);
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR id, DWORD_PTR data);
    void applyWindowPolicy();
    void applyFullscreenGeometry();
    void applyFullscreenGeometryNow();
    void queueRepair();
    bool hitTest(const MSG &event, qintptr *result) const;
    QMetaObject::Connection m_visibilityConnection;
    QMetaObject::Connection m_stateConnection;
    QMetaObject::Connection m_screenConnection;
    QPointer<QWindow> m_window;
    HWND m_handle = nullptr;
    QString m_error;
    QVariantMap m_lastDiagnostics;
    int m_build = 0;
    int m_transparency = 0;
    // Last appearance request, kept across failed applies so the queued repair
    // can retry the state once the native surface report is available.
    int m_requestedEnabled = -1, m_requestedDark = -1, m_requestedTransparency = -1;
    bool m_enabled = false, m_dark = false, m_repairQueued = false;
    bool m_geometryQueued = false;
    bool m_restoreFullscreen = false;
    bool m_framelessForFullscreen = false;
    bool m_initialized = false, m_subclassInstalled = false;
    Applied m_applied = Applied::None;
};
} // namespace EchoMusic
