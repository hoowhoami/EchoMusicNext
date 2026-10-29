#pragma once
#include "PlaylistImportService.h"
#include "TaskCenterModel.h"
#include "SettingsStore.h"

namespace EchoMusic
{
/// Shell projects a library-owned run; closing its QML dialog never owns or
/// cancels the API workflow. Registrations are replaced per run/account.
class PlaylistImportTaskCoordinator final : public QObject
{
    Q_OBJECT
public:
    PlaylistImportTaskCoordinator(PlaylistImportService &service, TaskCenterModel &tasks,
                                  EchoMusic::SettingsStore &settings, QObject *parent = nullptr);

private:
    void publish();
    PlaylistImportService &m_service;
    TaskCenterModel &m_tasks;
    TaskCenterModel::Handle m_handle;
    PlaylistImportService::Phase m_phase = PlaylistImportService::Idle;
};
} // namespace EchoMusic
