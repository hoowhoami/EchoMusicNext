#pragma once
#include "AppearanceConfiguration.h"
#include <QAbstractListModel>
#include <QPointer>
#include <QUrl>
#include <QSettings>
#include <functional>
namespace EchoMusic
{
class AppearanceCatalog;
class AppearanceEntry final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Registered appearance contribution")
    Q_PROPERTY(QString key READ publicKey CONSTANT)
    Q_PROPERTY(QString title READ publicTitle CONSTANT)
    Q_PROPERTY(QString description READ publicDescription CONSTANT)
    Q_PROPERTY(QString kind READ publicKind CONSTANT)
    Q_PROPERTY(QString type READ publicType CONSTANT)
    Q_PROPERTY(QString defaultMode READ publicDefaultMode CONSTANT)
    Q_PROPERTY(QUrl preview READ publicPreview CONSTANT)
    Q_PROPERTY(QString titlebar READ publicTitlebar CONSTANT)
    Q_PROPERTY(QString tools READ publicTools CONSTANT)
    Q_PROPERTY(EchoMusic::AppearanceConfiguration *configuration READ configuration CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    QString publicKey() const
    {
        return key;
    }
    QString publicTitle() const
    {
        return title;
    }
    QString publicDescription() const
    {
        return description;
    }
    QString publicKind() const
    {
        return kind;
    }
    QUrl publicPreview() const
    {
        return preview;
    }
    QString publicTitlebar() const
    {
        return titlebar;
    }
    QString publicTools() const
    {
        return tools;
    }
    explicit AppearanceEntry(QObject *parent) : QObject(parent) {}
    QString key, title, description, kind, token, installation, titlebar = "host", tools = "host";
    QString type = "default", defaultMode = "system";
    QString publicType() const
    {
        return type;
    }
    QString publicDefaultMode() const
    {
        return defaultMode;
    }
    QUrl preview;
    QVariantMap definition;
    QHash<QString, QUrl> components;
    std::function<QVariantMap(const QString &, QObject *)> factory;
    std::function<QVariantMap(const QVariantMap &, QString &)> resolver;
    AppearanceConfiguration *configuration() const
    {
        return config;
    }
    QString errorMessage() const
    {
        return error;
    }
    AppearanceConfiguration *config = nullptr;
    QString error;
    bool retired = false;
signals:
    void changed();
};
class AppearanceListModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    QML_ELEMENT
    QML_UNCREATABLE("Owned by appearance catalog")
public:
    AppearanceListModel(AppearanceCatalog &, QString kind);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const
    {
        return rowCount();
    }
    void reload();
    QString typeFilter() const
    {
        return m_typeFilter;
    }
    void setTypeFilter(const QString &value);
signals:
    void typeFilterChanged();
    void countChanged();

private:
    AppearanceCatalog &m_catalog;
    QString m_kind, m_typeFilter;
    QList<AppearanceEntry *> filtered() const;
    bool includesBuiltin() const;
};
/// Non-business contribution directory. ThemeStore alone resolves appearance;
/// SDK supplies scoped presentation factories, never private host QObjects.
class AppearanceCatalog final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ThemeStore")
    Q_PROPERTY(EchoMusic::AppearanceListModel *themes READ themes CONSTANT)
    Q_PROPERTY(EchoMusic::AppearanceListModel *skins READ skins CONSTANT)
    Q_PROPERTY(EchoMusic::AppearanceEntry *theme READ theme NOTIFY changed)
    Q_PROPERTY(EchoMusic::AppearanceEntry *skin READ skin NOTIFY changed)
    Q_PROPERTY(QString selectedTheme READ selectedTheme NOTIFY changed)
    Q_PROPERTY(QString selectedSkin READ selectedSkin NOTIFY changed)
    Q_PROPERTY(bool previewing READ previewing NOTIFY changed)
public:
    explicit AppearanceCatalog(QSettings &, QObject *parent);
    AppearanceListModel *themes()
    {
        return &m_themes;
    }
    AppearanceListModel *skins()
    {
        return &m_skins;
    }
    QList<AppearanceEntry *> entries(const QString &kind) const;
    AppearanceEntry *find(const QString &) const;
    AppearanceEntry *theme() const;
    AppearanceEntry *skin() const;
    QString selectedTheme() const
    {
        return m_selectedTheme;
    }
    QString selectedSkin() const
    {
        return m_selectedSkin;
    }
    bool previewing() const
    {
        return !m_preview.isEmpty();
    }
    bool insert(AppearanceEntry *);
    void remove(const QString &token);
    void removeInstallation(const QString &installation);
    QSettings &settings()
    {
        return m_settings;
    }
    void invalidatePresentations();
    Q_INVOKABLE EchoMusic::AppearanceListModel *themeGroup(const QString &type);
    Q_INVOKABLE bool chooseTheme(const QString &key);
    Q_INVOKABLE bool chooseSkin(const QString &key);
    Q_INVOKABLE bool previewTheme(const QString &key);
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE void clearTheme();
    Q_INVOKABLE void fail(EchoMusic::AppearanceEntry *, const QString &message);
    Q_INVOKABLE void request(const QString &action);
    QVariantMap resolveTheme(const QVariantMap &input);
signals:
    void presentationsInvalidated();
    void changed();
    void themeChanged();
    void requestRaised(const QString &action);
    void contributionFailed(const QString &key, const QString &message);

private:
    void notify(bool theme);
    QSettings &m_settings;
    QHash<QString, QPointer<AppearanceEntry>> m_entries;
    bool m_invalidationPending = false;
    QString m_selectedTheme, m_selectedSkin = "cover", m_preview;
    AppearanceListModel m_themes, m_skins;
};
} // namespace EchoMusic
