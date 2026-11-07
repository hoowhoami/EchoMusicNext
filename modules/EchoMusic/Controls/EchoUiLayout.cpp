#include "EchoUiLayout.h"
#include <QtQuick/private/qquickitem_p.h>

namespace EchoMusic::EchoUiLayout
{
void setDecoration(QQuickItem *item, bool decoration)
{
    if (item)
        QQuickItemPrivate::get(item)->setTransparentForPositioner(decoration);
}
QString kind(const QQuickItem *item)
{
    if (!item)
        return {};
    if (item->inherits("QQuickRowLayout") || item->inherits("QQuickRow"))
        return "row";
    if (item->inherits("QQuickColumnLayout") || item->inherits("QQuickColumn"))
        return "column";
    if (item->inherits("QQuickGridLayout") || item->inherits("QQuickGrid"))
        return "grid";
    if (item->inherits("QQuickFlow"))
        return "flow";
    return {};
}
QString engine(const QQuickItem *item)
{
    if (kind(item).isEmpty())
        return {};
    return item->inherits("QQuickLayout") ? "layout" : "positioner";
}
QStringList itemProperties(const QQuickItem *container)
{
    QStringList result{"visible"};
    if (engine(container) == "layout") {
        result +=
            {"preferredWidth", "preferredHeight", "minimumWidth", "minimumHeight", "maximumWidth",
             "maximumHeight",  "fillWidth",       "fillHeight",   "alignment",     "margins",
             "leftMargin",     "rightMargin",     "topMargin",    "bottomMargin"};
        if (kind(container) == "grid")
            result += {"row", "column", "rowSpan", "columnSpan"};
    } else if (engine(container) == "positioner") {
        // A positioner places naturally sized children. Space distribution and
        // grid spans belong to Qt Quick Layouts, not to Row/Column/Grid/Flow.
        result += {"preferredWidth", "preferredHeight"};
    }
    return result;
}
QStringList insertionPositions(const QQuickItem *item)
{
    QStringList result{"background", "foreground"};
    if (!item)
        return {};
    if (!kind(item).isEmpty())
        result += {"append", "prepend"};
    if (item->parentItem())
        result += {"before", "after"};
    return result;
}
bool validate(const QQuickItem *container, const QVariantMap &values, QString &error)
{
    const auto supported = itemProperties(container);
    for (auto it = values.begin(); it != values.end(); ++it)
        if (!supported.contains(it.key())) {
            error = QString("%1 is not supported by this native %2 %3")
                        .arg(it.key(), kind(container), engine(container));
            return false;
        }
    return true;
}
bool validateInsertion(const QQuickItem *container, const QVariantMap &values, QString &error)
{
    if (!kind(container).isEmpty())
        return validate(container, values, error);
    const QStringList supported{
        "visible",      "preferredWidth", "preferredHeight", "minimumWidth", "minimumHeight",
        "maximumWidth", "maximumHeight",  "fillWidth",       "fillHeight",   "alignment",
        "margins",      "leftMargin",     "rightMargin",     "topMargin",    "bottomMargin"};
    for (const auto &key : values.keys())
        if (!supported.contains(key)) {
            error = "Unsupported anchored insertion property: " + key;
            return false;
        }
    return true;
}
} // namespace EchoMusic::EchoUiLayout
