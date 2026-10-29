#pragma once
#include <QAbstractListModel>
#include "SettingsStore.h"
#include <QPointer>
#include <functional>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class LibraryController;
class PlaybackService;
class ApiClient;
class UserPlaylistModel;
class RouteController;
/// Application-owned menu layout. The finite built-in catalog is separate from
/// the virtual playlist model; visibility/order survives restart, never changes
/// API identities or creates a second writable QML preference store.
class SidebarLayoutModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("LibraryController.h")
    Q_MOC_INCLUDE("RouteController.h")
    QML_UNCREATABLE("Owned by ShellSettings")
    Q_PROPERTY(EchoMusic::LibraryController *libraryController READ libraryController WRITE
                   setLibraryController NOTIFY changed)
    Q_PROPERTY(bool playlistAreaVisible READ playlistAreaVisible NOTIFY changed)
    Q_PROPERTY(bool menuVisible READ menuVisible NOTIFY changed)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *sectionModel READ sectionModel CONSTANT)
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *shortcutModel READ shortcutModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *navigationModel READ navigationModel CONSTANT)
    Q_PROPERTY(QVariantList shortcutCatalog READ shortcutCatalog NOTIFY changed)
    Q_PROPERTY(QVariantList frequentArtists READ frequentArtists NOTIFY changed)
    Q_PROPERTY(QVariantList recentPlaylists READ recentPlaylists NOTIFY changed)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY changed)
    Q_PROPERTY(QString historyError READ historyError NOTIFY changed)
public:
    ~SidebarLayoutModel() override;
    /// Borrow the canonical playback/library owners; retain only bounded display projections.
    void attachResources(ApiClient &, PlaybackService &, UserPlaylistModel &, bool networkEnabled);
    QVariantList frequentArtists() const;
    QVariantList recentPlaylists() const;
    bool historyLoading() const
    {
        return m_historyLoading;
    }
    QString historyError() const
    {
        return m_historyError;
    }
    Q_INVOKABLE void browseResources(bool active);
    Q_INVOKABLE bool openShortcut(RouteController *router, const QString &key);
    QString resourceIdForKey(const QString &key) const;
    QString homeResourceId() const;
    enum Role
    {
        Key = Qt::UserRole + 1,
        Title,
        Glyph,
        Group,
        Hidden,
        Pinned
    };
    explicit SidebarLayoutModel(EchoMusic::SettingsStore &preferences, QObject *parent);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    LibraryController *libraryController() const;
    void setLibraryController(LibraryController *controller);
    bool playlistAreaVisible() const;
    bool menuVisible() const;
    QVariantList sections() const;
    // Stable, read-only editor projection. Visibility updates must not reset
    // the popup's virtual view or destroy the button handling the click.
    QAbstractItemModel *sectionModel();
    QVariantList shortcuts() const;
    QAbstractItemModel *shortcutModel() const
    {
        return m_shortcuts;
    }
    QVariantList shortcutCatalog() const;
    struct PluginEntry
    {
        QString key;
        QVariantMap values;
        bool shortcut = false;
        QPointer<QObject> owner;
        std::function<bool()> visible, enabled;
        std::function<void()> invoke;
        quint64 generation = 0;
    };
    quint64 registerPluginEntry(PluginEntry entry);
    void unregisterPluginEntry(const QString &key, quint64 generation);
    void refreshPlugins();
    QAbstractItemModel *navigationModel() const
    {
        return m_navigation;
    }
    Q_INVOKABLE bool isPlugin(const QString &key) const;
    Q_INVOKABLE bool entryEnabled(const QString &key) const;
    Q_INVOKABLE bool activate(const QString &key);
    Q_INVOKABLE void toggleCollapsed(const QString &section);
    /// Restored first card, including preference-key to application-route mapping.
    QString homeRoute() const;
    /// Transient reorder belongs to this model. Only a successful drag commits
    /// preferences; cancellation restores the original order without disk writes.
    Q_INVOKABLE bool beginShortcutDrag(const QString &key);
    Q_INVOKABLE void previewShortcutMove(const QString &key, const QString &target);
    Q_INVOKABLE void endShortcutDrag(bool commit);
    Q_INVOKABLE void toggleHidden(const QString &key);
    Q_INVOKABLE void toggleGroup(const QString &key);
    Q_INVOKABLE void toggleShortcut(const QString &key);
    Q_INVOKABLE void moveShortcutBefore(const QString &key, const QString &before,
                                        bool after = false);
    Q_INVOKABLE void moveBefore(const QString &key, const QString &before, bool after = false);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE void restoreShortcuts();
signals:
    void changed();

private:
    struct Entry
    {
        QString key, title, glyph, group;
    };
    QVariantMap describe(const Entry &entry) const;
    QVariantMap describePlugin(const PluginEntry &) const;
    QVariantList pluginEntries(bool shortcut) const;
    void addPluginSections(QVariantList &) const;
    void refreshSectionModel();
    void save();
    void acceptHistory(const QJsonArray &entries);
    QVariantMap resourceEntry(const QString &key) const;
    QVariantMap resolveResource(QVariantMap entry) const;
    void updatePortraits();
    void cancelPortraits();
    void saveResources();
    QPointer<ApiClient> m_resourceApi;
    QPointer<PlaybackService> m_playback;
    QPointer<UserPlaylistModel> m_resourceLibrary;
    QVariantList m_frequentArtists;
    QHash<QString, QVariantMap> m_resources;
    QHash<quint64, QString> m_portraitRequests;
    QSet<QString> m_portraitAttempts;
    bool m_browsing = false, m_resourceNetwork = false, m_historyLoading = false;
    QString m_historyError;
    EchoMusic::SettingsStore &m_preferences;
    QPointer<LibraryController> m_library;
    void applyPlaylistVisibility();
    QAbstractItemModel *m_shortcuts = nullptr;
    QAbstractItemModel *m_sections = nullptr, *m_menuItems = nullptr, *m_playlistItems = nullptr;
    QList<Entry> m_entries, m_shortcutCatalog;
    QStringList m_hidden, m_hiddenGroups, m_pinned, m_dragOriginal;
    QString m_dragKey;
    QHash<QString, PluginEntry> m_plugins;
    QHash<QString, QAbstractItemModel *> m_pluginSectionModels;
    QStringList m_pluginOrder, m_collapsedGroups;
    QAbstractItemModel *m_navigation = nullptr;
    quint64 m_pluginGeneration = 0;
    bool m_pluginRefreshPending = false;
};
} // namespace EchoMusic
