#include "PresentationScope.h"
#include "EchoUiPresentation.h"
#include <utility>
namespace EchoMusic
{
PresentationScope::PresentationScope(QQuickItem *parent) : QQuickItem(parent)
{
    auto *observer = new EchoUiPresentation(this, this);
    connect(observer, &EchoUiPresentation::changed, &m_callbacks, [this] { refresh(); });
    refresh();
}
PresentationScope::~PresentationScope() = default;
void PresentationScope::refresh()
{
    const bool next = EchoUiPresentation::presented(this);
    if (std::exchange(m_presented, next) != next)
        emit presentedChanged();
}
} // namespace EchoMusic
