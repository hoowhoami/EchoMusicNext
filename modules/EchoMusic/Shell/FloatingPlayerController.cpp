#include "SettingsKeys.h"
#include "FloatingPlayerController.h"
#include <QGuiApplication>
#include <QScreen>
#include <QCloseEvent>
#include <QCursor>
#include <QQuickItem>
#include <QScopedValueRollback>
namespace EchoMusic
{
#ifdef Q_OS_MACOS
void configureMacFloating(QQuickWindow *, bool onTop, bool desktop);
void configureMacFloatingPopup(QWindow *, QWindow *);
#endif
namespace
{
QList<QRect> workAreas()
{
    QList<QRect> areas;
    for (auto *screen : QGuiApplication::screens())
        areas << screen->availableGeometry();
    return areas;
}
QRect primaryArea()
{
    auto *screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
}
QRect nearestArea(const QRect &bounds)
{
    auto *screen = QGuiApplication::screenAt(bounds.center());
    return screen ? screen->availableGeometry() : primaryArea();
}
} // namespace
FloatingPlayerController::FloatingPlayerController(QSettings &settings, bool persist,
                                                   QObject *parent)
    : QObject(parent), m_settings(settings), m_persist(persist), m_desktop(settings, true, this),
      m_mini(settings, false, this)
{
#ifdef Q_OS_MACOS
    // Popup.Window is a separate Qt-owned native surface. Its Cocoa level needs
    // resync after Qt's show/activation handling and the host's pin changes.
    qGuiApp->installEventFilter(this);
#endif
    m_desktopEnabled =
        persist &&
        settings.value(EchoMusic::SettingsKeys::Floating::DesktopEnabled, false).toBool();
    m_locked = settings.value(EchoMusic::SettingsKeys::Floating::DesktopLocked, false).toBool();
    for (auto *timer : {&m_desktopSave, &m_miniSave, &m_collapse})
        timer->setSingleShot(true);
    m_desktopSave.setInterval(160);
    m_miniSave.setInterval(250);
    m_collapse.setInterval(280);
    // A click-through window cannot receive HoverHandler events. Query only
    // while a locked, visible desktop actually offers the escape control.
    m_unlockHover.setInterval(150);
    connect(&m_unlockHover, &QTimer::timeout, this, &FloatingPlayerController::updateUnlockHover);
    connect(&m_desktopSave, &QTimer::timeout, this, &FloatingPlayerController::saveDesktop);
    connect(&m_miniSave, &QTimer::timeout, this, &FloatingPlayerController::saveMini);
    connect(&m_collapse, &QTimer::timeout, this, [this] {
        if (m_panel.isEmpty())
            resizeMini(false);
    });
    connect(&m_desktop, &FloatingLyricPreferences::changed, this, [this] {
        applyDesktop();
        emit changed();
    });
    connect(&m_mini, &FloatingLyricPreferences::changed, this,
            &FloatingPlayerController::applyMini);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        if (m_desktopWindow && !wayland()) {
            m_restoring = true;
            m_desktopWindow->setGeometry(
                fitBounds(m_desktopWindow->geometry(), workAreas(), primaryArea(), {720, 200}));
            m_restoring = false;
            positionUnlock();
        }
        if (m_miniWindow && !wayland()) {
            const QScopedValueRollback restoring(m_restoring, true);
            m_anchor = fitBounds(m_anchor, workAreas(), primaryArea(), {360, 64});
            resizeMini(!m_panel.isEmpty());
        }
    });
}
bool FloatingPlayerController::wayland() const
{
    return QGuiApplication::platformName().startsWith("wayland");
}
bool FloatingPlayerController::unlockAvailable() const
{
    return !m_shutdown && m_desktopEnabled && m_locked && m_desktop.showUnlockButton() &&
           !wayland();
}
bool FloatingPlayerController::unlockVisible() const
{
    return unlockAvailable() && m_unlockHovered;
}
void FloatingPlayerController::syncUnlockMonitoring()
{
    if (unlockAvailable() && m_desktopWindow && m_desktopWindow->isVisible() &&
        m_desktopWindow->visibility() != QWindow::Minimized) {
        if (!m_unlockHover.isActive())
            m_unlockHover.start();
    } else {
        m_unlockHover.stop();
    }
    updateUnlockHover();
}
void FloatingPlayerController::updateUnlockHover()
{
    const bool hovered = m_unlockHover.isActive() && m_desktopWindow &&
                         m_desktopWindow->geometry().contains(QCursor::pos());
    if (hovered == m_unlockHovered)
        return;
    m_unlockHovered = hovered;
    if (m_unlockWindow)
        m_unlockWindow->setVisible(unlockVisible());
    emit unlockVisibilityChanged();
}
QRect FloatingPlayerController::fitBounds(QRect saved, const QList<QRect> &areas,
                                          const QRect &fallback, QSize size)
{
    QRect area = fallback;
    bool found = false;
    if (saved.isValid())
        for (const auto &candidate : areas)
            if (saved.intersected(candidate).width() >= 48 &&
                saved.intersected(candidate).height() >= 48) {
                area = candidate;
                found = true;
                break;
            }
    if (!found)
        saved = QRect(area.center() - QPoint(size.width() / 2, size.height() / 2), size);
    saved.setSize({qMin(saved.width(), area.width()), qMin(saved.height(), area.height())});
    saved.moveLeft(qBound(area.left(), saved.x(), area.right() - saved.width() + 1));
    saved.moveTop(qBound(area.top(), saved.y(), area.bottom() - saved.height() + 1));
    return saved;
}
bool FloatingPlayerController::chooseUp(const QRect &anchor, const QRect &area)
{
    const int below = area.bottom() - anchor.bottom(), above = anchor.top() - area.top();
    return below < 296 && (above >= 296 || above > below);
}
void FloatingPlayerController::setDesktopEnabled(bool value)
{
    if (m_shutdown || m_desktopEnabled == value)
        return;
    if (!value)
        saveDesktop();
    m_desktopEnabled = value;
    if (m_persist)
        m_settings.setValue(EchoMusic::SettingsKeys::Floating::DesktopEnabled, value);
    applyDesktop();
    emit changed();
}
void FloatingPlayerController::setLocked(bool value)
{
    if (m_locked == value)
        return;
    m_locked = value;
    if (m_persist)
        m_settings.setValue(EchoMusic::SettingsKeys::Floating::DesktopLocked, value);
    applyDesktop();
    emit changed();
}
void FloatingPlayerController::toggleDesktop()
{
    setDesktopEnabled(!m_desktopEnabled);
}
void FloatingPlayerController::toggleMini()
{
    if (m_shutdown)
        return;
    if (m_miniActive) {
        showMain();
        return;
    }
    m_miniActive = true;
    emit changed();
    emit miniEntered();
}
void FloatingPlayerController::showMini()
{
    if (m_shutdown)
        return;
    if (!m_miniActive) {
        toggleMini();
        return;
    }
    if (m_miniWindow) {
        m_miniWindow->showNormal();
        m_miniWindow->raise();
        m_miniWindow->requestActivate();
    }
}
void FloatingPlayerController::closeMini()
{
    if (!m_miniActive)
        return;
    leaveMini();
    emit miniClosed();
}
void FloatingPlayerController::leaveMini()
{
    saveMini();
    m_collapse.stop();
    m_panel.clear();
    m_miniActive = false;
    emit changed();
}
void FloatingPlayerController::showMain()
{
    leaveMini();
    emit mainRequested();
}
void FloatingPlayerController::selectLyrics()
{
    showMain();
    emit lyricSelectionRequested();
}
void FloatingPlayerController::openSettings()
{
    showMain();
    emit settingsRequested();
}
void FloatingPlayerController::setPanel(const QString &panel)
{
    if (!m_miniActive || (panel != "lyric" && panel != "queue" && !panel.isEmpty()) ||
        panel == m_panel)
        return;
    m_collapse.stop();
    const bool wasEmpty = m_panel.isEmpty();
    if (!panel.isEmpty() && wasEmpty)
        resizeMini(true);
    m_panel = panel;
    emit changed();
    if (panel.isEmpty())
        m_collapse.start();
}
void FloatingPlayerController::attachDesktop(QQuickWindow *window)
{
    if (m_desktopWindow == window)
        return;
    if (m_desktopWindow) {
        saveDesktop();
        disconnect(m_desktopWindow, nullptr, this, nullptr);
        m_desktopWindow->removeEventFilter(this);
    }
    m_desktopWindow = window;
    if (!window) {
        syncUnlockMonitoring();
        emit presentationWindowChanged();
        return;
    }
    window->setTransientParent(nullptr);
    window->setMinimumSize({80, 80});
    window->installEventFilter(this);
    connect(window, &QWindow::visibleChanged, this,
            &FloatingPlayerController::syncUnlockMonitoring);
    connect(window, &QWindow::visibilityChanged, this,
            &FloatingPlayerController::syncUnlockMonitoring);
    connect(window, &QObject::destroyed, this, &FloatingPlayerController::syncUnlockMonitoring);
    m_restoring = true;
    const auto saved =
        m_persist ? m_settings.value(EchoMusic::SettingsKeys::Floating::DesktopGeometry).toRect()
                  : QRect{};
    if (wayland())
        window->resize(saved.isValid() ? saved.size() : QSize(720, 200));
    else {
        auto fallback = primaryArea();
        auto initial = QRect(fallback.center().x() - 360, fallback.bottom() - 240, 720, 200);
        window->setGeometry(
            fitBounds(saved.isValid() ? saved : initial, workAreas(), fallback, {720, 200}));
    }
    m_restoring = false;
    for (auto signal :
         {&QWindow::xChanged, &QWindow::yChanged, &QWindow::widthChanged, &QWindow::heightChanged})
        connect(window, signal, this, [this] {
            if (!m_restoring)
                m_desktopSave.start();
            positionUnlock();
        });
    applyDesktop();
    emit presentationWindowChanged();
}
void FloatingPlayerController::attachMini(QQuickWindow *window)
{
    if (m_miniWindow == window)
        return;
    if (m_miniWindow) {
        m_miniResizeEdge = 0;
        saveMini();
        disconnect(m_miniWindow, nullptr, this, nullptr);
        m_miniWindow->removeEventFilter(this);
    }
    m_miniWindow = window;
    if (!window) {
        emit presentationWindowChanged();
        return;
    }
    window->setTransientParent(nullptr);
    // Expansion changes only the Mini window height, preserving its bounded width.
    window->setMinimumWidth(360);
    window->setMaximumWidth(560);
    window->installEventFilter(this);
    auto saved = m_anchor.isValid() ? m_anchor
                 : m_persist
                     ? m_settings.value(EchoMusic::SettingsKeys::Floating::MiniAnchor).toRect()
                     : QRect{};
    if (saved.isValid())
        saved.setSize({qBound(360, saved.width(), 560), 64});
    m_anchor = fitBounds(saved, workAreas(), primaryArea(), {360, 64});
    {
        const QScopedValueRollback restoring(m_restoring, true);
        resizeMini(false);
    }
    for (auto signal : {&QWindow::xChanged, &QWindow::yChanged, &QWindow::widthChanged})
        connect(window, signal, this, [this] {
            if (!m_restoring)
                m_miniSave.start();
        });
    applyMini();
    if (QGuiApplication::platformName() != "offscreen") {
        window->raise();
        window->requestActivate();
    }
    emit presentationWindowChanged();
}
void FloatingPlayerController::attachUnlock(QQuickWindow *window)
{
    m_unlockWindow = window;
    if (window) {
        window->setTransientParent(nullptr);
        positionUnlock();
    }
}
void FloatingPlayerController::attachUnlockButton(QQuickItem *button)
{
    for (const auto &connection : std::as_const(m_unlockGeometryConnections))
        disconnect(connection);
    m_unlockGeometryConnections.clear();
    m_unlockButton = button;
    // Map the real control, including every layout ancestor, rather than
    // maintaining a second set of horizontal/vertical toolbar coordinates.
    for (auto *item = button; item; item = item->parentItem()) {
        m_unlockGeometryConnections << connect(item, &QQuickItem::visibleChanged, this,
                                               &FloatingPlayerController::positionUnlock);
        for (auto signal : {&QQuickItem::xChanged, &QQuickItem::yChanged, &QQuickItem::widthChanged,
                            &QQuickItem::heightChanged})
            m_unlockGeometryConnections
                << connect(item, signal, this, &FloatingPlayerController::positionUnlock);
    }
    positionUnlock();
}
void FloatingPlayerController::applyDesktop()
{
    if (!m_desktopWindow) {
        syncUnlockMonitoring();
        return;
    }
    auto flags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus;
    if (m_desktop.alwaysOnTop())
        flags |= Qt::WindowStaysOnTopHint;
    if (m_locked)
        flags |= Qt::WindowTransparentForInput;
    m_desktopWindow->setFlags(flags);
    m_desktopWindow->setVisible(m_desktopEnabled);
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa")
        configureMacFloating(m_desktopWindow, m_desktop.alwaysOnTop(), true);
#endif
    positionUnlock();
    syncUnlockMonitoring();
}
void FloatingPlayerController::positionUnlock()
{
    if (!m_unlockWindow || !m_desktopWindow)
        return;
    auto flags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus;
    if (m_desktop.alwaysOnTop())
        flags |= Qt::WindowStaysOnTopHint;
    m_unlockWindow->setFlags(flags);
    const auto bounds =
        m_unlockButton && m_unlockButton->isVisible()
            ? m_unlockButton
                  ->mapRectToScene({0, 0, m_unlockButton->width(), m_unlockButton->height()})
                  .toAlignedRect()
            : QRect(m_desktopWindow->width() - 44, 12, 32, 32);
    m_unlockWindow->setGeometry(
        QRect(m_desktopWindow->position() + bounds.topLeft(), bounds.size()));
    m_unlockWindow->setVisible(unlockVisible());
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa")
        configureMacFloating(m_unlockWindow, m_desktop.alwaysOnTop(), true);
#endif
}
void FloatingPlayerController::applyMini()
{
    if (!m_miniWindow)
        return;
    auto flags = Qt::Tool | Qt::FramelessWindowHint;
    if (m_mini.alwaysOnTop())
        flags |= Qt::WindowStaysOnTopHint;
    m_miniWindow->setFlags(flags);
    m_miniWindow->setVisible(m_miniActive);
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa") {
        configureMacFloating(m_miniWindow, m_mini.alwaysOnTop(), false);
        for (auto *popup : QGuiApplication::topLevelWindows())
            syncMiniPopup(popup);
    }
#endif
}
void FloatingPlayerController::syncMiniPopup(QWindow *popup)
{
#ifdef Q_OS_MACOS
    if (m_shutdown || !m_miniWindow || !popup || popup == m_miniWindow || !popup->isVisible() ||
        QGuiApplication::platformName() != "cocoa")
        return;
    bool belongsToMini = false;
    for (auto *parent = popup->transientParent(); parent; parent = parent->transientParent())
        if (parent == m_miniWindow) {
            belongsToMini = true;
            break;
        }
    if (!belongsToMini)
        return;
    auto *parent = popup->transientParent();
    if (parent != m_miniWindow)
        syncMiniPopup(parent);
    configureMacFloatingPopup(popup, parent);
#else
    Q_UNUSED(popup);
#endif
}
void FloatingPlayerController::resizeMini(bool expanded)
{
    if (!m_miniWindow)
        return;
    // Save a collapsed control-bar anchor, not the expanded panel's top-left.
    if (!m_restoring && m_miniWindow->isVisible())
        saveMini();
    if (expanded)
        m_up = chooseUp(m_anchor, nearestArea(m_anchor));
    auto bounds = m_anchor;
    bounds.setHeight(expanded ? 360 : 64);
    if (expanded && m_up)
        bounds.moveTop(m_anchor.y() - 296);
    const QScopedValueRollback restoring(m_restoring, true);
    if (wayland())
        m_miniWindow->resize(bounds.size());
    else
        m_miniWindow->setGeometry(bounds);
}
void FloatingPlayerController::saveDesktop()
{
    m_desktopSave.stop();
    if (!m_persist || !m_desktopWindow || m_restoring)
        return;
    auto bounds = m_desktopWindow->geometry();
    if (wayland())
        bounds.moveTopLeft(m_settings.value(EchoMusic::SettingsKeys::Floating::DesktopGeometry)
                               .toRect()
                               .topLeft());
    m_settings.setValue(EchoMusic::SettingsKeys::Floating::DesktopGeometry, bounds);
}
void FloatingPlayerController::saveMini()
{
    m_miniSave.stop();
    if (!m_miniWindow || m_restoring)
        return;
    auto bounds = m_miniWindow->geometry();
    if (bounds.height() > 64 && m_up)
        bounds.moveTop(bounds.y() + bounds.height() - 64);
    bounds.setSize({qBound(360, bounds.width(), 560), 64});
    if (wayland())
        bounds.moveTopLeft(m_anchor.topLeft());
    m_anchor = bounds;
    if (m_persist)
        m_settings.setValue(EchoMusic::SettingsKeys::Floating::MiniAnchor, bounds);
}
void FloatingPlayerController::moveDesktop()
{
    if (m_desktopWindow && !m_locked)
        m_desktopWindow->startSystemMove();
}
void FloatingPlayerController::resizeDesktop(int edges)
{
    if (m_desktopWindow && !m_locked)
        m_desktopWindow->startSystemResize(Qt::Edges(edges));
}
void FloatingPlayerController::moveMini()
{
    if (m_miniWindow)
        m_miniWindow->startSystemMove();
}
void FloatingPlayerController::resizeMiniWidth(int edge, int globalX)
{
    m_miniResizeEdge = 0;
    if (!m_miniWindow || (edge != Qt::LeftEdge && edge != Qt::RightEdge))
        return;
    if (m_miniWindow->startSystemResize(Qt::Edge(edge)) || wayland())
        return;
    // Cocoa 6.12 has no startSystemResize implementation. Retain the press
    // geometry so clamping cannot accumulate drift when the left edge moves.
    m_miniResizeEdge = edge;
    m_miniResizeX = globalX;
    m_miniResizeBounds = m_miniWindow->geometry();
}
void FloatingPlayerController::updateMiniWidthResize(int globalX)
{
    if (!m_miniWindow || !m_miniResizeEdge)
        return;
    const int delta = (globalX - m_miniResizeX) * (m_miniResizeEdge == Qt::LeftEdge ? -1 : 1);
    auto bounds = m_miniResizeBounds;
    bounds.setWidth(
        qBound(m_miniWindow->minimumWidth(), bounds.width() + delta, m_miniWindow->maximumWidth()));
    if (m_miniResizeEdge == Qt::LeftEdge)
        bounds.moveRight(m_miniResizeBounds.right());
    m_miniWindow->setGeometry(bounds);
}
void FloatingPlayerController::endMiniWidthResize()
{
    m_miniResizeEdge = 0;
    saveMini();
}
bool FloatingPlayerController::eventFilter(QObject *watched, QEvent *event)
{
#ifdef Q_OS_MACOS
    if (!m_shutdown && m_miniWindow &&
        (event->type() == QEvent::Show || event->type() == QEvent::WindowActivate)) {
        if (auto *window = qobject_cast<QWindow *>(watched)) {
            const QPointer<QWindow> guard(window);
            // Qt updates the platform flags/level during the current operation.
            // One queued lifecycle update runs after it; no polling or frame loop.
            QTimer::singleShot(0, this, [this, guard] {
                if (!guard)
                    return;
                if (guard == m_miniWindow) {
                    for (auto *popup : QGuiApplication::topLevelWindows())
                        syncMiniPopup(popup);
                } else {
                    syncMiniPopup(guard);
                }
            });
        }
    }
#endif
    if (event->type() == QEvent::Close && !m_shutdown) {
        if (watched == m_desktopWindow) {
            static_cast<QCloseEvent *>(event)->ignore();
            setDesktopEnabled(false);
            return true;
        }
        if (watched == m_miniWindow) {
            static_cast<QCloseEvent *>(event)->ignore();
            closeMini();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
QQuickWindow *FloatingPlayerController::presentationWindow() const
{
    if (m_miniActive && m_miniWindow)
        return m_miniWindow;
    return m_desktopEnabled ? m_desktopWindow.data() : nullptr;
}
void FloatingPlayerController::shutdown()
{
    saveDesktop();
    saveMini();
    m_shutdown = true;
    syncUnlockMonitoring();
    m_desktopSave.stop();
    m_miniSave.stop();
    m_collapse.stop();
    for (auto window : {m_desktopWindow, m_miniWindow, m_unlockWindow})
        if (window)
            window->hide();
}
} // namespace EchoMusic
