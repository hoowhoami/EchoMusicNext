#include "EchoPluginToast.h"
#include "EchoPluginContext.h"
#include "ShellController.h"
#include "ApiClient.h"
#include "AccountService.h"
#include <cmath>
namespace EchoMusic
{
EchoPluginToast::EchoPluginToast(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope), m_service(scope.host().shell.toasts())
{
    connect(&scope, &EchoPluginContext::activeChanged, this, [this] {
        if (!m_scope.active())
            clear();
    });
    connect(&scope.host().account, &AccountService::sessionAdopted, this, &EchoPluginToast::clear);
    connect(&scope.host().api, &ApiClient::sessionChanged, this, &EchoPluginToast::clear);
    connect(scope.host().shell.toasts(), &ToastService::changed, this, [this] {
        auto *s = m_service.data();
        m_owned.removeIf([s](quint64 id) { return !s || !s->containsNotice(id); });
    });
}
EchoPluginToast::~EchoPluginToast()
{
    clear();
}
void EchoPluginToast::clear()
{
    if (m_service)
        m_service->dismissProvider(this);
    m_owned.clear();
}
void EchoPluginToast::dismiss(const QString &id)
{
    bool ok = false;
    auto n = id.toULongLong(&ok);
    if (ok && m_owned.contains(n))
        m_scope.host().shell.toasts()->dismiss(n);
}
EchoPluginOperation *EchoPluginToast::show(const QString &message, const QVariantMap &options,
                                           int tone)
{
    const QStringList allowed{"title", "durationMs", "presentation"};
    for (auto it = options.begin(); it != options.end(); ++it)
        if (!allowed.contains(it.key()))
            return m_scope.rejected("InvalidArgument", "Unknown toast option");
    const auto duration = options.value("durationMs", 2600).toDouble();
    const auto presentation = options.value("presentation", "auto").toString();
    if (message.trimmed().isEmpty() || message.size() > 4096 ||
        options.value("title").toString().size() > 256 || !std::isfinite(duration) ||
        duration < 0 || duration > 60000 || std::floor(duration) != duration ||
        !QStringList{"auto", "mini", "standard"}.contains(presentation))
        return m_scope.rejected("InvalidArgument", "Invalid toast");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    const auto id = m_scope.host().shell.toasts()->showOwned(
        *this, message, ToastService::Tone(tone), int(duration),
        ToastService::Presentation(QStringList{"auto", "mini", "standard"}.indexOf(presentation)),
        options.value("title").toString());
    if (!id)
        op->fail("Unavailable", "Toast service is unavailable");
    else {
        m_owned.insert(id);
        op->succeed(QVariantMap{{"id", QString::number(id)}});
    }
    return op;
}
EchoPluginOperation *EchoPluginToast::info(const QString &m, const QVariantMap &o)
{
    return show(m, o, 0);
}
EchoPluginOperation *EchoPluginToast::success(const QString &m, const QVariantMap &o)
{
    return show(m, o, 1);
}
EchoPluginOperation *EchoPluginToast::warning(const QString &m, const QVariantMap &o)
{
    return show(m, o, 2);
}
EchoPluginOperation *EchoPluginToast::danger(const QString &m, const QVariantMap &o)
{
    return show(m, o, 3);
}
} // namespace EchoMusic
