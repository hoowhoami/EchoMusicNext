#include "SettingsKeys.h"
#include "PlaylistImportTaskCoordinator.h"
namespace EchoMusic
{
PlaylistImportTaskCoordinator::PlaylistImportTaskCoordinator(PlaylistImportService &service,
                                                             TaskCenterModel &tasks,
                                                             EchoMusic::SettingsStore &settings,
                                                             QObject *parent)
    : QObject(parent), m_service(service), m_tasks(tasks)
{
    service.setProperty(
        "backgroundReminderMuted",
        settings.value(EchoMusic::SettingsKeys::Library::ImportBackgroundReminderMuted, false));
    connect(&service, &PlaylistImportService::reminderChanged, this, [&service, &settings] {
        settings.setValue(EchoMusic::SettingsKeys::Library::ImportBackgroundReminderMuted,
                          service.property("backgroundReminderMuted"));
    });
    connect(&service, &PlaylistImportService::started, this, [this] {
        if (m_handle.generation)
            m_tasks.remove(m_handle);
        TaskCenterModel::Registration registration;
        registration.id = "playlist-import";
        registration.content.name = tr("歌单导入");
        registration.policy = TaskCenterModel::Policy::actionRequired();
        registration.cancel = [this] {
            if (m_service.busy())
                m_service.stop();
        };
        m_handle = m_tasks.registerTask(*this, std::move(registration));
        m_phase = PlaylistImportService::Preparing;
        publish();
    });
    connect(&service, &PlaylistImportService::changed, this,
            &PlaylistImportTaskCoordinator::publish);
    connect(&service, &PlaylistImportService::accountInvalidated, this, [this] {
        m_tasks.remove(m_handle);
        m_handle = {};
    });
}
void PlaylistImportTaskCoordinator::publish()
{
    if (!m_handle.generation)
        return;
    if (m_service.phase() == PlaylistImportService::Idle) {
        m_tasks.remove(m_handle);
        m_handle = {};
        return;
    }
    TaskCenterModel::Content content;
    content.name = m_service.title().isEmpty() ? tr("歌单导入") : m_service.title();
    content.statusLabel = m_service.statusLabel();
    content.error = m_service.errorMessage();
    if (m_service.total())
        content.percent = m_service.progress() * 100;
    content.actions.append({"view", m_service.busy() ? tr("查看进度") : tr("查看结果"),
                            TaskAction::Ghost, true, true, [this] { m_service.reopen(); }});
    if (m_service.busy())
        content.actions.append({"stop", m_service.cloudTask() ? tr("停止查看") : tr("停止导入"),
                                TaskAction::Ghost, true, false, [this] { m_service.stop(); }});
    for (const auto &value : m_service.stages()) {
        const auto stage = value.toMap();
        TaskDetail detail;
        detail.id = QString("stage-%1").arg(content.items.size());
        detail.name = stage.value("title").toString();
        detail.description = stage.value("artist").toString();
        detail.statusLabel = stage.value("status").toString();
        content.items.append(std::move(detail));
    }
    for (int row = 0; row < m_service.rowCount(); ++row) {
        TaskDetail detail;
        detail.id = QString("missed-%1").arg(row);
        detail.name =
            m_service.data(m_service.index(row), PlaylistImportService::TitleRole).toString();
        detail.description =
            m_service.data(m_service.index(row), PlaylistImportService::ArtistRole).toString();
        detail.error =
            m_service.data(m_service.index(row), PlaylistImportService::ReasonRole).toString();
        detail.statusLabel = tr("未匹配");
        content.items.append(std::move(detail));
    }
    const auto phase = m_service.phase();
    if (phase != m_phase && !m_service.busy()) {
        const auto status = phase == PlaylistImportService::Completed ? TaskCenterModel::Completed
                            : phase == PlaylistImportService::Failed  ? TaskCenterModel::Error
                                                                      : TaskCenterModel::Aborted;
        m_tasks.finish(m_handle, status, std::move(content));
    } else
        m_tasks.update(m_handle, std::move(content));
    m_phase = phase;
}
} // namespace EchoMusic
