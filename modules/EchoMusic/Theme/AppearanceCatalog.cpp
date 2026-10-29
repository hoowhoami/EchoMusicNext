#include "AppearanceCatalog.h"
#include "SettingsKeys.h"
#include <algorithm>
#include <QTimer>
namespace EchoMusic
{
AppearanceCatalog::AppearanceCatalog(EchoMusic::SettingsStore &s, QObject *p)
    : QObject(p), m_settings(s),
      m_selectedTheme(s.value(SettingsKeys::Appearance::ExtensionTheme).toString()),
      m_selectedSkin(s.value(SettingsKeys::Appearance::ExtensionSkin, "cover").toString()),
      m_themes(*this, "theme"), m_skins(*this, "skin")
{
}
QList<AppearanceEntry *> AppearanceCatalog::entries(const QString &kind) const
{
    QList<AppearanceEntry *> result;
    for (auto e : m_entries)
        if (e && !e->retired && e->kind == kind)
            result << e;
    std::sort(result.begin(), result.end(), [](auto a, auto b) { return a->key < b->key; });
    return result;
}
AppearanceEntry *AppearanceCatalog::find(const QString &key) const
{
    return m_entries.value(key);
}
AppearanceEntry *AppearanceCatalog::theme() const
{
    auto e = find(m_preview.isEmpty() ? m_selectedTheme : m_preview);
    return e && e->kind == "theme" && e->error.isEmpty() ? e : nullptr;
}
AppearanceEntry *AppearanceCatalog::skin() const
{
    auto e = find(m_selectedSkin);
    return e && e->kind == "skin" && e->error.isEmpty() ? e : nullptr;
}
void AppearanceCatalog::invalidatePresentations()
{
    if (std::exchange(m_invalidationPending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_invalidationPending = false;
        cancelPreview();
        emit presentationsInvalidated();
    });
}
AppearanceListModel *AppearanceCatalog::themeGroup(const QString &type)
{
    if (!QStringList{"default", "solid", "dynamic"}.contains(type))
        return nullptr;
    const auto name = "group." + type;
    if (auto *existing = findChild<AppearanceListModel *>(name, Qt::FindDirectChildrenOnly))
        return existing;
    auto *model = new AppearanceListModel(*this, "theme");
    model->setObjectName(name);
    model->setTypeFilter(type);
    return model;
}
void AppearanceCatalog::notify(bool theme)
{
    m_themes.reload();
    m_skins.reload();
    for (auto *model : findChildren<AppearanceListModel *>(Qt::FindDirectChildrenOnly))
        if (model != &m_themes && model != &m_skins)
            model->reload();
    emit changed();
    if (theme)
        emit themeChanged();
}
bool AppearanceCatalog::insert(AppearanceEntry *entry)
{
    if (!entry || entry->key.isEmpty() || m_entries.size() >= 512 || m_entries.contains(entry->key))
        return false;
    entry->setParent(this);
    entry->configuration()->publish();
    m_entries.insert(entry->key, entry);
    connect(entry->configuration(), &AppearanceConfiguration::changed, this, [this, entry] {
        if (!entry->retired) {
            emit entry->changed();
            // Configuration does not change immutable catalog metadata. Keep
            // delegates alive while a slider writes, and resolve only the live theme.
            if (theme() == entry)
                emit themeChanged();
        }
    });
    notify(entry->kind == "theme");
    return true;
}
void AppearanceCatalog::remove(const QString &token)
{
    for (auto e : m_entries)
        if (e && e->token == token) {
            const bool theme = e->kind == "theme";
            m_entries.remove(e->key);
            e->retired = true;
            e->factory = {};
            e->resolver = {};
            e->configuration()->retire();
            emit e->changed();
            if (e->kind == "skin" && m_selectedSkin == e->key) {
                m_selectedSkin = "cover";
                m_settings.setValue(SettingsKeys::Appearance::ExtensionSkin, "cover");
            }
            if (m_preview == e->key)
                m_preview.clear();
            notify(theme);
            if (e->configuration()->busy())
                connect(e->configuration(), &AppearanceConfiguration::drained, e,
                        &QObject::deleteLater);
            else
                e->deleteLater();
            return;
        }
}
void AppearanceCatalog::removeInstallation(const QString &installation)
{
    const auto copy = m_entries.values();
    for (auto e : copy)
        if (e && e->installation == installation)
            remove(e->token);
    if (m_selectedTheme.startsWith(installation + ":"))
        clearTheme();
    if (m_selectedSkin.startsWith(installation + ":"))
        chooseSkin("cover");
}
bool AppearanceCatalog::chooseTheme(const QString &key)
{
    auto e = find(key);
    if (!e || e->kind != "theme")
        return false;
    e->error.clear();
    m_preview.clear();
    m_selectedTheme = key;
    m_settings.setValue(SettingsKeys::Appearance::ExtensionTheme, key);
    notify(true);
    return true;
}
bool AppearanceCatalog::previewTheme(const QString &key)
{
    auto e = find(key);
    if (!e || e->kind != "theme" || !e->error.isEmpty())
        return false;
    m_preview = key;
    notify(true);
    return true;
}
void AppearanceCatalog::cancelPreview()
{
    if (m_preview.isEmpty())
        return;
    m_preview.clear();
    notify(true);
}
void AppearanceCatalog::clearTheme()
{
    m_preview.clear();
    m_selectedTheme.clear();
    m_settings.remove(SettingsKeys::Appearance::ExtensionTheme);
    notify(true);
}
bool AppearanceCatalog::chooseSkin(const QString &key)
{
    auto e = find(key);
    if (key != "cover" && key != "portrait" && (!e || e->kind != "skin"))
        return false;
    if (e)
        e->error.clear();
    m_selectedSkin = key;
    m_settings.setValue(SettingsKeys::Appearance::ExtensionSkin, key);
    notify(false);
    return true;
}
void AppearanceCatalog::fail(AppearanceEntry *e, const QString &message)
{
    if (!e || e->retired || !e->error.isEmpty())
        return;
    e->error = message;
    if (m_preview == e->key)
        m_preview.clear();
    if (e->kind == "skin" && m_selectedSkin == e->key) {
        m_selectedSkin = "cover";
        m_settings.setValue(SettingsKeys::Appearance::ExtensionSkin, "cover");
    }
    emit e->changed();
    notify(e->kind == "theme");
    emit contributionFailed(e->key, message);
}
void AppearanceCatalog::request(const QString &action)
{
    if (QStringList{"themes", "playback", "closePlayback", "skins", "settings", "source",
                    "comments", "barrage"}
            .contains(action))
        emit requestRaised(action);
}
QVariantMap AppearanceCatalog::resolveTheme(const QVariantMap &input)
{
    auto e = theme();
    if (!e)
        return {};
    auto values = e->definition.value(input.value("dark").toBool() ? "dark" : "light").toMap();
    if (e->resolver) {
        QString error;
        auto state = input;
        state.insert("settings", e->configuration()->values());
        const auto extra = e->resolver(state, error);
        if (!error.isEmpty()) {
            fail(e, error);
            return {};
        }
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            auto merged = values.value(it.key()).toMap();
            const auto patch = it.value().toMap();
            for (auto field = patch.begin(); field != patch.end(); ++field)
                merged.insert(field.key(), field.value());
            values.insert(it.key(), merged);
        }
    }
    return values;
}
AppearanceListModel::AppearanceListModel(AppearanceCatalog &c, QString k)
    : QAbstractListModel(&c), m_catalog(c), m_kind(std::move(k))
{
}
int AppearanceListModel::rowCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : filtered().size() + builtinCount();
}
QHash<int, QByteArray> AppearanceListModel::roleNames() const
{
    return {{Qt::UserRole + 1, "entryKey"},    {Qt::UserRole + 2, "title"},
            {Qt::UserRole + 3, "description"}, {Qt::UserRole + 4, "preview"},
            {Qt::UserRole + 5, "selected"},    {Qt::UserRole + 6, "entry"},
            {Qt::UserRole + 7, "variants"}};
}
QVariant AppearanceListModel::data(const QModelIndex &i, int r) const
{
    const auto all = filtered();
    if (!i.isValid() || i.row() < 0 || i.row() >= all.size() + builtinCount())
        return {};
    if (i.row() < builtinCount()) {
        switch (r - Qt::UserRole) {
        case 1:
            return m_kind == "theme" ? "echo" : i.row() == 0 ? "cover" : "portrait";
        case 2:
            return m_kind == "theme" ? "Echo"
                   : i.row() == 0    ? QString::fromUtf8("封面歌词")
                                     : QString::fromUtf8("写真");
        case 3:
            return m_kind == "theme" ? QString::fromUtf8("简约清爽，让音乐成为主角。") : QString();
        case 4:
            return QUrl();
        case 5:
            return m_kind == "theme"
                       ? m_catalog.selectedTheme().isEmpty()
                       : m_catalog.selectedSkin() == (i.row() == 0 ? "cover" : "portrait");
        case 6:
            return QVariant::fromValue<AppearanceEntry *>(nullptr);
        default:
            return {};
        }
    }
    auto e = all[i.row() - builtinCount()];
    switch (r - Qt::UserRole) {
    case 1:
        return e->key;
    case 2:
        return e->title;
    case 3:
        return e->description;
    case 4:
        return e->preview;
    case 5:
        return m_kind == "theme" ? m_catalog.selectedTheme() == e->key
                                 : m_catalog.selectedSkin() == e->key;
    case 6:
        return QVariant::fromValue(e);
    case 7:
        return e->definition;
    default:
        return {};
    }
}
QList<AppearanceEntry *> AppearanceListModel::filtered() const
{
    auto result = m_catalog.entries(m_kind);
    if (!m_typeFilter.isEmpty())
        result.removeIf([this](auto *e) { return e->type != m_typeFilter; });
    return result;
}
int AppearanceListModel::builtinCount() const
{
    return m_typeFilter.isEmpty() || m_typeFilter == "default" ? (m_kind == "skin" ? 2 : 1) : 0;
}
void AppearanceListModel::setTypeFilter(const QString &value)
{
    if (m_typeFilter == value || !QStringList{"", "default", "solid", "dynamic"}.contains(value))
        return;
    m_typeFilter = value;
    reload();
    emit typeFilterChanged();
}
void AppearanceListModel::reload()
{
    beginResetModel();
    endResetModel();
    emit countChanged();
}
} // namespace EchoMusic
