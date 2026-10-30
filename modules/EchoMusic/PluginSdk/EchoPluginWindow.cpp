#include "EchoPluginWindow.h"
#include "EchoPluginContext.h"
#include "AccountService.h"
#include "ApiClient.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QEvent>
#include <QTimer>
#include <QScopeGuard>
#include <cmath>
namespace EchoMusic
{
#ifdef Q_OS_MACOS
void configureMacPluginWindow(QQuickWindow *, bool firstMouse, bool onTop);
#endif
namespace
{
bool positionSupported()
{
    return !QGuiApplication::platformName().startsWith("wayland");
}
bool nativeOpacity()
{
    return QStringList{"cocoa", "windows", "xcb"}.contains(QGuiApplication::platformName());
}
bool number(const QVariant &v)
{
    bool ok;
    double n = v.toDouble(&ok);
    return ok && std::isfinite(n) && std::floor(n) == n && qAbs(n) <= 100000 &&
           v.metaType().id() != QMetaType::Bool;
}
} // namespace
EchoPluginWindow::EchoPluginWindow(EchoPluginContext &s, QVariantMap d)
    : QObject(&s), m_scope(s), m_definition(std::move(d)), m_initialDefinition(m_definition)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&s, &EchoPluginContext::sessionInvalidated, this, [this] {
        release();
        m_definition = m_initialDefinition;
        m_error.clear();
        emit changed();
    });
    connect(&s, &EchoPluginContext::activeChanged, this, [this] {
        if (!m_scope.active())
            release();
    });
    connect(&s.host().account, &AccountService::sessionAdopted, this, &EchoPluginWindow::release);
    connect(&s.host().api, &ApiClient::sessionChanged, this, &EchoPluginWindow::release);
    if (s.host().permissions)
        connect(s.host().permissions, &PluginPermissionStore::revoked, this,
                [this](const QString &id, const QString &cap) {
                    if (id == m_scope.installationId() && cap == "windows")
                        release();
                });
}
EchoPluginWindow::~EchoPluginWindow()
{
    release();
}
QVariantMap EchoPluginWindow::snapshot() const
{
    const auto b = m_window ? m_window->geometry() : m_bounds;
    return {
        {"id", id()},
        {"visible", bool(m_window && m_window->isVisible())},
        {"loading", bool(m_opening)},
        {"active", bool(m_window && m_window->isActive())},
        {"minimized", bool(m_window && m_window->windowState() == Qt::WindowMinimized)},
        {"opacity",
         m_window ? (nativeOpacity() ? m_window->opacity() : m_window->contentItem()->opacity())
                  : m_definition.value("opacity", 1).toDouble()},
        {"opacitySupported", nativeOpacity() || m_definition.value("transparent", false).toBool()},
        {"opacityMode", nativeOpacity()                                     ? "native"
                        : m_definition.value("transparent", false).toBool() ? "content"
                                                                            : "unsupported"},
        {"title", m_window ? m_window->title()
                           : m_definition.value("title", m_scope.package().name).toString()},
        {"bounds",
         QVariantMap{{"x", b.x()}, {"y", b.y()}, {"width", b.width()}, {"height", b.height()}}},
        {"ignoreMouseEvents", m_ignore},
        {"alwaysOnTop", m_window ? bool(m_window->flags() & Qt::WindowStaysOnTopHint)
                                 : m_definition.value("alwaysOnTop", false).toBool()},
        {"positionSupported", positionSupported()},
        {"error", m_error}};
}
QRect EchoPluginWindow::bounded(QRect r) const
{
    r.setSize({qBound(m_definition.value("minWidth", 64).toInt(), r.width(),
                      m_definition.value("maxWidth", 8192).toInt()),
               qBound(m_definition.value("minHeight", 64).toInt(), r.height(),
                      m_definition.value("maxHeight", 8192).toInt())});
    if (!m_definition.value("allowOutsideWorkArea", false).toBool() && positionSupported()) {
        auto *screen = QGuiApplication::screenAt(r.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const auto a = screen->availableGeometry();
            r.moveLeft(qBound(a.left(), r.left(), qMax(a.left(), a.right() - r.width() + 1)));
            r.moveTop(qBound(a.top(), r.top(), qMax(a.top(), a.bottom() - r.height() + 1)));
        }
    }
    return r;
}
EchoPluginOperation *EchoPluginWindow::show(const QVariantMap &options)
{
    if (m_opening)
        return m_scope.rejected("Busy", "Window is opening");
    for (auto i = options.begin(); i != options.end(); ++i)
        if (!QStringList{"x", "y", "width", "height", "focus", "alwaysOnTop",
                         "allowOutsideWorkArea"}
                 .contains(i.key()) ||
            (QStringList{"focus", "alwaysOnTop", "allowOutsideWorkArea"}.contains(i.key())
                 ? i.value().metaType().id() != QMetaType::Bool
                 : !number(i.value())))
            return m_scope.rejected("InvalidArgument", "Invalid window show options");
    if (!positionSupported() && (options.contains("x") || options.contains("y")))
        return m_scope.rejected("Unsupported",
                                "Global window positioning is unavailable on Wayland");
    auto *op = m_scope.permissions()->authorize(
        "windows", tr("打开窗口“%1”").arg(m_definition.value("title", id()).toString()),
        [this, options](auto *operation) { create(operation, options); });
    if (!op->finished())
        m_opening = op;
    connect(op, &EchoPluginOperation::completed, this, [this, op] {
        if (m_opening == op) {
            m_opening = nullptr;
            if (op->state() == EchoPluginOperation::Canceled)
                release();
            emit changed();
        }
    });
    return op;
}
void EchoPluginWindow::create(EchoPluginOperation *op, QVariantMap options)
{
    for (const auto &key : {"alwaysOnTop", "allowOutsideWorkArea"})
        if (options.contains(key))
            m_definition[key] = options.value(key);
    if (m_window) {
        m_creating = true;
        const auto creationGuard = qScopeGuard([this] {
            m_creating = false;
            if (std::exchange(m_releasePending, false))
                release();
        });
        const bool onTop = m_definition.value("alwaysOnTop", false).toBool();
        if (bool(m_window->flags() & Qt::WindowStaysOnTopHint) != onTop) {
            m_reconfiguring = true;
            m_window->setFlag(Qt::WindowStaysOnTopHint, onTop);
            m_window->show();
#ifdef Q_OS_MACOS
            configureMacPluginWindow(m_window,
                                     m_definition.value("acceptFirstMouse", true).toBool(), onTop);
#endif
            m_reconfiguring = false;
        }
        if (m_window->windowState() == Qt::WindowMinimized)
            m_window->showNormal();
        if (!options.isEmpty()) {
            auto b = m_window->geometry();
            for (const auto &key : {"x", "y", "width", "height"})
                if (options.contains(key)) {
                    if (key == QString("x"))
                        b.moveLeft(options[key].toInt());
                    else if (key == QString("y"))
                        b.moveTop(options[key].toInt());
                    else if (key == QString("width"))
                        b.setWidth(options[key].toInt());
                    else
                        b.setHeight(options[key].toInt());
                }
            m_window->setGeometry(bounded(b));
        }
        m_window->raise();
        if (options.value("focus", true).toBool())
            m_window->requestActivate();
        op->succeed(QVariant::fromValue(this));
        return;
    }
    const auto file = m_scope.package().resolve(m_definition.value("component").toString());
    if (file.isEmpty()) {
        op->fail("InvalidComponent", "Window component is missing");
        return;
    }
    m_opening = op;
    const auto revision = ++m_revision;
    m_error.clear();

    m_component = new QQmlComponent(&m_scope.engine(), QUrl::fromLocalFile(file),
                                    QQmlComponent::Asynchronous, this);
    auto ready = [this, revision, options] {
        if (revision != m_revision || !m_component || m_component->isLoading() || !m_opening ||
            m_opening->finished())
            return;
        auto op = m_opening;
        if (m_component->isError()) {
            m_error = m_component->errorString();
            m_opening = nullptr;
            op->fail("ComponentError", m_error);
            release();
            return;
        }
        m_owner = new QObject(this);
        m_presentation = m_scope.createPresentation(m_owner, "window." + id());
        if (!m_presentation) {
            op->fail("Unavailable", "Presentation scope limit exceeded");
            release();
            return;
        }
        auto *w = new QQuickWindow;
        m_window = w;
        w->setObjectName("pluginWindow-" + m_scope.id() + "-" + id());
        w->installEventFilter(this);
        auto flags = (m_definition.value("skipTaskbar", true).toBool() ? Qt::Tool : Qt::Window) |
                     Qt::FramelessWindowHint;
        if (m_definition.value("alwaysOnTop", false).toBool())
            flags |= Qt::WindowStaysOnTopHint;
        w->setFlags(flags);
        if (nativeOpacity())
            w->setOpacity(m_definition.value("opacity", 1).toDouble());
        else if (m_definition.value("transparent", false).toBool())
            w->contentItem()->setOpacity(m_definition.value("opacity", 1).toDouble());
        w->setTitle(m_definition.value("title", m_scope.package().name).toString());
        w->setColor(m_definition.value("transparent", false).toBool() ? Qt::transparent
                                                                      : QColor("#202020"));
        w->setMinimumSize({m_definition.value("minWidth", 64).toInt(),
                           m_definition.value("minHeight", 64).toInt()});
        w->setMaximumSize({m_definition.value("maxWidth", 8192).toInt(),
                           m_definition.value("maxHeight", 8192).toInt()});
        QRect b(0, 0, m_definition.value("defaultWidth", 480).toInt(),
                m_definition.value("defaultHeight", 320).toInt());
        if (auto *screen = QGuiApplication::primaryScreen()) {
            b.moveCenter(screen->availableGeometry().center());
            if (m_definition.value("position") == "top-center")
                b.moveTop(screen->availableGeometry().top() + 24);
        }
        if (m_definition.value("rememberBounds", true).toBool()) {
            const auto saved =
                m_scope.host().permissions->windowBounds(m_scope.installationId(), id());
            if (saved.isValid())
                b = saved;
        }
        if (m_definition.value("rememberBounds", true).toBool() && m_bounds.isValid())
            b = m_bounds;
        if (options.contains("x"))
            b.moveLeft(options["x"].toInt());
        if (options.contains("y"))
            b.moveTop(options["y"].toInt());
        if (options.contains("width"))
            b.setWidth(options["width"].toInt());
        if (options.contains("height"))
            b.setHeight(options["height"].toInt());
        w->setGeometry(bounded(b));
        m_creating = true;
        const auto creationGuard = qScopeGuard([this] {
            m_creating = false;
            if (std::exchange(m_releasePending, false))
                release();
        });
        auto *object = m_component->beginCreate(m_scope.engine().rootContext());
        auto *item = qobject_cast<QQuickItem *>(object);
        if (item) {
            QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
            item->setParent(w->contentItem());
            item->setParentItem(w->contentItem());
            item->setSize(w->size());
            m_component->setInitialProperties(
                item, {{"plugin", QVariant::fromValue(m_presentation.data())},
                       {"window", QVariant::fromValue(this)}});
            connect(w, &QQuickWindow::widthChanged, item,
                    [item, w] { item->setWidth(w->width()); });
            connect(w, &QQuickWindow::heightChanged, item,
                    [item, w] { item->setHeight(w->height()); });
        }
        m_component->completeCreate();
        if (!item || m_component->isError()) {
            m_error = m_component->errorString();
            if (!item)
                delete object;
            m_opening = nullptr;
            op->fail("ComponentError",
                     m_error.isEmpty()
                         ? "Window component must be an Item with plugin/window properties"
                         : m_error);
            release();
            return;
        }
        if (revision != m_revision || !m_scope.active() || op->finished()) {
            release();
            return;
        }
        connect(w, &QWindow::activeChanged, this, &EchoPluginWindow::changed);
        connect(w, &QWindow::windowStateChanged, this, &EchoPluginWindow::changed);
        connect(w, &QWindow::visibilityChanged, this, &EchoPluginWindow::changed);
        connect(w, &QWindow::opacityChanged, this, &EchoPluginWindow::changed);
        connect(w->contentItem(), &QQuickItem::opacityChanged, this, &EchoPluginWindow::changed);
        connect(w, &QWindow::windowTitleChanged, this, &EchoPluginWindow::changed);
        connect(w, &QQuickWindow::xChanged, this, &EchoPluginWindow::changed);
        connect(w, &QQuickWindow::yChanged, this, &EchoPluginWindow::changed);
        connect(w, &QQuickWindow::widthChanged, this, &EchoPluginWindow::changed);
        connect(w, &QQuickWindow::heightChanged, this, &EchoPluginWindow::changed);
        connect(m_presentation, &EchoPluginContext::callbackFailed, this,
                [this](const QString &error) {
                    m_error = error;
                    release();
                });
        if (m_scope.host().attachPluginWindow)
            m_scope.host().attachPluginWindow(w);
        w->show();
#ifdef Q_OS_MACOS
        configureMacPluginWindow(w, m_definition.value("acceptFirstMouse", true).toBool(),
                                 m_definition.value("alwaysOnTop", false).toBool());
#endif
        if (options.value("focus", true).toBool())
            w->requestActivate();
        if (!m_definition.value("resizable", true).toBool()) {
            w->setMinimumSize(w->size());
            w->setMaximumSize(w->size());
        }
        m_opening = nullptr;
        op->succeed(QVariant::fromValue(this));
        emit changed();
    };
    connect(m_component, &QQmlComponent::statusChanged, this, [ready] { ready(); });
    QTimer::singleShot(0, this, ready);
    emit changed();
}
void EchoPluginWindow::saveBounds()
{
    if (m_window) {
        m_bounds = m_window->geometry();
        if (m_definition.value("rememberBounds", true).toBool()) {
            m_scope.host().permissions->saveWindowBounds(m_scope.installationId(), id(), m_bounds);
        }
    }
}
void EchoPluginWindow::release()
{
    ++m_revision;
    if (m_creating) {
        m_releasePending = true;
        return;
    }
    m_ignore = false;
    m_dragging = false;
    auto op = m_opening;
    m_opening = nullptr;
    if (op)
        op->cancel();
    saveBounds();
    if (m_presentation)
        m_presentation->beginRetire();
    m_presentation = nullptr;
    delete m_owner.data();
    m_owner = nullptr;
    if (m_window) {
        auto *w = m_window.data();
        m_window = nullptr;
        w->removeEventFilter(this);
        w->hide();
        delete w;
    }
    if (m_component) {
        m_component->deleteLater();
        m_component = nullptr;
    }
    emit changed();
}
bool EchoPluginWindow::eventFilter(QObject *o, QEvent *e)
{
    if (!m_reconfiguring && o == m_window &&
        (e->type() == QEvent::Close ||
         (e->type() == QEvent::Hide && m_window->windowState() != Qt::WindowMinimized))) {
        QTimer::singleShot(0, this, &EchoPluginWindow::release);
    }
    return QObject::eventFilter(o, e);
}
void EchoPluginWindow::hide()
{
    QTimer::singleShot(0, this, &EchoPluginWindow::release);
}
void EchoPluginWindow::close()
{
    QTimer::singleShot(0, this, &EchoPluginWindow::release);
}
EchoPluginOperation *EchoPluginWindow::run(std::function<void(QQuickWindow *)> command)
{
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    const auto revision = m_revision, epoch = m_scope.accountEpoch();
    const QPointer<QQuickWindow> window = m_window;
    QTimer::singleShot(0, op, [this, op, window, revision, epoch, command = std::move(command)] {
        if (op->finished())
            return;
        if (!window || window != m_window || revision != m_revision ||
            epoch != m_scope.accountEpoch() || !m_scope.active()) {
            op->cancel();
            return;
        }
        m_creating = true;
        const auto guard = qScopeGuard([this] {
            m_creating = false;
            if (std::exchange(m_releasePending, false))
                release();
        });
        command(window);
        if (!op->finished())
            op->succeed();
    });
    return op;
}
EchoPluginOperation *EchoPluginWindow::setBounds(const QVariantMap &values)
{
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    auto b = m_window->geometry();
    for (auto i = values.begin(); i != values.end(); ++i) {
        if (!QStringList{"x", "y", "width", "height"}.contains(i.key()) || !number(i.value()))
            return m_scope.rejected("InvalidArgument", "Invalid bounds");
        if (!positionSupported() && (i.key() == "x" || i.key() == "y"))
            return m_scope.rejected("Unsupported", "Global positioning is unavailable");
        if (i.key() == "x")
            b.moveLeft(i.value().toInt());
        else if (i.key() == "y")
            b.moveTop(i.value().toInt());
        else if (i.key() == "width")
            b.setWidth(i.value().toInt());
        else
            b.setHeight(i.value().toInt());
    }
    return run([this, b](QQuickWindow *window) { window->setGeometry(bounded(b)); });
}
EchoPluginOperation *EchoPluginWindow::setIgnoreMouseEvents(bool ignore)
{
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    return run([this, ignore](QQuickWindow *) {
        if (m_window) {
            m_ignore = ignore;
            m_reconfiguring = true;
            m_window->setFlag(Qt::WindowTransparentForInput, ignore);
            m_window->show();
#ifdef Q_OS_MACOS
            configureMacPluginWindow(m_window,
                                     m_definition.value("acceptFirstMouse", true).toBool(),
                                     m_definition.value("alwaysOnTop", false).toBool());
#endif
            m_reconfiguring = false;
            emit changed();
        }
    });
}
EchoPluginOperation *EchoPluginWindow::showOnTop(bool focus)
{
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    return run([this, focus](QQuickWindow *) {
        if (m_window) {
            if (m_window->windowState() == Qt::WindowMinimized)
                m_window->showNormal();
            m_window->raise();
            if (focus)
                m_window->requestActivate();
        }
    });
}
EchoPluginOperation *EchoPluginWindow::setAlwaysOnTop(bool value)
{
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    return show({{"alwaysOnTop", value}, {"focus", false}});
}
EchoPluginOperation *EchoPluginWindow::setOpacity(double value)
{
    if (!std::isfinite(value) || value < 0 || value > 1)
        return m_scope.rejected("InvalidArgument", "Opacity must be in 0..1");
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    if (!nativeOpacity() && !m_definition.value("transparent", false).toBool())
        return m_scope.rejected(
            "Unsupported", "This backend requires a transparent plugin window for content opacity");
    return run([this, value](QQuickWindow *) {
        if (m_window) {
            m_definition["opacity"] = value;
            if (nativeOpacity())
                m_window->setOpacity(value);
            else
                m_window->contentItem()->setOpacity(value);
        }
    });
}
EchoPluginOperation *EchoPluginWindow::setTitle(const QString &value)
{
    if (value.size() > 256 || value.contains(QChar::Null))
        return m_scope.rejected("InvalidArgument", "Invalid window title");
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    return run([this, value](QQuickWindow *) {
        if (m_window) {
            m_definition["title"] = value;
            m_window->setTitle(value);
        }
    });
}
EchoPluginOperation *EchoPluginWindow::minimize()
{
    if (!m_window)
        return m_scope.rejected("Unavailable", "Window is closed");
    return run([this](QQuickWindow *) {
        if (m_window)
            m_window->showMinimized();
    });
}
EchoPluginOperation *EchoPluginWindow::restore(bool focus)
{
    return showOnTop(focus);
}
Qt::Edges EchoPluginWindow::edges(const QString &d) const
{
    Qt::Edges e;
    if (d.contains('n'))
        e |= Qt::TopEdge;
    if (d.contains('s'))
        e |= Qt::BottomEdge;
    if (d.contains('e'))
        e |= Qt::RightEdge;
    if (d.contains('w'))
        e |= Qt::LeftEdge;
    return QStringList{"n", "ne", "e", "se", "s", "sw", "w", "nw"}.contains(d) ? e : Qt::Edges{};
}
EchoPluginOperation *EchoPluginWindow::startMove()
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (!m_window || !m_definition.value("movable", true).toBool() || !m_window->startSystemMove())
        op->fail("Unavailable", "System move unavailable; use pointer drag API where supported");
    else
        op->succeed();
    return op;
}
EchoPluginOperation *EchoPluginWindow::startResize(const QString &d)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (!m_window || !edges(d) || !m_definition.value("resizable", true).toBool() ||
        !m_window->startSystemResize(edges(d)))
        op->fail("Unavailable",
                 "System resize unavailable; use pointer resize API where supported");
    else
        op->succeed();
    return op;
}
EchoPluginOperation *EchoPluginWindow::beginMove(double x, double y)
{
    return beginResize("", x, y);
}
EchoPluginOperation *EchoPluginWindow::beginResize(const QString &d, double x, double y)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (!m_window || !positionSupported() || !std::isfinite(x) || !std::isfinite(y) ||
        qAbs(x) > 100000 || qAbs(y) > 100000 || (!d.isEmpty() && !edges(d)) ||
        !m_definition.value(d.isEmpty() ? "movable" : "resizable", true).toBool()) {
        op->fail("Unavailable", "Pointer window manipulation unavailable");
        return op;
    }
    m_dragging = true;
    m_edges = edges(d);
    m_pointer = QPoint(int(x), int(y));
    m_dragBounds = m_window->geometry();
    op->succeed();
    return op;
}
void EchoPluginWindow::updatePointer(double x, double y)
{
    if (!m_dragging || !m_window || !std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 100000 ||
        qAbs(y) > 100000)
        return;
    auto b = m_dragBounds;
    const auto delta = QPoint(int(x), int(y)) - m_pointer;
    if (!m_edges)
        b.translate(delta);
    else {
        if (m_edges & Qt::LeftEdge)
            b.setLeft(b.left() + delta.x());
        if (m_edges & Qt::RightEdge)
            b.setRight(b.right() + delta.x());
        if (m_edges & Qt::TopEdge)
            b.setTop(b.top() + delta.y());
        if (m_edges & Qt::BottomEdge)
            b.setBottom(b.bottom() + delta.y());
        auto size = bounded(b).size();
        if (m_edges & Qt::LeftEdge)
            b.setLeft(m_dragBounds.right() - size.width() + 1);
        else
            b.setWidth(size.width());
        if (m_edges & Qt::TopEdge)
            b.setTop(m_dragBounds.bottom() - size.height() + 1);
        else
            b.setHeight(size.height());
    }
    m_window->setGeometry(bounded(b));
}
void EchoPluginWindow::endPointer()
{
    m_dragging = false;
    saveBounds();
}
} // namespace EchoMusic
