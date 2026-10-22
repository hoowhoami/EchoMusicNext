#include "EchoPluginScroll.h"
#include "EchoPluginContext.h"
#include "EchoPluginUiParts.h"
#include <cmath>
namespace EchoMusic
{
namespace
{
QVariantMap selector(QVariantMap v)
{
    v["role"] = "scroll.container";
    return v;
}
} // namespace
QVariant EchoPluginScroll::snapshot(EchoUiPartRef *r) const
{
    return r && r->belongsTo(&m_scope) && r->valid() && r->role() == "scroll.container"
               ? QVariant(r->state())
               : QVariant::fromValue(nullptr);
}
EchoUiPartRef *EchoPluginScroll::query(const QVariantMap &v)
{
    return m_scope.ui()->parts()->query(selector(v));
}
QVariantList EchoPluginScroll::queryAll(const QVariantMap &v)
{
    return m_scope.ui()->parts()->queryAll(selector(v));
}
EchoUiPartRef *EchoPluginScroll::current()
{
    const auto all = queryAll({});
    for (const auto &v : all) {
        auto *r = v.value<EchoUiPartRef *>();
        if (r && r->visible())
            return r;
    }
    return nullptr;
}
EchoPluginRegistration *EchoPluginScroll::observe(const QString &id, const QVariantMap &v,
                                                  const QJSValue &callback, bool immediate)
{
    return m_scope.ui()->parts()->observe(id, selector(v), callback, immediate);
}
EchoPluginOperation *EchoPluginScroll::invoke(EchoUiPartRef *r, const QString &action, double value)
{
    if (!r || !r->valid() || !r->belongsTo(&m_scope) || r->role() != "scroll.container")
        return m_scope.rejected("Unavailable", "Scroll container is unavailable");
    if (!std::isfinite(value) || std::abs(value) > 1e9)
        return m_scope.rejected("InvalidArgument", "Invalid scroll position");
    return r->invoke(action, {{"position", value}});
}
EchoPluginOperation *EchoPluginScroll::scrollTo(EchoUiPartRef *r, double v)
{
    return invoke(r, "scrollTo", v);
}
EchoPluginOperation *EchoPluginScroll::scrollBy(EchoUiPartRef *r, double v)
{
    return invoke(r, "scrollBy", v);
}
EchoPluginOperation *EchoPluginScroll::top(EchoUiPartRef *r)
{
    return invoke(r, "top");
}
EchoPluginOperation *EchoPluginScroll::bottom(EchoUiPartRef *r)
{
    return invoke(r, "bottom");
}
} // namespace EchoMusic
