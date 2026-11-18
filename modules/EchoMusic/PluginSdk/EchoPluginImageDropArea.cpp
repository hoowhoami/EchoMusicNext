#include "EchoPluginImageDropArea.h"
#include "EchoPluginAssets.h"
#include "EchoPluginContext.h"
#include "EchoUiPresentation.h"
#include <QDragEnterEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <utility>

namespace EchoMusic
{
EchoPluginImageDropArea::EchoPluginImageDropArea(QQuickItem *parent) : QQuickItem(parent)
{
    auto *observer = new EchoUiPresentation(this, this);
    connect(observer, &EchoUiPresentation::changed, this, &EchoPluginImageDropArea::refresh);
    connect(this, &QQuickItem::enabledChanged, this, &EchoPluginImageDropArea::refresh);
    refresh();
}
EchoPluginImageDropArea::~EchoPluginImageDropArea()
{
    if (m_assets)
        m_assets->m_dropAreas.removeAll(this);
}
EchoPluginAssets *EchoPluginImageDropArea::assets() const
{
    return m_assets;
}
void EchoPluginImageDropArea::setAssets(EchoPluginAssets *value)
{
    if (m_assets == value)
        return;
    for (const auto &connection : std::exchange(m_connections, {}))
        QObject::disconnect(connection);
    if (m_assets)
        m_assets->m_dropAreas.removeAll(this);
    m_assets = value;
    if (value) {
        value->m_dropAreas.append(this);
        m_connections.append(connect(value, &QObject::destroyed, &m_assetConnections, [this] {
            m_assets = nullptr;
            const QPointer<EchoPluginImageDropArea> self(this);
            refresh();
            if (self)
                emit assetsChanged();
        }));
        m_connections.append(
            connect(value, &EchoPluginAssets::changed, &m_assetConnections, [this] { refresh(); }));
        m_connections.append(connect(&value->m_scope, &EchoPluginContext::activeChanged,
                                     &m_assetConnections, [this] { refresh(); }));
    }
    const QPointer<EchoPluginImageDropArea> self(this);
    refresh();
    if (self)
        emit assetsChanged();
}
void EchoPluginImageDropArea::setCollection(const QString &value)
{
    if (m_collection == value)
        return;
    m_collection = value;
    const QPointer<EchoPluginImageDropArea> self(this);
    refresh();
    if (self)
        emit configurationChanged();
}
void EchoPluginImageDropArea::setMaxCount(int value)
{
    if (m_maxCount == value)
        return;
    m_maxCount = value;
    const QPointer<EchoPluginImageDropArea> self(this);
    refresh();
    if (self)
        emit configurationChanged();
}
void EchoPluginImageDropArea::setArchiveJsonPaths(const QStringList &value)
{
    if (m_archiveJsonPaths == value)
        return;
    m_archiveJsonPaths = value;
    const QPointer<EchoPluginImageDropArea> self(this);
    refresh();
    if (self)
        emit configurationChanged();
}
void EchoPluginImageDropArea::setActive(bool value)
{
    if (std::exchange(m_active, value) == value)
        return;
    const QPointer<EchoPluginImageDropArea> self(this);
    refresh();
    if (self)
        emit activeChanged();
}
bool EchoPluginImageDropArea::accepting() const
{
    static const QRegularExpression collection(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return m_active && isEnabled() && m_assets && !m_assets->m_closed &&
           m_assets->m_scope.active() && EchoUiPresentation::presented(this) &&
           collection.match(m_collection).hasMatch() && m_maxCount >= 0 && m_maxCount <= 16;
}
bool EchoPluginImageDropArea::accepts(const QMimeData *mime, bool &archive) const
{
    archive = false;
    if (!accepting() || !mime || !mime->hasUrls())
        return false;
    const auto urls = mime->urls();
    if (urls.isEmpty() || urls.size() > 128)
        return false;
    for (const auto &url : urls) {
        const auto local = url.toLocalFile();
        if (!url.isLocalFile() || local.isEmpty() || local.size() > 8192)
            return false;
        if (local.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
            archive = true;
    }
    if (archive)
        return urls.size() == 1 && m_archiveJsonPaths.size() <= 4;
    return m_maxCount > 0;
}
void EchoPluginImageDropArea::setContainsDrag(bool value)
{
    if (std::exchange(m_containsDrag, value) != value)
        emit containsDragChanged();
}
void EchoPluginImageDropArea::refresh()
{
    const bool allowed = accepting();
    setFlag(ItemAcceptsDrops, allowed);
    if (!allowed)
        setContainsDrag(false);
}
void EchoPluginImageDropArea::dragEnterEvent(QDragEnterEvent *event)
{
    bool archive;
    const bool allowed = accepts(event->mimeData(), archive);
    setContainsDrag(allowed);
    if (allowed) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else
        event->ignore();
}
void EchoPluginImageDropArea::dragMoveEvent(QDragMoveEvent *event)
{
    bool archive;
    const bool allowed = accepts(event->mimeData(), archive);
    setContainsDrag(allowed);
    if (allowed) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else
        event->ignore();
}
void EchoPluginImageDropArea::dragLeaveEvent(QDragLeaveEvent *event)
{
    setContainsDrag(false);
    event->accept();
}
void EchoPluginImageDropArea::dropEvent(QDropEvent *event)
{
    const QPointer<EchoPluginImageDropArea> self(this);
    bool archive;
    const bool allowed = accepts(event->mimeData(), archive);
    const QPointer<EchoPluginAssets> assets(m_assets);
    const auto urls = event->mimeData()->urls();
    const auto collection = m_collection;
    const auto maxCount = m_maxCount;
    const auto paths = m_archiveJsonPaths;
    setContainsDrag(false);
    if (!self || !allowed || !assets || assets != m_assets || !accepting()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    if (archive) {
        auto *lease = assets->openDroppedImageArchive(paths, urls.first());
        if (self && lease)
            emit archiveDropped(lease);
    } else {
        auto *operation = assets->importDroppedImages(collection, urls, maxCount);
        if (self && operation)
            emit importStarted(operation);
    }
}
} // namespace EchoMusic
