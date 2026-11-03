#pragma once
#include "ApiClient.h"
#include "CommentAuthorService.h"
#include "RouteController.h"
#include <QAbstractListModel>
#include <QJsonArray>
#include <QPointer>
#include <QJsonObject>
#include <QSet>
#include <QQmlEngine>
#include <memory>
namespace EchoMusic
{
class CommentsModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by CommentsController")
public:
    enum Role
    {
        Entry = Qt::UserRole + 1,
        Kind,
        RootId
    };
    explicit CommentsModel(QObject *p = nullptr) : QAbstractListModel(p) {}
    int rowCount(const QModelIndex &p = {}) const override
    {
        return p.isValid() ? 0 : m_rows.size();
    }
    QVariant data(const QModelIndex &, int) const override;
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Entry, "entry"}, {Kind, "kind"}, {RootId, "rootId"}};
    }
    void replace(QList<QVariantMap> rows);
    void clearProjection();

private:
    friend class CommentExtent;
    QVariantMap geometryEntry(int row) const;
    QList<QVariantMap> m_rows;
};
/// Per-resource, session-isolated comments. The flat presentation model keeps
/// root comments and expanded floor replies in one virtual native item view.
class CommentsController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a detail route")
    Q_PROPERTY(EchoMusic::CommentsModel *model READ model CONSTANT)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(QVariantList hotWords READ hotWords NOTIFY changed)
    Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY changed)
    Q_PROPERTY(QString selection READ selection NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool fetching READ fetching NOTIFY changed)
    Q_PROPERTY(bool hasComments READ hasComments NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int sendCooldownSeconds READ sendCooldownSeconds NOTIFY sendCooldownChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY changed)
    Q_PROPERTY(QString replyDraft READ replyDraft WRITE setReplyDraft NOTIFY changed)
    Q_PROPERTY(QString totalText READ totalText NOTIFY changed)
public:
    CommentsController(ApiClient &, RouteContext &, bool network, QObject *parent,
                       CommentAuthorService *authors = nullptr);
    CommentsController(ApiClient &, bool network, QObject *parent,
                       CommentAuthorService *authors = nullptr);
    ~CommentsController() override;
    CommentsModel *model()
    {
        return &m_model;
    }
    QVariantList categories() const
    {
        return m_categories.toVariantList();
    }
    QVariantList hotWords() const
    {
        return m_words.toVariantList();
    }
    int tab() const
    {
        return m_tab;
    }
    QString selection() const;
    void setTab(int);
    bool loading() const;
    bool fetching() const;
    bool hasComments() const;
    bool hasMore() const;
    bool busy() const;
    int sendCooldownSeconds() const
    {
        return m_api.messageSendCooldownSeconds();
    }
    bool signedIn() const;
    QString errorMessage() const;
    QString totalText() const;
    QString draft() const
    {
        return m_draft;
    }
    void setDraft(const QString &);
    QString replyDraft() const
    {
        return m_replyDraft;
    }
    void setReplyDraft(const QString &);
    Q_INVOKABLE void beginReply(const QVariantMap &, const QString &rootId);
    Q_INVOKABLE void closeReply();
    Q_INVOKABLE void submitReply();
    void configure(ApiClient::CommentResource, const QString &id, const QString &name,
                   quint64 mix = 0);
    void setPresented(bool);
    void start();
    void suspend();
    void clear();
    void shutdown();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void select(const QString &);
    Q_INVOKABLE void toggleThread(const QString &id);
    Q_INVOKABLE void toggleContent(const QString &id);
    Q_INVOKABLE void loadThread(const QString &id, bool reset = false);
    Q_INVOKABLE bool canDelete(const QVariantMap &) const;
    Q_INVOKABLE void send();
    Q_INVOKABLE void reply(const QVariantMap &target, const QString &rootId,
                           const QString &content);
    Q_INVOKABLE void remove(const QVariantMap &target, const QString &rootId);
    Q_INVOKABLE QVariantMap comment(const QString &id) const;
signals:
    void changed();
    void initialFinished();
    void readFailed(const QString &message);
    void scopeReset();
    void notice(const QString &, bool warning);
    void submitted(bool reply);
    void sendCooldownChanged();

private:
    friend class DetailsTestAccess;
    CommentsController(ApiClient &, RouteContext *, bool network, QObject *,
                       CommentAuthorService *);
    bool active() const
    {
        return m_presented && (!m_route || m_route->active());
    }
    struct Stream
    {
        QJsonArray rows;
        QJsonArray singerRows;
        QJsonValue total;
        QString error;
        quint32 next = 1;
        quint64 revision = 0;
        bool more = true, loaded = false, expanded = false;
    };
    enum class Operation
    {
        List,
        Send,
        Reply,
        Delete
    };
    struct Pending
    {
        Operation operation;
        QString stream, draft;
        quint64 epoch, revision, session;
        quint32 page;
    };
    QString threadKey(const QString &id) const;
    void rememberThread(const QString &id);
    Stream &stream(const QString &key);
    const Stream *current() const;
    QString currentKey() const;
    void request(const QString &key, bool reset);
    void rebuild();
    void finishInitialIfReady();
    void accept(quint64, const QByteArray &);
    void fail(quint64, const QByteArray &);
    void cancel();
    ApiClient::CommentWrite write(const QVariantMap &, const QString &rootId) const;
    void mutation(quint64, Operation, const QString &draft = {}, const QString &rootId = {});
    CommentAuthorService *m_authors;
    ApiClient &m_api;
    QPointer<RouteContext> m_route;
    CommentsModel m_model;
    QHash<QString, std::shared_ptr<Stream>> m_streams;
    QHash<quint64, Pending> m_pending;
    QHash<QString, QVariantMap> m_threadTargets;
    QSet<QString> m_expandedContents;
    QJsonArray m_categories, m_words;
    QString m_id, m_name, m_category, m_word, m_draft;
    QVariantMap m_replyTarget;
    QString m_replyRoot, m_replyDraft;
    quint64 m_replyRevision = 0;
    ApiClient::CommentResource m_resource = ApiClient::CommentResource::Music;
    quint64 m_mix = 0, m_epoch = 0;
    int m_tab = 0;
    bool m_presented = true;
    bool m_network, m_started = false, m_closed = false;
    bool m_initialSettled = false, m_initialCompleted = false, m_waitingAuthors = false;
};
} // namespace EchoMusic
