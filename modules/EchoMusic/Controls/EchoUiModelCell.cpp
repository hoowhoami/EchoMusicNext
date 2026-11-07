#include "EchoUiModelCell.h"
#include "EchoUiPresentation.h"
#include "EchoUiScope.h"
#include <QTimer>
namespace EchoMusic
{
EchoUiModelCell::EchoUiModelCell(QQuickItem *parent) : EchoUiContentHost(parent)
{
    setImplicitWidth(160);
    setImplicitHeight(32);
    setActive(false);
    auto *presentation = new EchoUiPresentation(this, this);
    connect(presentation, &EchoUiPresentation::changed, this, &EchoUiModelCell::updateActive);
    auto *scope = EchoUiScope::get(this, true);
    scope->setResourceKey("@model:pending");
    connect(scope, &EchoUiScope::changed, this, [this, scope] {
        if (!scope->active())
            retire();
        else
            schedule();
    });
}
void EchoUiModelCell::retire()
{
    setActive(false);
    setFactory(nullptr);
    if (m_cellFactory)
        m_cellFactory->deleteLater();
    m_cellFactory = nullptr;
}
void EchoUiModelCell::schedule()
{
    retire();
    if (!m_complete || std::exchange(m_pending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        rebuild();
    });
}
void EchoUiModelCell::setRecipe(QObject *value)
{
    auto *recipe = qobject_cast<EchoUiModelRecipe *>(value);
    if (m_recipe == recipe)
        return;
    for (const auto &connection : std::as_const(m_recipeConnections))
        disconnect(connection);
    m_recipeConnections.clear();
    m_recipe = recipe;
    if (recipe) {
        m_recipeConnections << connect(recipe, &EchoUiModelRecipe::changed, this,
                                       &EchoUiModelCell::schedule)
                            << connect(recipe, &QObject::destroyed, this,
                                       &EchoUiModelCell::schedule);
    }
    schedule();
    emit cellChanged();
}
void EchoUiModelCell::setRowData(const QVariantMap &value)
{
    if (m_row != value) {
        m_row = value;
        updateResourceKey();
        schedule();
        emit cellChanged();
    }
}
void EchoUiModelCell::setEntryData(const QVariantMap &value)
{
    if (m_entry != value) {
        m_entry = value;
        updateResourceKey();
        setImplicitWidth(value.value("width", 160).toDouble());
        setImplicitHeight(value.value("height", 32).toDouble());
        schedule();
        emit cellChanged();
    }
}
void EchoUiModelCell::setViewRow(int value)
{
    if (m_viewRow != value) {
        m_viewRow = value;
        updateResourceKey();
        schedule();
        emit cellChanged();
    }
}
void EchoUiModelCell::setViewColumn(int value)
{
    if (m_viewColumn != value) {
        m_viewColumn = value;
        updateResourceKey();
        schedule();
        emit cellChanged();
    }
}
void EchoUiModelCell::updateResourceKey()
{
    const auto key = m_entry.value("_echoUiKey").toString();
    QString resource;
    for (const auto *field : {"resourceId", "id", "key"})
        if (!(resource = m_row.value(field).toString()).isEmpty())
            break;
    EchoUiScope::get(this, true)
        ->setResourceKey(key.isEmpty()        ? "@model:pending"
                         : resource.isEmpty() ? "@model:" + key + ':' + QString::number(m_viewRow) +
                                                    ":c" + QString::number(m_viewColumn)
                                              : "model:" + key + ':' + resource + ":c" +
                                                    QString::number(m_viewColumn));
}
void EchoUiModelCell::componentComplete()
{
    EchoUiContentHost::componentComplete();
    m_complete = true;
    schedule();
}
void EchoUiModelCell::rebuild()
{
    if (!m_recipe || !m_recipe->active() || m_entry.value("key").toString().isEmpty() ||
        !EchoUiScope::get(this)->active())
        return;
    const QVariantMap row{{"index", m_viewRow}, {"column", m_viewColumn}, {"values", m_row}};
    m_cellFactory = m_recipe->factory(this, row, m_entry);
    setFactory(m_cellFactory);
    updateActive();
}
void EchoUiModelCell::updateActive()
{
    const auto *scope = EchoUiScope::get(this);
    const bool measure = width() <= 0 || height() <= 0;
    const bool live = m_recipe && m_recipe->active() && scope && scope->active() && isVisible();
    setActive(live && (EchoUiPresentation::presented(this) ||
                       (measure && EchoUiPresentation::measurable(this))));
}
} // namespace EchoMusic
