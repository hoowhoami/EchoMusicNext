#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QtQml/qqmlregistration.h>
#include <optional>
#include "TaskbarProgressState.h"
#include "WindowsTaskbarProgress.h"
#include "WindowsTaskbarThumbar.h"
#include "WindowsTaskbarThumbnail.h"
class QQuickWindow;
namespace EchoMusic
{
class PlaybackLibraryActions;
class PlaybackService;
/// Taskbar button extras: playback progress, the hover-preview album cover and
/// the hover-preview transport buttons.
///
/// All three decorate the same taskbar button, follow the same playback source
/// and share one availability check, so they live in one controller rather than
/// three (EchoMusic taskbarProgress.ts + taskbarThumbnail.ts + thumbar.ts).
/// Preferences are persisted next to the other window behaviour keys.
class TaskbarButtonController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the window controller")
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool progressEnabled READ progressEnabled WRITE setProgressEnabled NOTIFY changed)
    Q_PROPERTY(bool coverPreviewEnabled READ coverPreviewEnabled WRITE setCoverPreviewEnabled NOTIFY
                   changed)
public:
    explicit TaskbarButtonController(QSettings &settings, QObject *parent = nullptr);
    bool available() const
    {
        return m_available;
    }
    bool progressEnabled() const
    {
        return m_progressEnabled;
    }
    void setProgressEnabled(bool enabled);
    bool coverPreviewEnabled() const
    {
        return m_coverEnabled;
    }
    void setCoverPreviewEnabled(bool enabled);
    /// The window whose taskbar button this controller decorates.
    void attach(QQuickWindow *window);
    /// Follows the queue: bar, cover and transport buttons all read from it.
    void attachPlayback(PlaybackService &playback);
    /// Icon colour follows the resolved appearance; the taskbar toolbar has no
    /// way to invert the icons itself.
    void setDark(bool dark);

signals:
    void changed();

private:
    void refreshAll();
    void refreshProgress();
    void refreshCover();
    void refreshThumbar();
    QImage icon(const QString &glyph, const QColor &tint = QColor(), int size = 0);
    /// The application's own no-artwork placeholder, mirrored from EchoCover:
    /// a muted surface with a low-opacity music glyph.
    QImage placeholderCover();
    quintptr handle() const;
    QSettings &m_settings;
    QPointer<QQuickWindow> m_window;
    QPointer<PlaybackService> m_playback;
    /// The library action hub is created lazily when the library attaches, so
    /// it may still be absent when playback is attached. Kept as a QPointer to
    /// QObject: only its identity is needed here, and it must never dangle if
    /// playback is torn down first.
    QPointer<QObject> m_actions;
    WindowsTaskbarProgress m_progress;
    WindowsTaskbarThumbar m_thumbar;
    WindowsTaskbarThumbnail m_thumbnail;
    TaskbarProgressGate m_gate;
    QElapsedTimer m_clock;
    /// Icons are vector-rendered once per size; a rebuild only re-tints.
    QHash<QString, QImage> m_icons;
    bool m_available = false;
    bool m_progressEnabled = false;
    bool m_coverEnabled = false;
    bool m_dark = false;
    bool m_progressCleared = true;
    /// Track the preview was built from; identical tracks must not rebuild the
    /// bitmap on every playback notification.
    QString m_coverKey;
    bool m_previewApplied = false;
    bool m_thumbarHasTrack = false, m_thumbarPlaying = false, m_thumbarFavorite = false,
         m_thumbarBuilt = false;
    /// Mirrors a favourite click until the asynchronous library write settles.
    /// Without it the button only flips after the API round trip, which reads
    /// as an unresponsive button next to the instant transport buttons.
    std::optional<bool> m_favoriteIntent;
    QString m_favoriteIntentTrack;
    qint64 m_favoriteIntentMs = 0;
};
} // namespace EchoMusic
