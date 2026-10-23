#pragma once
#include "ApiClient.h"
#include "PlaylistMutationService.h"
#include "UserPlaylistModel.h"
#include <QUrl>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Temporary account-bound draft. Network writes belong to the library's single
/// writer; prepared image bytes and draft fields are released on close.
class PlaylistEditModel final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Library-owned playlist editor")
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool saving READ saving NOTIFY changed)
    Q_PROPERTY(bool selectingCover READ selectingCover NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed)
    Q_PROPERTY(QString tags READ tags WRITE setTags NOTIFY changed)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY changed)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(QString coverName READ coverName NOTIFY changed)
public:
    PlaylistEditModel(ApiClient &, UserPlaylistModel &, PlaylistMutationService &,
                      QObject *parent = nullptr);
    ~PlaylistEditModel() override;
    bool loading() const
    {
        return m_request != 0;
    }
    bool saving() const
    {
        return m_saving;
    }
    bool selectingCover() const
    {
        return m_reading;
    }
    bool loaded() const
    {
        return m_loaded;
    }
    bool dirty() const;
    QString errorMessage() const
    {
        return m_error;
    }
    QString title() const
    {
        return m_title;
    }
    QString tags() const
    {
        return m_tags;
    }
    QString description() const
    {
        return m_description;
    }
    QString coverUrl() const
    {
        return m_cover;
    }
    QString coverName() const
    {
        return m_coverName;
    }
    void setTitle(const QString &);
    void setTags(const QString &);
    void setDescription(const QString &);
    bool canEdit(quint64 id) const;
    Q_INVOKABLE void open(quint64 id);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void close();
    Q_INVOKABLE void selectCover(const QUrl &);
    Q_INVOKABLE void save();
signals:
    void changed();
    void saved();
    void invalidated();
    void notice(const QString &, bool warning);

private:
    friend class PlaylistEditModelTestAccess;
    void accept(quint64, const QByteArray &);
    void fail(const QString &);
    void ready();
    ApiClient &m_api;
    UserPlaylistModel &m_source;
    PlaylistMutationService &m_mutations;
    quint64 m_id = 0, m_request = 0, m_generation = 0, m_selection = 0, m_version = 0;
    quint32 m_page = 1;
    ApiClient::PlaylistUpdate m_update{};
    QJsonObject m_snapshot;
    QString m_title, m_tags, m_description, m_cover, m_coverName, m_error;
    QByteArray m_png;
    QObject *m_coverWorker = nullptr;
    bool m_unknownRefreshed = false;
    bool m_loaded = false, m_saving = false, m_reading = false, m_detailRequest = false;
};
} // namespace EchoMusic
