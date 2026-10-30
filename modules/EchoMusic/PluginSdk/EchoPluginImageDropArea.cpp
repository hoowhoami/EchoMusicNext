#include "EchoPluginImageDropArea.h"
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QQuickWindow>
#include <QRegularExpression>
#include <algorithm>

namespace EchoMusic
{
EchoPluginImageDropArea::EchoPluginImageDropArea(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemAcceptsDrops, true);
}
EchoPluginImageDropArea::~EchoPluginImageDropArea()
{
    blockSignals(true);
    retire();
}
void EchoPluginImageDropArea::setAssets(EchoPluginAssets *assets)
{
    if (m_assets == assets)
        return;
    const QPointer<EchoPluginImageDropArea> self(this);
    const QPointer<EchoPluginAssets> next(assets);
    retire();
    if (!self)
        return;
    m_assets = next;
    emit changed();
}
void EchoPluginImageDropArea::setCollection(const QString &collection)
{
    if (m_collection == collection)
        return;
    const QPointer<EchoPluginImageDropArea> self(this);
    retire();
    if (!self)
        return;
    m_collection = collection;
    emit changed();
}
void EchoPluginImageDropArea::setMaxCount(int maxCount)
{
    if (m_maxCount == maxCount)
        return;
    m_containsDrag = false;
    m_maxCount = maxCount;
    emit changed();
}
void EchoPluginImageDropArea::setArchiveJsonPaths(const QStringList &paths)
{
    if (m_archiveJsonPaths == paths)
        return;
    const QPointer<EchoPluginImageDropArea> self(this);
    retire();
    if (!self)
        return;
    m_archiveJsonPaths = paths;
    emit changed();
}
void EchoPluginImageDropArea::hover(bool value)
{
    if (m_containsDrag == value)
        return;
    m_containsDrag = value;
    emit changed();
}
void EchoPluginImageDropArea::retire()
{
    const QPointer<EchoPluginImageDropArea> self(this);
    const bool wasBusy = busy();
    const auto operation = m_operation;
    m_operation = nullptr;
    const auto archive = m_archive;
    m_archive = nullptr;
    if (operation)
        disconnect(operation, nullptr, this, nullptr);
    if (archive)
        disconnect(archive, nullptr, this, nullptr);
    hover(false);
    if (operation && !operation->finished())
        operation->cancel();
    if (archive && archive->active())
        archive->dispose();
    if (wasBusy && self)
        emit self->changed();
}
bool EchoPluginImageDropArea::accepts(const QDropEvent *event) const
{
    static const QRegularExpression collection("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$");
    if (!m_assets || busy() || !isEnabled() || !isVisible() || !window() ||
        !window()->isVisible() || width() <= 0 || height() <= 0 || m_maxCount < 0 ||
        m_maxCount > 16 || !collection.match(m_collection).hasMatch() ||
        !(event->possibleActions() & Qt::CopyAction) || event->source() != nullptr ||
        !event->mimeData() || event->mimeData()->inherits("QQuickDragMimeData") ||
        !event->mimeData()->hasUrls())
        return false;
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || urls.size() > 128)
        return false;
    for (const auto &url : urls)
        if (!url.isLocalFile() || url.toLocalFile().isEmpty() || url.toLocalFile().size() > 8192)
            return false;
    return true;
}
void EchoPluginImageDropArea::dragEnterEvent(QDragEnterEvent *event)
{
    const bool accepted = accepts(event);
    hover(accepted);
    if (accepted) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}
void EchoPluginImageDropArea::dragMoveEvent(QDragMoveEvent *event)
{
    const bool accepted = accepts(event);
    hover(accepted);
    if (accepted) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}
void EchoPluginImageDropArea::dragLeaveEvent(QDragLeaveEvent *event)
{
    hover(false);
    event->accept();
}
void EchoPluginImageDropArea::dropEvent(QDropEvent *event)
{
    const bool accepted = accepts(event);
    if (!accepted) {
        hover(false);
        event->ignore();
        return;
    }
    // Publish hover/busy together after the native grant is captured. A QML
    // changed handler must not change the bound service between these steps.
    m_containsDrag = false;
    const QPointer<EchoPluginImageDropArea> self(this);
    const auto urls = event->mimeData()->urls();
    const auto zip = std::find_if(urls.cbegin(), urls.cend(), [](const QUrl &url) {
        return url.fileName().endsWith(".zip", Qt::CaseInsensitive);
    });
    if (!m_archiveJsonPaths.isEmpty() && zip != urls.cend()) {
        auto *archive = m_assets->openDroppedImageArchive(m_archiveJsonPaths, *zip);
        if (!archive) {
            event->ignore();
            emit changed();
            return;
        }
        if (m_operation)
            disconnect(m_operation, nullptr, this, nullptr);
        m_operation = nullptr;
        if (m_archive)
            disconnect(m_archive, nullptr, this, nullptr);
        m_archive = archive;
        connect(archive, &EchoPluginAssetArchive::changed, this, &EchoPluginImageDropArea::changed);
        connect(archive, &QObject::destroyed, this, &EchoPluginImageDropArea::changed);
        event->setDropAction(Qt::CopyAction);
        event->accept();
        emit changed();
        if (self)
            emit self->archiveDropped(archive);
        return;
    }
    auto *operation = m_assets->importDroppedImages(m_collection, urls, m_maxCount);
    if (m_operation)
        disconnect(m_operation, nullptr, this, nullptr);
    if (m_archive)
        disconnect(m_archive, nullptr, this, nullptr);
    m_archive = nullptr;
    m_operation = operation;
    connect(operation, &EchoPluginOperation::changed, this, &EchoPluginImageDropArea::changed);
    connect(operation, &QObject::destroyed, this, &EchoPluginImageDropArea::changed);
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit changed();
    // No code after this signal: a view may retire itself synchronously.
    if (self)
        emit self->importStarted(operation);
}
void EchoPluginImageDropArea::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemVisibleHasChanged && !data.boolValue)
        retire();
    else if (change == ItemSceneChange) {
        retire();
        disconnect(m_windowVisible);
        if (data.window)
            m_windowVisible =
                connect(data.window, &QWindow::visibleChanged, this, [this](bool visible) {
                    if (!visible)
                        retire();
                });
    }
}
} // namespace EchoMusic
