#pragma once
#include <QPointer>
#include <QObject>
#include <memory>
#include <QString>
class QWindow;
namespace EchoMusic
{
class HyprlandWindowBackdrop;
#ifdef Q_OS_WIN
class WindowsWindowBackdrop;
#endif
/// GUI-thread platform presentation only. Preferences belong to ThemeStore.
/// Owns native effects and checks the attached surface's backend capabilities.
class WindowBackdrop final : public QObject
{
    Q_OBJECT
public:
    explicit WindowBackdrop(bool native);
    ~WindowBackdrop();
    /// Select before native creation; appearance changes keep the same surface.
    static Qt::WindowFlags mainWindowFlags();
    void attach(QWindow *window);
    bool supported() const;
    QString apply(bool enabled, bool dark, int transparency = 0);

signals:
    void completed(const QString &error);

private:
    void reportError(const QString &error);
    std::unique_ptr<HyprlandWindowBackdrop> m_hyprland;
#ifdef Q_OS_WIN
    std::unique_ptr<WindowsWindowBackdrop> m_windows;
#endif
    QString m_error;
    bool m_native;
    QPointer<QWindow> m_window;
    void *m_effect = nullptr;
};
} // namespace EchoMusic
