#include "MainWindowHost.h"
#include "WindowController.h"
#include "ShortcutService.h"
#include "RouteController.h"
#include <QQmlEngine>
#include <QQuickWindow>
#include <utility>
namespace EchoMusic
{
MainWindowHost::MainWindowHost(QQmlEngine &engine, WindowController &controller,
                               ShortcutService &shortcuts, Factory factory, RouteController *routes,
                               QObject *parent)
    : QObject(parent), m_engine(engine), m_controller(controller), m_shortcuts(shortcuts),
      m_routes(routes), m_factory(std::move(factory))
{
    if (m_routes)
        m_routes->setPresentationActive(false);
    m_reconcile.setSingleShot(true);
    m_reconcile.setInterval(0);
    connect(&m_reconcile, &QTimer::timeout, this, &MainWindowHost::reconcile);
    connect(&controller, &WindowController::mainWindowRequested, this, [this] {
        if (m_state == State::Closed)
            return;
        m_wantsWindow = true;
        m_reconcile.stop();
        // Existing windows are presented by WindowController. Record the newer
        // intent so a queued retirement cannot discard that foreground window.
        if (m_state == State::Absent)
            show();
    });
    connect(&controller, &WindowController::backgroundRequested, this, &MainWindowHost::unload);
}
MainWindowHost::~MainWindowHost()
{
    shutdown();
    delete m_window.data();
}
void MainWindowHost::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}
void MainWindowHost::show()
{
    if (m_state == State::Closed)
        return;
    m_wantsWindow = true;
    m_reconcile.stop();
    m_controller.floating()->leaveMini();
    reconcile();
}
void MainWindowHost::unload()
{
    if (m_state == State::Closed)
        return;
    m_wantsWindow = false;
    // A close/native menu/QML handler may still be using the visual tree.
    // Retire outside that stack and coalesce a newer foreground intent.
    m_reconcile.start();
}
void MainWindowHost::reconcile()
{
    if (m_state == State::Closed || m_state == State::Creating || m_state == State::Retiring)
        return;
    if (m_wantsWindow) {
        if (!m_window) {
            setState(State::Creating);
            if (m_state == State::Closed)
                return;
            if (m_routes)
                m_routes->setPresentationActive(true);
            auto *created = m_factory();
            if (m_state == State::Closed) {
                delete created;
                return;
            }
            if (!created) {
                setState(State::Absent);
                // Retry only on a new user action, never a background loop.
                m_wantsWindow = false;
                if (m_routes)
                    m_routes->setPresentationActive(false);
                emit creationFailed();
                return;
            }
            m_window = created;
            connect(created, &QObject::destroyed, this, [this] {
                m_window = nullptr;
                if (m_state == State::Closed)
                    return;
                setState(State::Absent);
                m_reconcile.start();
            });
            m_controller.attach(created);
            m_shortcuts.attach(created);
            setState(State::Present);
        }
        if (m_wantsWindow)
            m_controller.showWindow();
        else
            m_reconcile.start();
    } else if (m_window) {
        setState(State::Retiring);
        if (m_routes)
            m_routes->setPresentationActive(false);
        m_shortcuts.attach(nullptr);
        // Clears a window-owned incubation controller before it is destroyed;
        // WindowController then selects a surviving Mini/plugin window.
        m_engine.setIncubationController(nullptr);
        m_controller.detach();
        m_window->hide();
        m_window->deleteLater();
    }
}
void MainWindowHost::shutdown()
{
    if (m_state == State::Closed)
        return;
    m_wantsWindow = false;
    m_reconcile.stop();
    setState(State::Closed);
    if (m_routes)
        m_routes->setPresentationActive(false);
    m_shortcuts.attach(nullptr);
    m_engine.setIncubationController(nullptr);
    m_controller.detach();
    if (m_window)
        m_window->hide();
}
} // namespace EchoMusic
