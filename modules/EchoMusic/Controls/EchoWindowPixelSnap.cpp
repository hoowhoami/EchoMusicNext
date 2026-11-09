#include "EchoWindowPixelSnap.h"
#include "EchoItemObserver.h"
#include "EchoPixelSnap.h"
#include "EchoQueuedNotice.h"
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMatrix4x4>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPointer>
#include <QQuickWindow>
#include <QTextBlock>
#include <QTextDocument>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquicktext_p_p.h>
#include <QtQuick/private/qquicktextinput_p_p.h>
#include <QtQuick/private/qquicktextedit_p_p.h>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace EchoMusic
{
namespace
{
Q_LOGGING_CATEGORY(pixelLog, "echo.presentation.pixel")
class PhaseTranslation final : public QQuickTransform
{
public:
    PhaseTranslation(std::unique_ptr<PhaseTranslation> *owner, QQuickItem *item)
        : lease(owner), target(item)
    {
    }
    std::unique_ptr<PhaseTranslation> *lease;
    QQuickItem *target;
    QPointF offset;
    void targetDestroyed(QQuickItem *item)
    {
        // Qt can replace a layer proxy's transforms directly. Its destructor
        // then cannot find our transform to remove this reverse association.
        // Retire the link during the item notification, without destroying a
        // transform or mutating the item's list inside a listener broadcast.
        QQuickTransformPrivate::get(this)->items.removeAll(item);
        target = nullptr;
    }
    void setOffset(QPointF next)
    {
        if (qAbs(next.x() - offset.x()) < 1e-7 && qAbs(next.y() - offset.y()) < 1e-7)
            return;
        offset = next;
        update();
    }
    void applyTo(QMatrix4x4 *matrix) const override
    {
        matrix->translate(offset.x(), offset.y());
    }
};
constexpr auto listenerFlags = QQuickItemPrivate::Geometry | QQuickItemPrivate::Matrix |
                               QQuickItemPrivate::Visibility | QQuickItemPrivate::Opacity |
                               QQuickItemPrivate::Parent | QQuickItemPrivate::Children |
                               QQuickItemPrivate::Destroyed;

// A float matrix round-trip can put an exact half-pixel tie on either side.
// Keep ties deterministic; this tolerance is 1/100000 of a device pixel.
qreal nearestPixel(qreal value)
{
    return std::floor(value + .5 + 1e-5);
}
bool axisAligned(QPointF x, QPointF y)
{
    return std::isfinite(x.x()) && std::isfinite(y.y()) && x.x() > .001 && y.y() > .001 &&
           qAbs(x.y()) < 1e-6 && qAbs(y.x()) < 1e-6;
}
bool windowPolicyEnabled()
{
    if (!EchoPixelSnapAttached::snapEnabled())
        return false;
    if (qEnvironmentVariableIsSet("ECHO_PIXEL_SNAP_ALL"))
        return qEnvironmentVariableIntValue("ECHO_PIXEL_SNAP_ALL") != 0;
    return QGuiApplication::platformName() == QStringLiteral("windows");
}
QTransform unalignedTransform(QQuickItem *item, const QTransform &parent,
                              const PhaseTranslation *own, bool stripAll = false)
{
    // Match the pinned Qt 6.12 itemToParentTransform ordering, omitting only
    // our entry. Deriving this from an already compensated float matrix would
    // feed rounding residue back into the next pass.
    auto transform = parent;
    transform.translate(item->x(), item->y());
    QMatrix4x4 matrix(transform);
    const auto *data = QQuickItemPrivate::get(item);
    bool custom = false;
    for (qsizetype i = data->transforms.size(); i-- > 0;)
        if (data->transforms.at(i) != own &&
            (!stripAll || !dynamic_cast<PhaseTranslation *>(data->transforms.at(i)))) {
            data->transforms.at(i)->applyTo(&matrix);
            custom = true;
        }
    // Do not introduce a float conversion for an otherwise untransformed
    // item. Existing leaf alignment may already be exact in logical space.
    if (custom)
        transform = matrix.toTransform();
    if (item->scale() != 1 || item->rotation() != 0) {
        const auto origin = data->computeTransformOrigin();
        transform.translate(origin.x(), origin.y());
        transform.scale(item->scale(), item->scale());
        transform.rotate(item->rotation());
        transform.translate(-origin.x(), -origin.y());
    }
    return transform;
}
} // namespace

namespace
{
qreal alignmentOffset(qreal extent, qreal available, int align)
{
    if (align & (Qt::AlignRight | Qt::AlignBottom))
        return available - extent;
    if (align & (Qt::AlignHCenter | Qt::AlignVCenter))
        return (available - extent) / 2;
    return 0;
}
QPointF layoutAnchor(const QTextLayout *layout, QPointF offset, QPointF fallback)
{
    if (!layout || layout->lineCount() == 0)
        return fallback;
    const auto line = layout->lineAt(0);
    return offset + layout->position() +
           QPointF(line.cursorToX(line.textStart()), line.y() + line.ascent());
}
} // namespace
QPointF EchoWindowPixelSnap::contentAnchor(QQuickItem *item)
{
    if (!item)
        return {};
    const QPointF fallback(0, item->baselineOffset());
    if (auto *text = qobject_cast<QQuickText *>(item)) {
        const auto *data = QQuickTextPrivate::get(text);
        const qreal dy = text->topPadding() +
                         alignmentOffset(data->layedOutTextRect.height() + data->lineHeightOffset(),
                                         data->availableHeight(), data->vAlign);
        // contentWidth is not the layout's alignment width. Fixed-box
        // centered/elided text has an additional QTextLine origin.
        if (data->richText) {
            const auto *doc = data->extra.isAllocated() ? data->extra->doc : nullptr;
            const auto block = doc ? doc->firstBlock() : QTextBlock();
            const qreal dx = text->leftPadding() + alignmentOffset(data->layedOutTextRect.width(),
                                                                   data->availableWidth(),
                                                                   text->effectiveHAlign());
            return layoutAnchor(block.isValid() ? block.layout() : nullptr, {dx, dy}, fallback);
        }
        const qreal dx =
            text->leftPadding() +
            alignmentOffset(data->lineWidth, data->availableWidth(), text->effectiveHAlign());
        const auto *layout =
            data->elideLayout && data->lineCount <= 1 ? data->elideLayout.get() : &data->layout;
        return layoutAnchor(layout, {dx, dy}, fallback);
    }
    if (auto *input = qobject_cast<QQuickTextInput *>(item)) {
        const auto *data = QQuickTextInputPrivate::get(input);
        const auto rect = input->positionToRectangle(0);
        return data->m_textLayout.lineCount()
                   ? QPointF(rect.x(), rect.y() + data->m_textLayout.lineAt(0).ascent())
                   : fallback;
    }
    if (auto *edit = qobject_cast<QQuickTextEdit *>(item)) {
        const auto *data = QQuickTextEditPrivate::get(edit);
        const auto block = data->document ? data->document->firstBlock() : QTextBlock();
        const auto *layout = block.isValid() ? block.layout() : nullptr;
        const auto rect = edit->positionToRectangle(0);
        return layout && layout->lineCount()
                   ? QPointF(rect.x(), rect.y() + layout->lineAt(0).ascent())
                   : fallback;
    }
    return {};
}
QPointF EchoWindowPixelSnap::layoutPositionInParent(QQuickItem *item, QPointF point)
{
    if (!item)
        return {};
    for (auto *transform : QQuickItemPrivate::get(item)->transforms)
        if (dynamic_cast<PhaseTranslation *>(transform))
            return unalignedTransform(item, QTransform(), nullptr, true).map(point);
    return item->mapToItem(item->parentItem(), point);
}

struct EchoWindowPixelSnap::State
{
    struct Entry final : QQuickItemChangeListener
    {
        State *state;
        QQuickItem *item;
        Entry *parent = nullptr;
        bool presented = false;
        QRectF viewport;
        bool text;
        std::unique_ptr<PhaseTranslation> translation;
        std::vector<QMetaObject::Connection> connections;
        Entry(State *owner, QQuickItem *target)
            : state(owner), item(target),
              text(target->inherits("QQuickText") || target->inherits("QQuickTextInput") ||
                   target->inherits("QQuickTextEdit"))
        {
            QQuickItemPrivate::get(item)->addItemChangeListener(this, listenerFlags);
            if (text) {
                const auto *meta = item->metaObject();
                connections.push_back(QObject::connect(item, &QQuickItem::baselineOffsetChanged,
                                                       state->scope,
                                                       [owner] { owner->schedule(); }));
                // Text can change its internal alignment origin without changing
                // its fixed outer geometry (elision, translation, font changes).
                std::unordered_set<int> noticeMethods;
                for (const auto *name :
                     {"contentWidth", "contentHeight", "effectiveHorizontalAlignment",
                      "verticalAlignment", "leftPadding", "rightPadding", "topPadding",
                      "bottomPadding", "text", "font", "textFormat", "lineHeight",
                      "cursorRectangle"}) {
                    const int index = meta->indexOfProperty(name);
                    if (index < 0 || !meta->property(index).hasNotifySignal())
                        continue;
                    const auto signal = meta->property(index).notifySignal();
                    if (!noticeMethods.insert(signal.methodIndex()).second)
                        continue;
                    connections.push_back(QObject::connect(
                        item, signal, state->scope,
                        state->scope->metaObject()->method(
                            state->scope->metaObject()->indexOfSlot("scheduleAlignment()"))));
                }
            }
        }
        ~Entry() override
        {
            for (const auto &connection : connections)
                QObject::disconnect(connection);
            if (item && !QQuickItemPrivate::get(item)->inDestructor)
                QQuickItemPrivate::get(item)->removeItemChangeListener(this, listenerFlags);
            // QQuickTransform's destructor removes only its own list entry.
            // Existing user transforms/bindings are never cleared or rebuilt.
            translation.reset();
        }
        void itemGeometryChanged(QQuickItem *, QQuickGeometryChange, const QRectF &) override
        {
            state->schedule();
        }
        void itemTransformChanged(QQuickItem *, QQuickItem *) override
        {
            state->schedule();
        }
        void itemVisibilityChanged(QQuickItem *) override
        {
            state->schedule();
        }
        void itemOpacityChanged(QQuickItem *) override
        {
            state->schedule();
        }
        void itemParentChanged(QQuickItem *, QQuickItem *) override
        {
            state->schedule(true);
        }
        void itemChildAdded(QQuickItem *, QQuickItem *) override
        {
            state->schedule(true);
        }
        void itemChildRemoved(QQuickItem *, QQuickItem *) override
        {
            state->schedule(true);
        }
        void itemDestroyed(QQuickItem *destroyed) override
        {
            if (translation)
                translation->targetDestroyed(destroyed);
            item = nullptr;
            state->schedule(true);
        }
        QPointF anchor() const
        {
            return text ? EchoWindowPixelSnap::contentAnchor(item) : QPointF();
        }
    };

    EchoWindowPixelSnap *scope;
    QPointer<QQuickWindow> window;
    QPointer<QQuickItem> root;
    std::unordered_map<QQuickItem *, std::unique_ptr<Entry>> entries;
    std::vector<Entry *> ordered;
    bool enabled = true, dirtyTree = true, pending = false, deferred = false, applying = false;
    Statistics stats;
    const bool diagnostics = qEnvironmentVariableIntValue("ECHO_RENDER_DIAGNOSTICS") == 1;

    void report(const char *reason) const
    {
        if (diagnostics && stats.trackedItems)
            qCInfo(pixelLog) << reason << "tracked-items=" << stats.trackedItems
                             << "compensated-items=" << stats.compensatedItems
                             << "entry-payload-bytes=" << stats.payloadBytes
                             << "alignment-passes=" << stats.passes << "average-alignment-ms="
                             << (stats.passes
                                     ? stats.totalNanoseconds / (1e6 * double(stats.passes))
                                     : 0.)
                             << "maximum-alignment-ms=" << stats.maximumNanoseconds / 1e6;
    }

    explicit State(EchoWindowPixelSnap *owner, QQuickWindow *target)
        : scope(owner), window(target), root(target->contentItem())
    {
    }
    bool active() const
    {
        return enabled && window && window->isVisible() &&
               window->visibility() != QWindow::Minimized;
    }
    void schedule(bool tree = false)
    {
        dirtyTree |= tree;
        if (applying)
            return;
        if (!pending) {
            pending = true;
            scope->polish();
        }
        // Hidden windows do not render/polish. Retire resources on the GUI
        // event loop too, never by mutating a native listener broadcast.
        if (!active())
            queueCoalescedNotice(scope, deferred, [this] {
                if (!active()) {
                    applying = true;
                    report("Pixel alignment suspended");
                    ordered.clear();
                    entries.clear();
                    decltype(ordered){}.swap(ordered);
                    decltype(entries){}.swap(entries);
                    stats.trackedItems = stats.compensatedItems = stats.payloadBytes = 0;
                    dirtyTree = true;
                    applying = false;
                    notifyItemScenePositionChanged(root);
                }
            });
    }
    void reconcile()
    {
        // Retiring transforms can synchronously change a Qt layer's subtree.
        // Preserve those new invalidations for the next polish.
        dirtyTree = false;
        ordered.clear();
        std::unordered_set<QQuickItem *> present;
        const auto visit = [&](auto &&self, QQuickItem *item, Entry *parent) -> void {
            if (!item || item == scope || item->window() != window)
                return;
            present.insert(item);
            auto found = entries.find(item);
            if (found != entries.end() && found->second->item != item) {
                entries.erase(found);
                found = entries.end();
            }
            if (found == entries.end())
                found = entries.emplace(item, std::make_unique<Entry>(this, item)).first;
            auto *entry = found->second.get();
            entry->parent = parent;
            ordered.push_back(entry);
            for (auto *child : item->childItems())
                self(self, child, entry);
        };
        visit(visit, root, nullptr);
        for (auto it = entries.begin(); it != entries.end();)
            if (!present.contains(it->first))
                it = entries.erase(it);
            else
                ++it;
    }
    void align(Entry &entry, qreal dpr)
    {
        auto *item = entry.item;
        if (!item)
            return;
        const auto *data = QQuickItemPrivate::get(item);
        const bool referenced =
            data->extra.isAllocated() && data->extra->recursiveEffectRefCount > 0;
        entry.presented = (!entry.parent || entry.parent->presented) &&
                          (item->isVisible() || referenced) && item->opacity() > 0 && !data->culled;
        entry.viewport = entry.parent ? entry.parent->viewport : QRectF(QPointF(), window->size());
        if (item->clip())
            entry.viewport = entry.viewport.intersected(item->mapRectToScene(item->boundingRect()));
        // Qt owns the root matrices of its positioner-transparent render
        // proxies (layer effect/source). updateMatrix() replaces their entire
        // transforms list with the source's borrowed list. Compensate that
        // source instead, including a non-painting Item with an enabled layer.
        if (data->isTransparentForPositioner()) {
            entry.translation.reset();
            return;
        }
        // A reparent can reach the new window's polish before the old window
        // retires its entry. Only hand off a transform attached to this item;
        // a borrowed source transform is not a second owner's correction.
        const auto transforms = data->transforms;
        for (auto *transform : transforms)
            if (auto *phase = dynamic_cast<PhaseTranslation *>(transform);
                phase && phase->target == item && phase != entry.translation.get())
                phase->lease->reset();
        const bool layered =
            data->extra.isAllocated() && data->extra->layer && data->extra->layer->enabled();
        if (!entry.presented || item == root ||
            (!(item->flags() & QQuickItem::ItemHasContents) && !layered) || item->width() <= 0 ||
            item->height() <= 0) {
            entry.translation.reset();
            return;
        }
        if (entry.translation) {
            const auto &transforms = QQuickItemPrivate::get(item)->transforms;
            if (transforms.isEmpty() || transforms.constLast() != entry.translation.get())
                entry.translation.reset();
        }
        auto *parent = item->parentItem();
        const auto parentTransform =
            parent ? QQuickItemPrivate::get(parent)->itemToWindowTransform() : QTransform();
        const auto raw = unalignedTransform(item, parentTransform, entry.translation.get());
        const QPointF parentX(parentTransform.m11(), parentTransform.m12());
        const QPointF parentY(parentTransform.m21(), parentTransform.m22());
        if (!parentTransform.isAffine() || !raw.isAffine() ||
            !axisAligned({raw.m11(), raw.m12()}, {raw.m21(), raw.m22()}) ||
            !axisAligned(parentX, parentY) ||
            (!referenced && !entry.viewport.intersects(raw.mapRect(item->boundingRect())))) {
            entry.translation.reset();
            return;
        }
        // Last-appended QQuickTransform is outside the item's own scale and
        // existing transforms. Ancestor compensation is retained and resolved
        // before descendants; logical dimensions and text layout are untouched.
        const auto previous = entry.translation ? entry.translation->offset : QPointF();
        const auto point = raw.map(entry.anchor());
        const QPointF next((nearestPixel(point.x() * dpr) / dpr - point.x()) / parentX.x(),
                           (nearestPixel(point.y() * dpr) / dpr - point.y()) / parentY.y());
        if (qAbs(next.x()) < 1e-7 && qAbs(next.y()) < 1e-7) {
            entry.translation.reset();
            return;
        }
        if (!entry.translation) {
            entry.translation = std::make_unique<PhaseTranslation>(&entry.translation, item);
            entry.translation->appendToItem(item);
        }
        if (qAbs(next.x() - previous.x()) >= 1e-7 || qAbs(next.y() - previous.y()) >= 1e-7)
            ++stats.adjustments;
        entry.translation->setOffset(next);
    }
};

EchoWindowPixelSnap::EchoWindowPixelSnap(QQuickWindow *window)
    : QQuickItem(window->contentItem()), m_state(std::make_unique<State>(this, window))
{
    setObjectName(QStringLiteral("echoWindowPixelSnap"));
    setProperty("_echoUiInternal", true); // Never expose the polish owner as a plugin UI part.
    setVisible(false);                    // Native polish owner, never a visual/plugin surface.
    window->installEventFilter(this);
    connect(window, &QWindow::visibleChanged, this, [this] { m_state->schedule(true); });
    connect(window, &QWindow::visibilityChanged, this, [this] { m_state->schedule(true); });
    connect(window, &QWindow::screenChanged, this, [this] { m_state->schedule(); });
    m_state->schedule(true);
    if (qEnvironmentVariableIntValue("ECHO_RENDER_DIAGNOSTICS") == 1)
        qCInfo(pixelLog) << "All-content pixel phase compensation installed; effective-dpr="
                         << window->effectiveDevicePixelRatio();
}
EchoWindowPixelSnap::~EchoWindowPixelSnap()
{
    m_state->report("Pixel alignment retired");
    if (m_state->window) {
        m_state->window->removeEventFilter(this);
        disconnect(m_state->window, nullptr, this, nullptr);
    }
    m_state->applying = true;
    m_state->ordered.clear();
    m_state->entries.clear();
}
EchoWindowPixelSnap *EchoWindowPixelSnap::forWindow(QQuickWindow *window, bool create)
{
    if (!window)
        return nullptr;
    for (auto *child : window->contentItem()->childItems())
        if (auto *scope = qobject_cast<EchoWindowPixelSnap *>(child))
            return scope;
    return create ? new EchoWindowPixelSnap(window) : nullptr;
}
EchoWindowPixelSnap::Statistics EchoWindowPixelSnap::statistics() const
{
    return m_state->stats;
}
void EchoWindowPixelSnap::setAlignmentEnabled(bool enabled)
{
    if (m_state->enabled == enabled)
        return;
    m_state->enabled = enabled;
    m_state->schedule(true);
}
bool EchoWindowPixelSnap::alignmentEnabled() const
{
    return m_state->enabled;
}
bool EchoWindowPixelSnap::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::DevicePixelRatioChange)
        m_state->schedule();
    return false;
}
void EchoWindowPixelSnap::updatePolish()
{
    auto &state = *m_state;
    state.pending = false;
    if (!state.active())
        return;
    QElapsedTimer timer;
    timer.start();
    state.applying = true;
    if (state.dirtyTree)
        state.reconcile();
    for (auto *entry : state.ordered)
        state.align(*entry, state.window->effectiveDevicePixelRatio());
    state.stats.trackedItems = qsizetype(state.entries.size());
    state.stats.compensatedItems = 0;
    state.stats.payloadBytes =
        qsizetype(state.entries.size()) * qsizetype(sizeof(State::Entry)) +
        qsizetype(state.ordered.capacity()) * qsizetype(sizeof(State::Entry *)) +
        qsizetype(state.entries.bucket_count()) * qsizetype(sizeof(void *));
    for (auto *entry : state.ordered) {
        state.stats.payloadBytes +=
            qsizetype(entry->connections.capacity()) * qsizetype(sizeof(QMetaObject::Connection));
        if (entry->translation) {
            ++state.stats.compensatedItems;
            state.stats.payloadBytes += sizeof(PhaseTranslation);
        }
    }
    ++state.stats.passes;
    const auto elapsed = timer.nsecsElapsed();
    state.stats.totalNanoseconds += elapsed;
    state.stats.maximumNanoseconds = qMax(state.stats.maximumNanoseconds, elapsed);
    state.applying = false;
    const QPointer<EchoWindowPixelSnap> guard(this);
    notifyItemScenePositionChanged(state.root);
    // A scene observer may synchronously close/destroy its window or scope.
    if (guard && state.dirtyTree)
        state.schedule(true);
}
void EchoWindowPixelSnap::scheduleAlignment()
{
    m_state->schedule();
}

namespace
{
class WindowInstaller final : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
            if (auto *window = qobject_cast<QQuickWindow *>(object))
                EchoWindowPixelSnap::forWindow(window);
        return false;
    }
};
} // namespace
void installWindowPixelSnap()
{
    static QPointer<WindowInstaller> installer;
    if (installer || !windowPolicyEnabled())
        return;
    installer = new WindowInstaller(qGuiApp);
    qGuiApp->installEventFilter(installer);
    for (auto *window : QGuiApplication::allWindows())
        if (auto *quick = qobject_cast<QQuickWindow *>(window))
            EchoWindowPixelSnap::forWindow(quick);
}
} // namespace EchoMusic
