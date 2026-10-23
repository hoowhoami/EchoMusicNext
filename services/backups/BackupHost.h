#pragma once
#include <QObject>
#include <QVariant>
#include <QHash>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
namespace EchoMusic
{
struct BackupResult
{
    QVariant value;
    QString code, error;
};
using BackupCompletion = std::function<void(BackupResult)>;
using BackupCancel = std::shared_ptr<std::atomic_bool>;
/// Application-owned bridge. The host owns archive validation/restoration; a
/// storage provider only transports bytes. No plugin/GUI engine dependency.
class BackupHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(QVariantList providerEntries READ providerEntries NOTIFY providerEntriesChanged)
    Q_PROPERTY(bool providerBusy READ providerBusy NOTIFY providerEntriesChanged)
    Q_PROPERTY(QString providerError READ providerError NOTIFY providerEntriesChanged)
    Q_PROPERTY(QString selectedProvider READ selectedProvider NOTIFY providerEntriesChanged)
public:
    using Invoke =
        std::function<void(const QString &, const QVariantMap &, BackupCancel, BackupCompletion)>;
    explicit BackupHost(QObject *parent = nullptr);
    virtual void createArchive(const QString &owner, const QVariantMap &scope, BackupCancel,
                               BackupCompletion) = 0;
    virtual void inspectArchive(const QString &owner, const QByteArray &data, BackupCancel,
                                BackupCompletion) = 0;
    virtual void requestRestore(const QString &owner, const QString &token,
                                const QVariantMap &scope, BackupCancel, BackupCompletion) = 0;
    virtual void releaseOwner(const QString &owner) = 0;
    bool addProvider(const QString &key, quint64 generation, const QString &owner,
                     QVariantMap metadata, Invoke invoke);
    void removeProvider(const QString &key, quint64 generation);
    QVariantList providers() const;
    QVariantList providerEntries() const
    {
        return m_entries;
    }
    bool providerBusy() const
    {
        return bool(m_providerCancel);
    }
    QString providerError() const
    {
        return m_error;
    }
    QString selectedProvider() const
    {
        return m_selected;
    }
    Q_INVOKABLE void refreshProvider(const QString &key);
    Q_INVOKABLE void saveToProvider(const QString &key, bool settings, bool plugins);
    Q_INVOKABLE void loadFromProvider(const QString &key, const QString &id);
    Q_INVOKABLE void removeFromProvider(const QString &key, const QString &id);
    Q_INVOKABLE void cancelProvider();
    void invalidateOwner(const QString &owner);
    void shutdownProviders();
signals:
    void providersChanged();
    void providerEntriesChanged();
    void providerNotice(const QString &message, bool failure);
    void providerSaved();
    void providerInspection(const QVariantMap &result);

private:
    struct Provider
    {
        quint64 generation;
        QString owner;
        QVariantMap metadata;
        Invoke invoke;
    };
    bool begin(const QString &key);
    void invoke(const QString &key, const QString &method, const QVariantMap &arguments,
                BackupCancel, BackupCompletion);
    void finish(BackupCancel, const BackupResult &result);
    QHash<QString, Provider> m_providers;
    QVariantList m_entries;
    QString m_selected, m_error;
    BackupCancel m_providerCancel;
    quint64 m_run = 0;
    QTimer m_deadline;
};
} // namespace EchoMusic
