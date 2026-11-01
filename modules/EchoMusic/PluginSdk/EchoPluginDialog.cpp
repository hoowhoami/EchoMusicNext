#include "EchoPluginDialog.h"
#include "EchoPluginContext.h"
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include "ShellSettings.h"
namespace EchoMusic
{
EchoPluginDialog::EchoPluginDialog(EchoPluginContext &scope, EchoPluginOperation *op)
    : QObject(&scope), m_scope(scope), m_opening(op)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&scope, &EchoPluginContext::activeChanged, this, &EchoPluginDialog::close);
    connect(&scope, &EchoPluginContext::sessionInvalidated, this, &EchoPluginDialog::close);
}
EchoPluginDialog::~EchoPluginDialog()
{
    delete m_owner.data();
}
void EchoPluginDialog::open(const QVariantMap &options, EchoPluginOperation *op)
{
    auto *window = m_scope.host().mainWindow ? m_scope.host().mainWindow() : nullptr;
    if (!window || !window->isVisible() || window->windowState() == Qt::WindowMinimized) {
        op->fail("Unavailable", "Main window is not visible");
        return;
    }
    m_component = new QQmlComponent(
        &m_scope.engine(), QUrl("qrc:/qt/qml/EchoMusic/PluginSdk/EchoPluginDialogOwner.qml"),
        QQmlComponent::Asynchronous, this);
    auto loaded = [this, window = QPointer(window), options, op] {
        if (!m_component || m_component->isLoading() || op->finished())
            return;
        if (!window || !window->isVisible() || window->windowState() == Qt::WindowMinimized ||
            !m_scope.active()) {
            op->cancel();
            return;
        }
        if (m_component->isError()) {
            // Only the trusted host recipe is compiled here, never plugin text.
            op->fail("ComponentError", m_component->errorString());
            return;
        }
        auto *owner = m_component->createWithInitialProperties(
            {{"hostItem", QVariant::fromValue(window->contentItem())},
             {"handle", QVariant::fromValue(this)},
             {"options", options}});
        m_component->deleteLater();
        m_component = nullptr;
        if (!owner) {
            op->fail("ComponentError", "Cannot create plugin dialog owner");
            return;
        }
        owner->setParent(this);
        if (auto *settings = m_scope.host().settings) {
            owner->setProperty("uiScale", settings->uiScale());
            connect(settings, &ShellSettings::uiScaleChanged, owner,
                    [owner, settings] { owner->setProperty("uiScale", settings->uiScale()); });
        }
        m_owner = owner;
        auto *presentation = m_scope.createPresentation(owner, "dialog");
        if (!presentation) {
            delete owner;
            op->fail("LimitExceeded", "Too many presentation scopes");
            return;
        }
        owner->setProperty("plugin", QVariant::fromValue(presentation));
        connect(owner, SIGNAL(ready()), this, SLOT(ready()));
        connect(owner, SIGNAL(closing()), this, SLOT(retirePresentation()));
        connect(owner, SIGNAL(finished()), this, SLOT(finished()));
        connect(owner, SIGNAL(failed()), this, SLOT(failed()));
        connect(op, &EchoPluginOperation::completed, this, [this, op] {
            if (op->state() != EchoPluginOperation::Succeeded)
                close();
        });
        emit changed();
        QMetaObject::invokeMethod(owner, "open");
    };
    connect(m_component, &QQmlComponent::statusChanged, this, loaded);
    QTimer::singleShot(0, m_component, loaded);
}

void EchoPluginDialog::ready()
{
    if (m_opening && !m_opening->finished()) {
        // Keep the QVariant result valid even when JS reads a completed operation
        // after the dialog closed. Discarding both handles permits collection.
        m_opening->setProperty("_dialogHandle",
                               QVariant::fromValue(m_scope.engine().newQObject(this)));
        m_opening->succeed(QVariant::fromValue(this));
    }
    m_opening = nullptr;
}
void EchoPluginDialog::failed()
{
    if (m_opening && !m_opening->finished())
        m_opening->fail("ComponentError",
                        "Dialog component must be an Item with plugin/dialog properties");
    finished();
}
void EchoPluginDialog::close()
{
    if (m_owner)
        QMetaObject::invokeMethod(m_owner, "close");
}
void EchoPluginDialog::retirePresentation()
{
    if (m_owner)
        if (auto *scope = m_owner->property("plugin").value<EchoPluginContext *>())
            scope->beginRetire();
}
void EchoPluginDialog::finished()
{
    if (!m_owner)
        return;
    // Retire commands now; keep the visual tree until Qt's closed handler ends.
    auto *owner = m_owner.data();
    retirePresentation();
    m_owner = nullptr;
    owner->deleteLater();
    if (m_opening && !m_opening->finished())
        m_opening->cancel();
    m_opening = nullptr;
    emit changed();
    emit closed();
    QTimer::singleShot(0, this, [this] {
        setParent(nullptr);
        QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
    });
}
} // namespace EchoMusic
