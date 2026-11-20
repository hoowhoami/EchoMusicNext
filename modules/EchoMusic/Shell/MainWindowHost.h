#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <functional>
class QQmlEngine;
namespace EchoMusic
{
class WindowController;
class ShortcutService;
class RouteController;
/// Owns only the replaceable main surface. Services and plugin activation
/// remain owned by the application, including while no Quick window exists.
class MainWindowHost final : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Absent,
        Creating,
        Present,
        Retiring,
        Closed
    };
    Q_ENUM(State)
    using Factory = std::function<QQuickWindow *()>;
    MainWindowHost(QQmlEngine &, WindowController &, ShortcutService &, Factory,
                   RouteController *routes = nullptr, QObject *parent = nullptr);
    ~MainWindowHost() override;
    State state() const
    {
        return m_state;
    }
    QQuickWindow *window() const
    {
        return m_window;
    }
    void show();
    void unload();
    void shutdown();
signals:
    void stateChanged();
    void creationFailed();

private:
    void reconcile();
    void setState(State);
    QQmlEngine &m_engine;
    WindowController &m_controller;
    ShortcutService &m_shortcuts;
    RouteController *m_routes;
    Factory m_factory;
    QPointer<QQuickWindow> m_window;
    QTimer m_reconcile;
    State m_state = State::Absent;
    bool m_wantsWindow = false;
};
} // namespace EchoMusic
