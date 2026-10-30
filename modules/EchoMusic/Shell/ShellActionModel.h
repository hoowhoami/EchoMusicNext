#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include "SettingsStore.h"
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace EchoMusic
{
/// GUI-thread action registry and layout preferences. Capacity never writes the
/// saved placement. Unloaded contributors retain their preferences, not callbacks.
class ShellActionModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ShellController")
public:
    enum Zone
    {
        All = -1,
        Toolbar,
        Left,
        Center,
        Right,
        More
    };
    Q_ENUM(Zone)
    enum Surface
    {
        Titlebar,
        Playerbar
    };
    enum Role
    {
        KeyRole = Qt::UserRole + 1,
        IdRole,
        TitleRole,
        IconRole,
        EnabledRole,
        BusyRole,
        PreferredRole,
        EffectiveRole,
        BadgeRole,
        BadgeVisibleRole,
        ActiveRole,
        BadgeSupportedRole,
        BadgePreferredRole,
        TooltipRole,
        BadgeTitleRole,
        HoverRole,
        PlaybackPageOnlyRole
    };
    struct Handle
    {
        QString key;
        quint64 generation = 0;
    };
    struct Contribution
    {
        QString key, id, title, icon;
        QString tooltip, badgeTitle;
        bool hover = false;
        Zone defaultZone = More;
        int order = 1000;
        bool visible = true, enabled = false, badgeDefaultVisible = true;
        bool supportsBadge = false;
        bool playbackPageOnly = false;
        std::function<void()> invoke;
    };
    explicit ShellActionModel(EchoMusic::SettingsStore &preferences, Surface surface,
                              QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    // Trusted host registration only. Plugin scope/key validation belongs to the
    // plugin host; QML cannot install callbacks or claim another owner's action.
    Handle registerAction(QObject &owner, Contribution contribution);
    bool unregisterAction(const Handle &handle);
    bool setPresentation(const Handle &handle, const QString &title, const QString &icon);
    bool setInteraction(const Handle &, const QString &tooltip, const QString &badgeTitle,
                        bool hover);
    bool setState(const Handle &handle, bool enabled, bool visible = true, bool busy = false,
                  const QString &badge = {}, bool active = false);
    Q_INVOKABLE bool activate(const QString &key);
    Q_INVOKABLE bool setPlacement(const QString &key, Zone zone);
    Q_INVOKABLE bool setBadgeVisible(const QString &key, bool visible);
    Q_INVOKABLE bool shiftAction(const QString &key, int offset, Zone projection = All);
    Q_INVOKABLE bool moveBefore(const QString &key, const QString &beforeKey,
                                Zone projection = All);
    Q_INVOKABLE bool moveAfter(const QString &key, const QString &afterKey, Zone projection = All);
    Q_INVOKABLE bool moveInPlayerZone(const QString &key, Zone zone,
                                      const QString &relativeKey = {}, bool after = false);
    Q_INVOKABLE bool shiftInPlayerZone(const QString &key, int offset);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE void setCapacity(Zone zone, int capacity);
    /// Validate the whole reordered region before replacing its existing slots.
    /// Other regions and preferences for inactive plugins remain untouched.
    bool reorder(const QStringList &keys);
signals:
    void layoutChanged();

private:
    struct Entry
    {
        Contribution action;
        QPointer<QObject> owner;
        quint64 generation;
        bool busy = false, active = false;
        QString badge;
        Zone effective = More;
        QMetaObject::Connection ownerConnection;
    };
    int rowFor(const QString &key) const;
    Zone preferred(const Entry &) const;
    bool validZone(Zone zone) const;
    QStringList projectedKeys(Zone projection, bool usePreferred = false) const;
    void resolve();
    void applyOrder();
    void persist();
    EchoMusic::SettingsStore &m_preferences;
    Surface m_surface;
    QString m_prefix;
    QList<Entry> m_entries;
    QStringList m_order;
    QVariantMap m_placements, m_badges;
    QHash<int, int> m_capacity;
    quint64 m_generation = 0;
};

/// Projection of one registry with optional transient drag order. Native moves keep delegates
/// alive; QSortFilterProxyModel translates source moves to layoutChanged instead.
class ShellActionProjection : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        EchoMusic::ShellActionModel *actions READ actions WRITE setActions NOTIFY actionsChanged)
    Q_PROPERTY(EchoMusic::ShellActionModel::Zone zone READ zone WRITE setZone NOTIFY zoneChanged)
    Q_PROPERTY(bool usePreferredPlacement READ usePreferredPlacement WRITE setUsePreferredPlacement
                   NOTIFY usePreferredPlacementChanged)
    Q_PROPERTY(bool includePlaybackPageActions READ includePlaybackPageActions WRITE
                   setIncludePlaybackPageActions NOTIFY includePlaybackPageActionsChanged)
public:
    explicit ShellActionProjection(QObject *parent = nullptr);
    ShellActionModel *actions() const
    {
        return m_actions;
    }
    void setActions(ShellActionModel *actions);
    ShellActionModel::Zone zone() const
    {
        return m_zone;
    }
    void setZone(ShellActionModel::Zone zone);
    bool usePreferredPlacement() const
    {
        return m_preferred;
    }
    void setUsePreferredPlacement(bool value);
    bool includePlaybackPageActions() const
    {
        return m_includePlaybackPageActions;
    }
    void setIncludePlaybackPageActions(bool value);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    // A drag owns a temporary view order. Registry/preferences change only once
    // on release; cancellation or a changed catalog restores the source order.
    Q_INVOKABLE bool beginReorder(const QString &key);
    Q_INVOKABLE void previewReorder(int target);
    Q_INVOKABLE void finishReorder(bool commit);
signals:
    void actionsChanged();
    void zoneChanged();
    void usePreferredPlacementChanged();
    void includePlaybackPageActionsChanged();

private:
    void reconcile();
    void removeSourceRows(int first, int last);
    void sourceDataChanged(const QModelIndex &first, const QModelIndex &last,
                           const QList<int> &roles);
    QPointer<ShellActionModel> m_actions;
    QList<QPersistentModelIndex> m_rows;
    QPersistentModelIndex m_reorderSource;
    QList<QPersistentModelIndex> m_reorderStart, m_reorderPreview;
    ShellActionModel::Zone m_zone = ShellActionModel::All;
    bool m_preferred = false;
    bool m_includePlaybackPageActions = true;
};
} // namespace EchoMusic
