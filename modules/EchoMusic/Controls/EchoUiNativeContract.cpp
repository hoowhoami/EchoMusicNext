#include "EchoUiNativeContract.h"
#include <QQuickWindow>
#include <QMetaProperty>
#include <cmath>
#include <QtQuick/private/qquickitem_p.h>
namespace EchoMusic::EchoUiNativeContract
{
namespace
{
bool has(QQuickItem *item, const char *name)
{
    return item && item->metaObject()->indexOfProperty(name) >= 0;
}
bool editor(QQuickItem *item)
{
    return item && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")) &&
           (!has(item, "echoMode") || item->property("echoMode").toInt() == 0);
}
bool number(const QVariant &value)
{
    return (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::Double ||
            value.metaType().id() == QMetaType::LongLong) &&
           std::isfinite(value.toDouble());
}
} // namespace
QStringList stateProperties(QQuickItem *item)
{
    QStringList result;
    for (const auto *name :
         {"enabled",  "visible",  "activeFocus",  "hovered",         "pressed",
          "down",     "checked",  "checkable",    "checkState",      "value",
          "from",     "to",       "position",     "currentIndex",    "count",
          "contentX", "contentY", "contentWidth", "contentHeight",   "moving",
          "dragging", "flicking", "readOnly",     "acceptableInput", "inputMethodComposing"})
        if (has(item, name))
            result.append(QString::fromLatin1(name));
    // Selection indices are useful; editor text, passwords and clipboard
    // contents are never included in generic public state snapshots.
    if (editor(item))
        result += {"cursorPosition", "selectionStart", "selectionEnd"};
    return result;
}
QVariantMap state(QQuickItem *item)
{
    QVariantMap result;
    for (const auto &name : stateProperties(item))
        result.insert(name, item->property(name.toUtf8()));
    return result;
}
QStringList actions(QQuickItem *item)
{
    QStringList result;
    if (!item)
        return result;
    result += {"focus", "focusNext", "focusPrevious"};
    if (item->inherits("QQuickAbstractButton") && item->metaObject()->indexOfMethod("click()") >= 0)
        result.append("activate");
    if (has(item, "value") && has(item, "from") && has(item, "to"))
        result.append("setValue");
    if (has(item, "currentIndex") && has(item, "count"))
        result.append("selectIndex");
    if (item->inherits("QQuickFlickable"))
        result.append("scrollTo");
    if (editor(item))
        result += {"selectText", "selectAll", "insertText"};
    return result;
}
bool invoke(QQuickItem *item, const QString &action, const QVariantMap &arguments)
{
    if (!item || !actions(item).contains(action))
        return false;
    if (action == "focus") {
        if (!arguments.isEmpty())
            return false;
        item->forceActiveFocus(Qt::OtherFocusReason);
        return item->hasActiveFocus();
    }
    if (action == "focusNext" || action == "focusPrevious") {
        if (!arguments.isEmpty())
            return false;
        auto *navigation = qobject_cast<QQuickKeyNavigationAttached *>(
            qmlAttachedPropertiesObject<QQuickKeyNavigationAttached>(item, false));
        auto *next = navigation ? action == "focusNext" ? navigation->tab() : navigation->backtab()
                                : nullptr;
        if (!next)
            next = item->nextItemInFocusChain(action == "focusNext");
        if (!next || !next->isVisible() || !next->isEnabled())
            return false;
        next->forceActiveFocus(action == "focusNext" ? Qt::TabFocusReason : Qt::BacktabFocusReason);
        return next->hasActiveFocus();
    }
    if (action == "activate")
        return arguments.isEmpty() && QMetaObject::invokeMethod(item, "click");
    if (action == "setValue") {
        const auto value = arguments.value("value");
        if (arguments.size() != 1 || !number(value))
            return false;
        const auto a = item->property("from").toDouble(), b = item->property("to").toDouble();
        return value.toDouble() >= qMin(a, b) && value.toDouble() <= qMax(a, b) &&
               item->setProperty("value", value);
    }
    if (action == "selectIndex") {
        const auto value = arguments.value("index");
        return arguments.size() == 1 && number(value) && value.toDouble() == value.toInt() &&
               value.toInt() >= 0 && value.toInt() < item->property("count").toInt() &&
               item->setProperty("currentIndex", value.toInt());
    }
    if (action == "scrollTo") {
        if (arguments.isEmpty() || arguments.size() > 2)
            return false;
        for (auto it = arguments.begin(); it != arguments.end(); ++it)
            if ((it.key() != "x" && it.key() != "y") || !number(it.value()))
                return false;
        for (const auto *axis : {"x", "y"}) {
            if (!arguments.contains(axis))
                continue;
            const bool horizontal = *axis == 'x';
            const auto origin = item->property(horizontal ? "originX" : "originY").toDouble();
            const auto extent =
                item->property(horizontal ? "contentWidth" : "contentHeight").toDouble();
            const auto size = horizontal ? item->width() : item->height();
            item->setProperty(
                horizontal ? "contentX" : "contentY",
                qBound(origin, arguments.value(axis).toDouble(), origin + qMax(0., extent - size)));
        }
        return true;
    }
    if (action == "selectAll")
        return arguments.isEmpty() && QMetaObject::invokeMethod(item, "selectAll");
    if (action == "selectText") {
        const auto start = arguments.value("start"), end = arguments.value("end");
        const auto length = item->property("length").toInt();
        if (arguments.size() != 2 || !number(start) || !number(end) ||
            start.toDouble() != start.toInt() || end.toDouble() != end.toInt() ||
            start.toInt() < 0 || end.toInt() < start.toInt() || end.toInt() > length)
            return false;
        return QMetaObject::invokeMethod(item, "select", Q_ARG(int, start.toInt()),
                                         Q_ARG(int, end.toInt()));
    }
    if (action == "insertText") {
        const auto text = arguments.value("text");
        if (arguments.size() != 1 || text.metaType().id() != QMetaType::QString ||
            text.toString().size() > 4096 || item->property("readOnly").toBool())
            return false;
        return QMetaObject::invokeMethod(item, "insert",
                                         Q_ARG(int, item->property("cursorPosition").toInt()),
                                         Q_ARG(QString, text.toString()));
    }
    return false;
}
QMap<QString, QString> visuals(QQuickItem *item)
{
    QMap<QString, QString> result;
    for (const auto *name :
         {"padding", "leftPadding", "rightPadding", "topPadding", "bottomPadding", "spacing"})
        if (has(item, name))
            result.insert(name, name);
    if (has(item, "text") && !item->inherits("QQuickTextInput") &&
        !item->inherits("QQuickTextEdit"))
        result.insert("text", "text");
    if (has(item, "glyph"))
        result.insert("glyph", "glyph");
    if (has(item, "toolTip"))
        result.insert("toolTip", "toolTip");
    return result;
}
} // namespace EchoMusic::EchoUiNativeContract
