#include "EchoPluginContext.h"
#include <QDynamicPropertyChangeEvent>
namespace EchoMusic
{
QString EchoPluginContext::registrationKey(const QString &kind, const QString &id) const
{
    return m_package.id + ':' + kind + (m_activation ? "." + m_contextId : QString()) + ':' + id;
}
bool EchoPluginContext::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_presentationOwner && event->type() == QEvent::DynamicPropertyChange &&
        static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "active" &&
        !object->property("active").toBool())
        beginRetire();
    return QObject::eventFilter(object, event);
}
EchoPluginContext *EchoPluginContext::createPresentation(QObject *owner, const QString &surface)
{
    auto *root = activationContext();
    if (!root->active() || !owner)
        return nullptr;
    root->m_presentations.removeIf([](auto child) { return !child; });
    if (root->m_presentations.size() >= 128)
        return nullptr;
    auto *child = new EchoPluginContext(
        root->m_package, root->m_generation, root->m_engine, root->m_host, root->m_dataPath, root,
        root, "presentation." + surface + '.' + QString::number(++root->m_presentationSequence));
    root->m_presentations.append(child);
    child->m_presentationOwner = owner;
    owner->installEventFilter(child);
    child->commit();
    connect(root->ui(), &EchoPluginUi::stateChanged, child->ui(), &EchoPluginUi::stateChanged);
    connect(child, &EchoPluginContext::resourcesDrained, root, [root, child] {
        if (child->drained()) {
            child->deleteLater();
            emit root->resourcesDrained();
        }
    });
    connect(owner, &QObject::destroyed, child, [child] {
        child->m_presentationOwner = nullptr;
        child->beginRetire();
        emit child->resourcesDrained();
    });
    return child;
}
} // namespace EchoMusic
