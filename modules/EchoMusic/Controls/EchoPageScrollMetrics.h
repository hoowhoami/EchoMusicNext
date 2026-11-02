#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;
namespace EchoMusic
{
/// Read-only system UI metrics. Qt.styleHints is exposed as generic QObject in
/// QML; a typed projection avoids dynamic meta-property access in controls.
class EchoPageScrollMetrics final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int wheelStep READ wheelStep NOTIFY wheelStepChanged FINAL)
    Q_PROPERTY(int startDragDistance READ startDragDistance NOTIFY startDragDistanceChanged FINAL)
public:
    // No default constructor: QML otherwise instantiates through QQmlElement<T>,
    // which cannot derive from a final class.
    explicit EchoPageScrollMetrics(QObject *parent);
    static EchoPageScrollMetrics *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    int wheelStep() const;
    int startDragDistance() const;
signals:
    void wheelStepChanged();
    void startDragDistanceChanged();
};
} // namespace EchoMusic
