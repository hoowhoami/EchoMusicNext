#pragma once
#include <QAbstractListModel>
#include <QUrl>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiModelAdapter;
/// Ephemeral dialog navigation/filter; business preferences stay in their owners.
/// Entries without a functional provider remain disabled, never load a stub page.
class SettingsCatalog : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *uiExtensions READ uiExtensions WRITE setUiExtensions NOTIFY changed)
    Q_PROPERTY(QObject *extensionRecipe READ extensionRecipe NOTIFY changed)
    Q_PROPERTY(QVariantMap extensionEntry READ extensionEntry NOTIFY changed)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
    Q_PROPERTY(QString current READ current NOTIFY changed)
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
    Q_PROPERTY(
        bool playbackAvailable READ playbackAvailable WRITE setPlaybackAvailable NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    enum Role
    {
        Key = Qt::UserRole + 1,
        Label,
        Glyph,
        Available
    };
    explicit SettingsCatalog(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QObject *uiExtensions() const;
    QObject *extensionRecipe() const;
    QVariantMap extensionEntry() const;
    void setUiExtensions(QObject *);
    QString filter() const
    {
        return m_filter;
    }
    QString current() const
    {
        return m_current;
    }
    QUrl source() const;
    int count() const
    {
        return int(m_visible.size());
    }
    bool playbackAvailable() const
    {
        return m_playbackAvailable;
    }
    void setPlaybackAvailable(bool value);
    void setFilter(const QString &filter);
    Q_INVOKABLE void select(const QString &key);
    Q_INVOKABLE void reset();
signals:
    void changed();

private:
    struct Entry
    {
        QString key, label, glyph, keywords, component;
        QString extension;
        bool enabled = true;
    };
    void rebuild(bool preserve, bool resetting = false);
    void refreshExtensions();
    int m_nativeCount = 0;
    QPointer<EchoUiModelAdapter> m_extensions;
    QList<QMetaObject::Connection> m_extensionConnections;
    QString m_lastNative = "interface";
    bool available(const Entry &entry) const;
    bool m_playbackAvailable = false;
    QList<Entry> m_entries;
    QList<int> m_visible;
    QString m_filter, m_current = QStringLiteral("interface");
};
} // namespace EchoMusic
