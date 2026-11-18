#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include "EchoPluginAssets.h"
class QMimeData;

namespace EchoMusic
{
class EchoPluginAssets;
class EchoPluginOperation;
class EchoPluginAssetArchive;
/// Only native user drops enter the existing scoped, managed asset import path.
class EchoPluginImageDropArea : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ImageDropArea)
    Q_PROPERTY(EchoMusic::EchoPluginAssets *assets READ assets WRITE setAssets NOTIFY assetsChanged)
    Q_PROPERTY(QString collection READ collection WRITE setCollection NOTIFY configurationChanged)
    Q_PROPERTY(int maxCount READ maxCount WRITE setMaxCount NOTIFY configurationChanged)
    Q_PROPERTY(QStringList archiveJsonPaths READ archiveJsonPaths WRITE setArchiveJsonPaths NOTIFY
                   configurationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool containsDrag READ containsDrag NOTIFY containsDragChanged)
public:
    explicit EchoPluginImageDropArea(QQuickItem *parent = nullptr);
    ~EchoPluginImageDropArea() override;
    EchoPluginAssets *assets() const;
    void setAssets(EchoPluginAssets *);
    QString collection() const
    {
        return m_collection;
    }
    void setCollection(const QString &);
    int maxCount() const
    {
        return m_maxCount;
    }
    void setMaxCount(int);
    QStringList archiveJsonPaths() const
    {
        return m_archiveJsonPaths;
    }
    void setArchiveJsonPaths(const QStringList &);
    bool active() const
    {
        return m_active;
    }
    void setActive(bool);
    bool containsDrag() const
    {
        return m_containsDrag;
    }
signals:
    void assetsChanged();
    void configurationChanged();
    void activeChanged();
    void containsDragChanged();
    void importStarted(EchoMusic::EchoPluginOperation *operation);
    void archiveDropped(EchoMusic::EchoPluginAssetArchive *archive);

protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    friend class EchoPluginAssets;
    bool accepting() const;
    bool accepts(const QMimeData *, bool &archive) const;
    void refresh();
    void setContainsDrag(bool);
    QPointer<EchoPluginAssets> m_assets;
    QObject m_assetConnections;
    QList<QMetaObject::Connection> m_connections;
    QString m_collection;
    QStringList m_archiveJsonPaths;
    int m_maxCount = 16;
    bool m_active = true, m_containsDrag = false;
};
} // namespace EchoMusic
