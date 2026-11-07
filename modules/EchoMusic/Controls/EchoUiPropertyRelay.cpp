#include "EchoUiPropertyRelay.h"
#include <QPointer>
#include <QQmlProperty>
#include <QtQml/private/qqmlanybinding_p.h>
#include <QtQml/private/qqmlproperty_p.h>
namespace EchoMusic
{
struct EchoUiPropertyRelay::Data
{
    QPointer<QObject> source, sink;
    QQmlProperty property, destination;
    QQmlAnyBinding binding;
    bool captured = false;
};
EchoUiPropertyRelay::EchoUiPropertyRelay(QObject *source, const QString &property, QObject *sink,
                                         const QString &sinkProperty)
    : m_data(std::make_unique<Data>())
{
    m_data->source = source;
    m_data->sink = sink;
    m_data->property = QQmlProperty(source, property);
    m_data->destination = QQmlProperty(sink, sinkProperty);
}
EchoUiPropertyRelay::~EchoUiPropertyRelay()
{
    restore();
}
void EchoUiPropertyRelay::capture()
{
    auto &data = *m_data;
    if (!data.source || !data.sink || !data.property.isValid() || !data.destination.isValid())
        return;
    // A new imperative assignment or Qt.binding on the source supersedes the
    // old host intent; do not keep a displaced binding alive on the sink.
    if (data.captured)
        QQmlAnyBinding::removeBindingFrom(data.destination);
    data.binding = QQmlAnyBinding::takeFrom(data.property);
    const auto value = data.property.read();
    data.captured = true;
    if (data.binding) {
        if (auto *binding = data.binding.asAbstractBinding())
            binding->setTarget(data.destination);
        data.binding.installOn(data.destination);
    } else
        QQmlPropertyPrivate::write(data.destination, value,
                                   QQmlPropertyData::DontRemoveBinding |
                                       QQmlPropertyData::BypassInterceptor);
}
void EchoUiPropertyRelay::restore()
{
    auto &data = *m_data;
    if (!std::exchange(data.captured, false))
        return;
    if (!data.sink) {
        data.binding = nullptr;
        return;
    }
    auto binding = QQmlAnyBinding::takeFrom(data.destination);
    const auto value = data.destination.read();
    data.binding = nullptr;
    if (!data.source)
        return;
    if (binding) {
        if (auto *abstract = binding.asAbstractBinding())
            abstract->setTarget(data.property);
        binding.installOn(data.property);
    } else
        QQmlPropertyPrivate::write(data.property, value,
                                   QQmlPropertyData::DontRemoveBinding |
                                       QQmlPropertyData::BypassInterceptor);
}
bool EchoUiPropertyRelay::bound() const
{
    return bool(m_data->binding);
}
QVariant EchoUiPropertyRelay::value() const
{
    return m_data->sink ? m_data->destination.read() : QVariant();
}
} // namespace EchoMusic
