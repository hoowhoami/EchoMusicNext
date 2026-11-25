#pragma once
#include <QAbstractListModel>
#include <QFont>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Read-only presentation projection. Keeps source indexes/typed values stable
/// during filtering; selection and application commands stay with the caller.
class EchoSelectModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariant source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString textRole READ textRole WRITE setTextRole NOTIFY sourceChanged)
    Q_PROPERTY(QString valueRole READ valueRole WRITE setValueRole NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList available READ available WRITE setAvailable NOTIFY sourceChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(qreal labelWidth READ labelWidth NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
public:
    explicit EchoSelectModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant source() const
    {
        return m_source;
    }
    QString textRole() const
    {
        return m_textRole;
    }
    QString valueRole() const
    {
        return m_valueRole;
    }
    QVariantList available() const
    {
        return m_available;
    }
    QString query() const
    {
        return m_query;
    }
    QFont font() const
    {
        return m_font;
    }
    int count() const
    {
        return m_visible.size();
    }
    int totalCount() const
    {
        return m_entries.size();
    }
    int currentIndex() const
    {
        return m_currentIndex;
    }
    Q_INVOKABLE void setCurrentIndex(int index)
    {
        if (m_currentIndex != index) {
            m_currentIndex = index;
            emit currentIndexChanged();
        }
    }
    Q_INVOKABLE QVariantMap optionAtSource(int index) const;
    Q_INVOKABLE int filteredIndex(int sourceIndex) const;
    qreal labelWidth() const
    {
        return m_labelWidth;
    }
    void setSource(const QVariant &source);
    void setTextRole(const QString &role);
    void setValueRole(const QString &role);
    void setAvailable(const QVariantList &values);
    void setQuery(const QString &query);
    void setFont(const QFont &font);
    Q_INVOKABLE QVariantMap option(int filteredIndex) const;
    Q_INVOKABLE QVariantMap optionForValue(const QVariant &value) const;
    Q_INVOKABLE int nextEnabled(int current, int direction) const;
signals:
    void currentIndexChanged();
    void sourceChanged();
    void queryChanged();
    void changed();

private:
    struct Entry
    {
        QString label;
        QVariant value;
        bool disabled;
        int sourceIndex;
    };
    void rebuild();
    void filter();
    QVariantMap map(const Entry &entry) const;
    QVariant m_source;
    QString m_textRole, m_valueRole, m_query;
    QVariantList m_available;
    QFont m_font;
    QList<Entry> m_entries;
    QList<int> m_visible;
    QList<QMetaObject::Connection> m_connections;
    QPointer<QAbstractItemModel> m_model;
    qreal m_labelWidth = 0;
    int m_currentIndex = -1;
};
} // namespace EchoMusic
