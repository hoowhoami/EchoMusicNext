#pragma once
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace EchoMusic
{
struct TaskAction
{
    QString id, label;
    enum Variant
    {
        Ghost,
        Primary,
        Danger
    } variant = Ghost;
    bool enabled = true, closePanel = false;
    std::function<void()> invoke;
};
struct TaskDetail
{
    QString id, name, description, error, statusLabel;
    QList<TaskAction> actions;
};
class TaskDetailsModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by TaskCenterModel")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY expandedChanged)
public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        ErrorRole,
        StatusRole,
        ActionsRole
    };
    explicit TaskDetailsModel(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_items.size());
    }
    int count() const
    {
        return rowCount();
    }
    bool expanded() const
    {
        return m_expanded;
    }
    void setExpanded(bool expanded);
    QVariant data(const QModelIndex &, int) const override;
    QHash<int, QByteArray> roleNames() const override;
    void replace(QList<TaskDetail> items);
signals:
    void countChanged();
    void expandedChanged();

private:
    friend class TaskCenterModel;
    QList<TaskDetail> m_items;
    bool m_expanded = true;
};

/// Application-owned task presentation, not a job executor. Producers publish
/// on the GUI thread using an owner/generation handle; stale handles cannot
/// mutate, dismiss or run commands belonging to a replacement registration.
class TaskCenterModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Status
    {
        Pending,
        Running,
        Completed,
        Error,
        Aborted
    };
    Q_ENUM(Status)
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        TokenRole,
        NameRole,
        StatusRole,
        StatusLabelRole,
        PercentRole,
        ErrorRole,
        ActionsRole,
        DetailsRole,
        DismissibleRole,
        ProgressLabelRole,
        HasProgressRole,
        IconRole
    };
    struct Retention
    {
        bool manual = false;
        int delayMs = 0;
    };
    struct Policy
    {
        Retention completed{false, 5000}, error{true, 0}, aborted{false, 3000};
        static Policy actionRequired()
        {
            return {{true, 0}, {true, 0}, {false, 3000}};
        }
    };
    struct Content
    {
        // Producer summary (e.g. import phase / setProgress label); status
        // badges derive from Status and must not be replaced by this summary.
        QString name, statusLabel, error, icon;
        std::optional<double> percent;
        QList<TaskAction> actions;
        QList<TaskDetail> items;
    };
    struct Handle
    {
        QString id;
        quint64 generation = 0;
    };
    struct Registration
    {
        QString id;
        Content content;
        Status status = Running;
        int priority = 0;
        Policy policy;
        std::function<void()> cancel;
    };
    explicit TaskCenterModel(QObject *parent = nullptr);
    ~TaskCenterModel() override;
    int count() const
    {
        return int(m_entries.size());
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : count();
    }
    QVariant data(const QModelIndex &, int) const override;
    QHash<int, QByteArray> roleNames() const override;
    Handle registerTask(QObject &owner, Registration registration);
    bool update(const Handle &, Content content, std::optional<int> priority = {});
    /// Frequent progress events do not rebuild item models or reset scrolling.
    bool updateProgress(const Handle &, std::optional<double> percent, QString label = {});
    bool start(const Handle &);
    bool finish(const Handle &, Status terminal, Content content);
    bool remove(const Handle &);
    Q_INVOKABLE bool dismiss(const QString &id, const QString &token);
    Q_INVOKABLE bool invokeAction(const QString &id, const QString &token, const QString &actionId,
                                  const QString &detailId = {});
    Q_INVOKABLE void resetDetailExpansion();
    void shutdown();
signals:
    void countChanged();
    void taskRemoved(const QString &id, const QString &token);
    void closePanelRequested();

private:
    struct Entry
    {
        Registration task;
        QPointer<QObject> owner;
        quint64 generation;
        TaskDetailsModel *details;
        QMetaObject::Connection ownerConnection;
        qint64 expires = -1;
        bool progressReported = false;
    };
    int rowFor(const Handle &) const;
    bool dismissible(const Entry &) const;
    void applyContent(Entry &, Content);
    void scheduleExpiry();
    Retention retention(const Entry &) const;
    void removeAt(int row);
    std::vector<std::unique_ptr<Entry>> m_entries;
    QElapsedTimer m_clock;
    QTimer m_expiry;
    quint64 m_generation = 0;
    bool m_closed = false;
};
} // namespace EchoMusic
