#include "CommentExtent.h"
#include <QFontMetricsF>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <cmath>
#include <string>

namespace EchoMusic
{
namespace
{
qreal textHeight(const QString &text, const QFont &font, qreal width, qreal lineHeight = 0)
{
    QTextDocument document;
    document.setDocumentMargin(0);
    document.setDefaultFont(font);
    auto option = document.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
    document.setPlainText(text);
    if (lineHeight > 0) {
        QTextCursor cursor(&document);
        cursor.select(QTextCursor::Document);
        QTextBlockFormat format;
        format.setLineHeight(lineHeight, QTextBlockFormat::FixedHeight);
        cursor.mergeBlockFormat(format);
    }
    document.setTextWidth(qMax(1., width));
    return document.size().height();
}
} // namespace
CommentExtent::CommentExtent(QObject *parent) : QObject(parent)
{
    m_reflow.setSingleShot(true);
    m_reflow.setInterval(40);
    connect(&m_reflow, &QTimer::timeout, this, &CommentExtent::invalidate);
}
void CommentExtent::setCompact(bool value)
{
    if (m_compact == value)
        return;
    m_compact = value;
    invalidate();
    emit configurationChanged();
}
void CommentExtent::setActive(bool value)
{
    if (m_active == value)
        return;
    m_active = value;
    if (!value)
        m_reflow.stop();
    else if (m_dirty)
        invalidate();
    emit configurationChanged();
}
QString CommentExtent::key(const QVariantMap &entry, const QString &kind, const QString &root)
{
    return kind + "/" + root + "/" + entry.value("id").toString();
}
void CommentExtent::setController(CommentsController *value)
{
    if (m_controller == value)
        return;
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    m_controller = value;
    if (value) {
        auto *model = value->model();
        m_connections.append(
            connect(model, &QAbstractItemModel::rowsInserted, this, &CommentExtent::schedule));
        m_connections.append(
            connect(model, &QAbstractItemModel::rowsRemoved, this, &CommentExtent::schedule));
        m_connections.append(
            connect(model, &QAbstractItemModel::modelReset, this, &CommentExtent::invalidate));
        m_connections.append(connect(
            model, &QAbstractItemModel::dataChanged, this,
            [this, model](const QModelIndex &first, const QModelIndex &last) {
                for (int row = first.row(); row <= last.row(); ++row) {
                    const auto index = model->index(row);
                    m_heights.remove(key(model->geometryEntry(row),
                                         model->data(index, CommentsModel::Kind).toString(),
                                         model->data(index, CommentsModel::RootId).toString()));
                }
                schedule();
            }));
        m_connections.append(
            connect(value, &CommentsController::scopeReset, this, &CommentExtent::invalidate));
    }
    invalidate();
    emit configurationChanged();
}
void CommentExtent::setLayoutWidth(qreal value)
{
    if (qFuzzyCompare(m_width, value))
        return;
    const bool initial = m_width <= 0;
    m_width = value;
    if (initial)
        invalidate();
    else {
        m_dirty = true;
        if (m_active)
            m_reflow.start();
    }
    emit configurationChanged();
}
void CommentExtent::setViewportWidth(qreal value)
{
    if (qFuzzyCompare(m_viewportWidth, value))
        return;
    const bool breakpointChanged = (m_viewportWidth <= 640) != (value <= 640);
    m_viewportWidth = value;
    // Width reflow already has a debounce. Only the CSS breakpoint adds a
    // distinct padding/gap change; ordinary resize ticks must not flush it.
    if (breakpointChanged)
        invalidate();
    emit configurationChanged();
}
void CommentExtent::setBodyFont(const QFont &value)
{
    if (m_bodyFont == value)
        return;
    m_bodyFont = value;
    invalidate();
    emit configurationChanged();
}
void CommentExtent::setTimeFont(const QFont &value)
{
    if (m_timeFont == value)
        return;
    m_timeFont = value;
    invalidate();
    emit configurationChanged();
}
void CommentExtent::setQuoteFont(const QFont &value)
{
    if (m_quoteFont == value)
        return;
    m_quoteFont = value;
    invalidate();
    emit configurationChanged();
}
void CommentExtent::invalidate()
{
    m_heights.clear();
    schedule();
}
void CommentExtent::schedule()
{
    m_dirty = true;
    if (m_pending || !m_active)
        return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        if (m_active) {
            m_dirty = false;
            sync();
        }
    });
}
qreal CommentExtent::estimate(const QVariantMap &entry, const QString &kind) const
{
    if (kind == "singerHeading")
        return m_compact ? 12 + 13 * 1.5 + 8 : 18 + 13 * 1.5 + 10;
    if (kind == "singerDivider")
        return m_compact ? 25 : 33;
    const qreal outer = m_compact ? 0 : 12;
    const qreal padding = m_compact ? 14 : 20;
    const qreal gap = m_compact ? 8 : 12;
    const qreal floorPadding = m_compact || m_viewportWidth <= 640 ? 12 : 18;
    const qreal floorVertical = m_compact ? 10 : 16;
    if (kind == "threadHeading")
        return 12 + floorVertical + 20;
    if (kind == "threadFooter") {
        const bool loading = entry.value("loading").toBool();
        const bool error = !entry.value("error").toString().isEmpty();
        const qreal state = loading || error || entry.value("empty").toBool() ? 42
                            : entry.value("hasMore").toBool()                 ? 48
                                                                              : 28.5;
        return state + floorVertical + padding + gap;
    }
    if (kind == "composer") {
        const bool nested = entry.value("reply_nested").toBool();
        const bool end = !nested && !entry.value("thread_expanded").toBool();
        return 12 + 218 + (end ? 12 + padding + gap : nested ? 28 : 0);
    }
    const bool reply = kind == "reply";
    const qreal rootAvatar = m_compact ? 30 : 36;
    const qreal width =
        qMax(1., m_width - 2 * outer - 2 * padding - rootAvatar - 12 -
                     (reply ? 2 * floorPadding + 30 + (m_viewportWidth <= 640 ? 8 : 12) : 0));
    auto body = entry.value("body", entry.value("content")).toString();
    const auto points = body.toUcs4();
    const bool longBody = points.size() > 120;
    if (longBody && !entry.value("content_expanded").toBool())
        body = QString::fromStdU32String(std::u32string(points.cbegin(), points.cbegin() + 120)) +
               QStringLiteral("... 展开");
    else if (longBody)
        body += QStringLiteral(" 收起");
    auto font = m_bodyFont;
    if (reply)
        font.setPixelSize(m_compact ? 12 : 13);
    qreal main = reply ? (m_compact ? 16.5 : 18) : (m_compact ? 35 : 38);
    main += reply ? 4 : m_compact ? 8 : 12;
    main += textHeight(body, font, width, font.pixelSize() * 1.75);
    if (!reply && !entry.value("quote_name").toString().isEmpty())
        main += 17;
    if (!entry.value("quote_name").toString().isEmpty())
        main += 8 + qMin(96., 12 + textHeight(entry.value("quote_name").toString() + QChar(0xff1a) +
                                                  entry.value("quote_content").toString(),
                                              m_quoteFont, width - 22, 19.2));
    const bool removable = m_controller && m_controller->canDelete(entry);
    main += reply ? 8 + (removable ? 36 : 18) : 10 + (removable ? 36 : 24);
    const bool continues =
        entry.value("thread_expanded").toBool() || entry.value("reply_open").toBool();
    return reply ? (entry.value("reply_open").toBool() ? 16 : 32) + qMax(30., main)
                 : padding + qMax(rootAvatar, main) + (continues ? 0 : padding + gap);
}
void CommentExtent::sync()
{
    QList<QString> keys;
    QHash<QString, int> occurrences;
    qreal total = 0;
    if (m_controller && m_width > 0) {
        auto *model = m_controller->model();
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto index = model->index(row);
            const auto kind = model->data(index, CommentsModel::Kind).toString();
            const auto entry = model->geometryEntry(row);
            const auto identity =
                key(entry, kind, model->data(index, CommentsModel::RootId).toString());
            keys.append(identity);
            ++occurrences[identity];
            if (!m_heights.contains(identity))
                m_heights.insert(identity, {estimate(entry, kind)});
            total += m_heights[identity].value;
        }
    }
    for (auto it = m_heights.begin(); it != m_heights.end();)
        if (!occurrences.contains(it.key()))
            it = m_heights.erase(it);
        else
            ++it;
    m_occurrences = std::move(occurrences);
    m_keys = std::move(keys);
    m_total = total;
    notify();
}
qreal CommentExtent::heightAt(int row) const
{
    return row >= 0 && row < m_keys.size() ? m_heights.value(m_keys[row], {128}).value : 128;
}
void CommentExtent::record(const QString &identity, qreal height)
{
    if (!qIsFinite(height) || height <= 0)
        return;
    auto it = m_heights.find(identity);
    if (it == m_heights.end()) {
        m_heights.insert(identity, {height});
        return;
    }
    if (qAbs(it->value - height) < .01)
        return;
    m_total += (height - it->value) * m_occurrences.value(identity);
    it->value = height;
    notify();
}
void CommentExtent::notify()
{
    if (m_notifying)
        return;
    m_notifying = true;
    QTimer::singleShot(0, this, [this] {
        m_notifying = false;
        emit changed();
    });
}
} // namespace EchoMusic
