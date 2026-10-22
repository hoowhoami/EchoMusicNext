#pragma once
#include "FloatingLyricPreferences.h"
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSettings>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Native secondary-window lifetime/placement. No audio engine or writable playback snapshot.
class FloatingPlayerController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by WindowController")
    Q_PROPERTY(EchoMusic::FloatingLyricPreferences *desktop READ desktop CONSTANT)
    Q_PROPERTY(EchoMusic::FloatingLyricPreferences *mini READ mini CONSTANT)
    Q_PROPERTY(bool desktopEnabled READ desktopEnabled WRITE setDesktopEnabled NOTIFY changed)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY changed)
    Q_PROPERTY(bool miniActive READ miniActive NOTIFY changed)
    Q_PROPERTY(QString panel READ panel NOTIFY changed)
    Q_PROPERTY(bool expandsUp READ expandsUp NOTIFY changed)
    Q_PROPERTY(qreal miniScale READ miniScale WRITE setMiniScale NOTIFY miniScaleChanged)
    Q_PROPERTY(bool wayland READ wayland CONSTANT)
    Q_PROPERTY(bool unlockAvailable READ unlockAvailable NOTIFY changed)
    Q_PROPERTY(bool unlockVisible READ unlockVisible NOTIFY unlockVisibilityChanged)
public:
    FloatingPlayerController(QSettings &, bool persist, QObject *parent = nullptr);
    FloatingLyricPreferences *desktop()
    {
        return &m_desktop;
    }
    FloatingLyricPreferences *mini()
    {
        return &m_mini;
    }
    bool desktopEnabled() const
    {
        return m_desktopEnabled;
    }
    bool locked() const
    {
        return m_locked;
    }
    bool miniActive() const
    {
        return m_miniActive;
    }
    QString panel() const
    {
        return m_panel;
    }
    bool expandsUp() const
    {
        return m_up;
    }
    qreal miniScale() const
    {
        return m_miniScale;
    }
    void setMiniScale(qreal scale);
    QQuickWindow *miniWindow() const
    {
        return m_miniWindow;
    }
    bool wayland() const;
    bool unlockAvailable() const;
    bool unlockVisible() const;
    void setDesktopEnabled(bool);
    void setLocked(bool);
    Q_INVOKABLE void toggleDesktop();
    Q_INVOKABLE void toggleMini();
    void showMini();
    Q_INVOKABLE void closeMini();
    void leaveMini();
    Q_INVOKABLE void showMain();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void selectLyrics();
    Q_INVOKABLE void setPanel(const QString &);
    Q_INVOKABLE void attachDesktop(QQuickWindow *);
    Q_INVOKABLE void attachMini(QQuickWindow *);
    Q_INVOKABLE void attachUnlock(QQuickWindow *);
    Q_INVOKABLE void attachUnlockButton(QQuickItem *);
    Q_INVOKABLE void moveDesktop();
    Q_INVOKABLE void resizeDesktop(int edges);
    Q_INVOKABLE void moveMini();
    Q_INVOKABLE void resizeMiniWidth(int edge, int globalX);
    Q_INVOKABLE void updateMiniWidthResize(int globalX);
    Q_INVOKABLE void endMiniWidthResize();
    QQuickWindow *presentationWindow() const;
    QVariantMap miniBounds() const;
    QVariantMap desktopBounds() const;
    bool setPluginBounds(const QString &target, const QVariantMap &bounds);
    void shutdown();
    static QRect fitBounds(QRect saved, const QList<QRect> &areas, const QRect &fallback,
                           QSize size);
    static bool chooseUp(const QRect &anchor, const QRect &area, int expansionHeight = 296);
signals:
    void miniScaleChanged();
    void presentationWindowChanged();
    void changed();
    void unlockVisibilityChanged();
    void miniEntered();
    void miniClosed();
    void mainRequested();
    void settingsRequested();
    void lyricSelectionRequested();

private:
    bool eventFilter(QObject *, QEvent *) override;
    void applyDesktop();
    void applyMini();
    void syncMiniPopup(QWindow *);
    void positionUnlock();
    void syncUnlockMonitoring();
    void updateUnlockHover();
    void saveDesktop();
    void saveMini();
    void resizeMini(bool expanded);
    QSettings &m_settings;
    bool m_persist, m_desktopEnabled = false, m_locked = false, m_miniActive = false, m_up = false,
                    m_restoring = false, m_shutdown = false, m_unlockHovered = false;
    FloatingLyricPreferences m_desktop, m_mini;
    QPointer<QQuickWindow> m_desktopWindow, m_miniWindow, m_unlockWindow;
    QPointer<QQuickItem> m_unlockButton;
    QList<QMetaObject::Connection> m_unlockGeometryConnections;
    QRect m_anchor;
    qreal m_requestedMiniScale = 1, m_miniScale = 1;
    QRect m_miniResizeBounds;
    int m_miniResizeEdge = 0, m_miniResizeX = 0;
    QString m_panel;
    QTimer m_desktopSave, m_miniSave, m_collapse, m_unlockHover;
};
} // namespace EchoMusic
