#pragma once
#include "EchoPluginAssets.h"
#include <QQuickItem>

namespace EchoMusic
{
/// Only a native external drop may grant new input paths. There is deliberately
/// no invokable taking URLs, filenames, MIME data or a generic source object.
class EchoPluginImageDropArea : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EchoMusic::EchoPluginAssets *assets READ assets WRITE setAssets NOTIFY changed)
    Q_PROPERTY(QString collection READ collection WRITE setCollection NOTIFY changed)
    Q_PROPERTY(int maxCount READ maxCount WRITE setMaxCount NOTIFY changed)
    Q_PROPERTY(
        QStringList archiveJsonPaths READ archiveJsonPaths WRITE setArchiveJsonPaths NOTIFY changed)
    Q_PROPERTY(bool containsDrag READ containsDrag NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit EchoPluginImageDropArea(QQuickItem *parent = nullptr);
    ~EchoPluginImageDropArea() override;
    EchoPluginAssets *assets() const
    {
        return m_assets;
    }
    void setAssets(EchoPluginAssets *assets);
    QString collection() const
    {
        return m_collection;
    }
    void setCollection(const QString &collection);
    int maxCount() const
    {
        return m_maxCount;
    }
    void setMaxCount(int maxCount);
    QStringList archiveJsonPaths() const
    {
        return m_archiveJsonPaths;
    }
    void setArchiveJsonPaths(const QStringList &paths);
    bool containsDrag() const
    {
        return m_containsDrag;
    }
    bool busy() const
    {
        return (m_operation && !m_operation->finished()) || (m_archive && m_archive->active());
    }
signals:
    void changed();
    void importStarted(EchoMusic::EchoPluginOperation *operation);
    void archiveDropped(EchoMusic::EchoPluginAssetArchive *archive);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;

private:
    bool accepts(const QDropEvent *event) const;
    void hover(bool value);
    void retire();
    QPointer<EchoPluginAssets> m_assets;
    QPointer<EchoPluginOperation> m_operation;
    QPointer<EchoPluginAssetArchive> m_archive;
    QMetaObject::Connection m_windowVisible;
    QString m_collection;
    QStringList m_archiveJsonPaths;
    int m_maxCount = 16;
    bool m_containsDrag = false;
};
} // namespace EchoMusic
