#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace EchoMusic
{
/// Author-owned visual container. No host item or ancestor is exposed to QML.
class PresentationScope : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool presented READ presented NOTIFY presentedChanged)
public:
    explicit PresentationScope(QQuickItem *parent = nullptr);
    bool presented() const
    {
        return m_presented;
    }
signals:
    void presentedChanged();

private:
    void observe();
    void refresh();
    bool m_presented = false;
    bool m_observing = false;
    std::vector<QMetaObject::Connection> m_connections;
};
} // namespace EchoMusic
