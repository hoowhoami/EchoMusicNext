#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QString>
#include <qt_windows.h>
class QWindow;
namespace EchoMusic
{
/// Qt owns HWND styles and its alpha swapchain. This adapter requests DWM material
/// and native corner policy. The custom expanded caption keeps client input in
/// Qt Quick; only resize borders use native non-client hit testing.
class WindowsWindowBackdrop final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    WindowsWindowBackdrop();
    ~WindowsWindowBackdrop() override;
    static Qt::WindowFlags windowFlags(int build);
    void attach(QWindow *window);
    bool supported() const;
    QString apply(bool enabled, bool dark);
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

signals:
    void completed(const QString &error);

private:
    void clear();
    bool accent(int state);
    bool material(bool enabled);
    void applyWindowPolicy();
    void queueRepair();
    bool hitTest(const MSG &event, qintptr *result) const;
    QMetaObject::Connection m_visibilityConnection;
    QPointer<QWindow> m_window;
    HWND m_handle = nullptr;
    QString m_error;
    int m_build = 0;
    bool m_enabled = false, m_dark = false, m_suspended = false, m_repairQueued = false;
};
} // namespace EchoMusic
