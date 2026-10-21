#pragma once
#include "StoredAccount.h"
#include <QAbstractListModel>
#include <QQueue>
#include <QSet>
#include <optional>
namespace EchoMusic
{
class AccountService;
class ApiClient;
class StorageService;
/// One account catalog owner. Credentials and selection commit in local SQLite.
class SavedAccountModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    enum Role
    {
        UserIdRole = Qt::UserRole + 1,
        NicknameRole,
        AvatarRole,
        CurrentRole,
        DurationRole,
        DurationLoadingRole,
        AvatarIdentityIconRole,
        AvatarIdentityLabelRole
    };
    SavedAccountModel(ApiClient &api, AccountService &account, StorageService &storage,
                      QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const
    {
        return int(m_rows.size());
    }
    bool busy() const
    {
        return m_operation.has_value() || !m_queue.isEmpty();
    }
    bool ready() const
    {
        return m_ready && !m_closing;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    Q_INVOKABLE bool switchAccount(quint64 userId);
    Q_INVOKABLE bool forgetAccount(quint64 userId);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void beginDurationQueries();
    Q_INVOKABLE void endDurationQueries();
    /// Explicit login owns the UI even while startup SQL I/O is pending.
    void suppressStartupRestoration();
    void setStartupHeld(bool held);
    /// True only during automatic startup adoption, never an explicit switch/login.
    bool restoringStartupAccount() const;
    void shutdown();
signals:
    void changed();
    void switched();
    void notice(const QString &message, bool warning);
    void closed();

private:
    friend class SavedAccountModelTestAccess;
    enum class Kind
    {
        Save,
        Switch,
        Remove,
        Deselect
    };
    struct Operation
    {
        quint64 id = 0, generation = 0;
        Kind kind = Kind::Save;
        StoredAccount record;
        QByteArray session;
        bool restoring = false, explicitRemoval = false;
    };
    void enqueue(Operation operation);
    void pump();
    void sqlReady(quint64 id, const StoredAccounts &accounts, quint64 active);
    void fail();
    void finish();
    void publish(const StoredAccounts &accounts);
    void pumpDurationQueries();
    const StoredAccount *find(quint64 userId) const;
    ApiClient &m_api;
    AccountService &m_account;
    StorageService &m_storage;
    StoredAccounts m_rows;
    QQueue<Operation> m_queue;
    std::optional<Operation> m_operation;
    QByteArray m_lastSession;
    QString m_error;
    QHash<quint64, quint64> m_durationRequests;
    QHash<quint64, qint64> m_durations;
    QQueue<quint64> m_durationQueue;
    quint64 m_durationRevision = 0;
    bool m_durationOpen = false;
    quint64 m_next = 0, m_generation = 0;
    quint64 m_presentedUser = 0;
    bool m_ready = false, m_restoring = false, m_closing = false, m_drained = false;
    bool m_startupHeld = false;
};
} // namespace EchoMusic
