#pragma once
#include "UserPlaylistModel.h"
#include "PlaylistMutationService.h"
#include <QAbstractListModel>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// GUI-owned import run, independent of its dialog. Workers own file reads;
/// API requests and callbacks are bound to the run and account revisions.
class PlaylistImportService final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Library-owned import service")
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool hasProgress MEMBER m_enteredProgress NOTIFY changed)
    Q_PROPERTY(bool cloudTask READ cloudTask NOTIFY changed)
    Q_PROPERTY(bool loadingTargets READ loadingTargets NOTIFY changed)
    Q_PROPERTY(QVariantList targets READ targets NOTIFY changed)
    Q_PROPERTY(QVariantList stages READ stages NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString statusLabel READ statusLabel NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString duplicateName READ duplicateName NOTIFY changed)
    Q_PROPERTY(bool backgroundReminderMuted MEMBER m_backgroundReminderMuted NOTIFY reminderChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY changed)
    Q_PROPERTY(quint64 total READ total NOTIFY changed)
    Q_PROPERTY(quint64 imported READ imported NOTIFY changed)
    Q_PROPERTY(quint64 missed READ missed NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    enum Phase
    {
        Idle,
        Preparing,
        Uploading,
        Cloud,
        Completed,
        Failed,
        Stopped
    };
    Q_ENUM(Phase)
    enum Role
    {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        ReasonRole
    };
    PlaylistImportService(ApiClient &api, UserPlaylistModel &source,
                          PlaylistMutationService &mutations, QObject *parent = nullptr);
    ~PlaylistImportService() override;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_missedRows.size();
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override
    {
        return {{TitleRole, "trackTitle"}, {ArtistRole, "artist"}, {ReasonRole, "reason"}};
    }
    Phase phase() const
    {
        return m_phase;
    }
    bool busy() const
    {
        return m_phase == Preparing || m_phase == Uploading || m_phase == Cloud;
    }
    bool cloudTask() const
    {
        return m_task != 0;
    }
    bool loadingTargets() const
    {
        return m_stage == Catalog;
    }
    QVariantList targets() const;
    QVariantList stages() const;
    QString title() const
    {
        return m_title;
    }
    QString statusLabel() const
    {
        return m_label;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString duplicateName() const
    {
        return m_duplicate;
    }
    int imageCount() const
    {
        return m_imageCount;
    }
    quint64 total() const
    {
        return m_total;
    }
    quint64 imported() const
    {
        return m_imported;
    }
    quint64 missed() const
    {
        return m_missed;
    }
    double progress() const;
    /// Picker metadata only: retain the first nine, and validate only those
    /// sizes. File content/type checks remain on the import worker.
    Q_INVOKABLE QVariantMap prepareScreenshotSelection(const QList<QUrl> &files) const;
    Q_INVOKABLE void refreshTargets();
    Q_INVOKABLE bool startLink(const QString &text);
    Q_INVOKABLE bool startScreenshot(const QList<QUrl> &files, const QString &targetId,
                                     const QString &newTitle);
    Q_INVOKABLE void confirmDuplicate(bool accepted, const QString &title = {});
    Q_INVOKABLE void stop();
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void reopen()
    {
        emit showRequested();
    }
signals:
    void changed();
    void reminderChanged();
    void notice(const QString &message, bool warning);
    void started();
    void showRequested();
    void duplicateConfirmationRequested();
    void accountInvalidated();

private:
    friend class PlaylistImportServiceTestAccess;
    enum Stage
    {
        None,
        Catalog,
        Creating,
        Image,
        Adding,
        Polling,
        Result
    };
    void reset();
    bool beginRun();
    void beginCatalog(bool create);
    void createTarget();
    void uploadNext();
    void poll();
    void succeed(quint64 request, const QByteArray &payload);
    void fail(const QString &message);
    void complete();
    ApiClient &m_api;
    UserPlaylistModel &m_source;
    PlaylistMutationService &m_mutations;
    QTimer m_poll;
    Phase m_phase = Idle;
    Stage m_stage = None;
    quint64 m_generation = 0, m_session = 0, m_request = 0, m_task = 0, m_target = 0, m_total = 0,
            m_imported = 0, m_missed = 0, m_catalogVersion = 0, m_catalogTotal = 0;
    quint32 m_page = 1;
    int m_imageIndex = 0, m_imageCount = 0, m_cloudStatus = 0;
    bool m_backgroundReminderMuted = false;
    bool m_newTarget = false, m_enteredProgress = false;
    QString m_title, m_label, m_error, m_duplicate, m_serial;
    QList<QByteArray> m_images;
    QList<QJsonObject> m_targets, m_catalogRows, m_missedRows;
};
} // namespace EchoMusic
