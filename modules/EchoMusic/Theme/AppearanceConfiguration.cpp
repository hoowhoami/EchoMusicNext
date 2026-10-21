#include "AppearanceConfiguration.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QFutureWatcher>
#include <QtConcurrent>
namespace EchoMusic
{
bool AppearanceConfiguration::isJson(const QVariantMap &values)
{
    const auto json = QJsonObject::fromVariantMap(values);
    return json.toVariantMap() == values &&
           QJsonDocument(json).toJson(QJsonDocument::Compact).size() <= 64 * 1024;
}
AppearanceConfiguration::AppearanceConfiguration(
    QString file, QVariantMap defaults, std::function<QString(const QVariantMap &)> validator,
    QObject *parent)
    : QObject(parent), m_file(std::move(file)), m_defaults(std::move(defaults)),
      m_values(m_defaults), m_validate(std::move(validator))
{
    m_workers.setMaxThreadCount(1);
    QFile savedFile(m_file);
    QVariantMap saved;
    if (savedFile.open(QIODevice::ReadOnly) && savedFile.size() <= 64 * 1024)
        saved = QJsonDocument::fromJson(savedFile.readAll()).object().toVariantMap();
    auto combined = m_defaults;
    for (auto it = saved.begin(); it != saved.end(); ++it)
        combined.insert(it.key(), it.value());
    if (isJson(combined) && (!m_validate || m_validate(combined).isEmpty()))
        m_values = combined;
    m_requested = m_values;
}
AppearanceConfiguration::AppearanceConfiguration(AppearanceConfiguration &source,
                                                 std::function<bool()> guard, QObject *parent)
    : QObject(parent), m_delegate(&source), m_guard(std::move(guard)), m_proxy(true)
{
    connect(&source, &AppearanceConfiguration::changed, this, &AppearanceConfiguration::changed);
    connect(&source, &AppearanceConfiguration::errorChanged, this,
            &AppearanceConfiguration::errorChanged);
    connect(&source, &AppearanceConfiguration::busyChanged, this,
            &AppearanceConfiguration::busyChanged);
    connect(&source, &AppearanceConfiguration::drained, this, &AppearanceConfiguration::drained);
}
AppearanceConfiguration::~AppearanceConfiguration()
{
    m_workers.waitForDone();
}
bool AppearanceConfiguration::patch(const QVariantMap &patch)
{
    if (m_proxy)
        return active() && m_delegate->patch(patch);
    if (!isJson(patch))
        return false;
    auto next = m_requested;
    for (auto it = patch.begin(); it != patch.end(); ++it)
        next.insert(it.key(), it.value());
    return apply(next);
}
bool AppearanceConfiguration::reset()
{
    return m_proxy ? active() && m_delegate->reset() : apply(m_defaults);
}
bool AppearanceConfiguration::apply(const QVariantMap &next)
{
    if (!m_active)
        return false;
    auto error = !isJson(next) ? QStringLiteral("Configuration must be JSON below 64 KiB")
                 : m_validate  ? m_validate(next)
                               : QString();
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
    if (!error.isEmpty())
        return false;
    if (m_requested == next)
        return true;
    if (m_pending >= 64) {
        m_error = "Configuration write queue is full";
        emit errorChanged();
        return false;
    }
    m_requested = next;
    ++m_pending;
    emit busyChanged();
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, next] {
        const auto error = watcher->result();
        watcher->deleteLater();
        --m_pending;
        if (error.isEmpty()) {
            m_values = next;
            emit changed();
        } else {
            m_error = error;
            emit errorChanged();
            if (!m_pending)
                m_requested = m_values;
        }
        emit busyChanged();
        if (!m_pending)
            emit drained();
    });
    watcher->setFuture(QtConcurrent::run(&m_workers, [file = m_file, next] {
        if (!QDir().mkpath(QFileInfo(file).absolutePath()))
            return QString("Cannot create appearance configuration directory");
        QSaveFile output(file);
        const auto bytes =
            QJsonDocument(QJsonObject::fromVariantMap(next)).toJson(QJsonDocument::Compact);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
            !output.commit())
            return QString("Cannot persist appearance configuration");
        return QString();
    }));
    return true;
}
void AppearanceConfiguration::publish()
{
    if (m_active)
        return;
    m_active = true;
    emit changed();
}
void AppearanceConfiguration::retire()
{
    if (!m_active)
        return;
    m_active = false;
    m_validate = {};
    emit changed();
}
} // namespace EchoMusic
