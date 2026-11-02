#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Window-local outside-press handling for editable controls. It observes only
/// while its editor owns focus and never accepts a pointer event. Composite
/// fields may include their trigger and popup contents in the editing boundary.
class EchoInputFocusAttached : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *boundary MEMBER m_boundary NOTIFY boundaryChanged)
    Q_PROPERTY(QQuickItem *extension MEMBER m_extension NOTIFY extensionChanged)
public:
    explicit EchoInputFocusAttached(QObject *parent);
    ~EchoInputFocusAttached() override;
signals:
    void boundaryChanged();
    void extensionChanged();
    void outsidePressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncWindow();
    QPointer<QQuickItem> m_editor, m_boundary, m_extension;
    QPointer<QQuickWindow> m_window;
};

class EchoInputFocus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Attach to an editor")
    QML_ATTACHED(EchoInputFocusAttached)
public:
    static EchoInputFocusAttached *qmlAttachedProperties(QObject *object)
    {
        return new EchoInputFocusAttached(object);
    }
};
} // namespace EchoMusic
QML_DECLARE_TYPEINFO(EchoMusic::EchoInputFocus, QML_HAS_ATTACHED_PROPERTIES)
