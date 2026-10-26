#pragma once
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
namespace EchoMusic
{
/// One thumbnail toolbar button. The image is pre-rendered by the shell module
/// so the platform layer stays free of icon/theme dependencies.
struct TaskbarThumbarButton
{
    QImage image;
    QString tooltip;
    bool enabled = true;
    /// Routed back verbatim through buttonClicked().
    int id = 0;
};
/// Windows thumbnail toolbar (ITaskbarList3 thumbbar): the transport buttons
/// beside the taskbar hover preview. Off Windows every call is a no-op.
///
/// Clicks arrive as WM_COMMAND on the host window, so the window is subclassed
/// to translate the button ids into a queued signal; running playback work
/// inside the message dispatch would re-enter the shell.
class WindowsTaskbarThumbar final : public QObject
{
    Q_OBJECT
public:
    explicit WindowsTaskbarThumbar(QObject *parent = nullptr);
    ~WindowsTaskbarThumbar() override;
    bool available();
    /// Replaces the button set of the given window. An empty list hides every
    /// button and detaches the host.
    void apply(quintptr hwnd, const QList<TaskbarThumbarButton> &buttons);
    void detach();
    /// Called by the window subclass when a button is pressed.
    void notifyClicked(int id);
    /// Explorer's "taskbar button was created" message. The toolbar belongs to
    /// that button, so hiding to the tray destroys it and it must be registered
    /// again once the button is back.
    unsigned taskbarCreatedMessage() const
    {
        return m_taskbarCreatedMessage;
    }
    /// Re-registers the current button set for a freshly created button.
    void reregister();

signals:
    void buttonClicked(int id);

private:
    void releaseIcons();
    /// Pushes the last button set to the current window.
    void push();
    /// ITaskbarList3*, owned here.
    void *m_taskbar = nullptr;
    bool m_attempted = false;
    quintptr m_hwnd = 0;
    int m_buttonCount = 0;
    bool m_subclassed = false;
    unsigned m_taskbarCreatedMessage = 0;
    /// Last set, kept so the toolbar can be rebuilt when the button returns
    /// without the shell module having to re-render anything.
    QList<TaskbarThumbarButton> m_buttons;
    /// HICONs handed to Explorer; it reads them after the call returns, so they
    /// outlive the call and are released with the button set.
    QList<quintptr> m_icons;
};
} // namespace EchoMusic
