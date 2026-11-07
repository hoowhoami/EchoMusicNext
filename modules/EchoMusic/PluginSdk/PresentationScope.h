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
    ~PresentationScope() override;
    bool presented() const
    {
        return m_presented;
    }
signals:
    void presentedChanged();

private:
    void refresh();
    bool m_presented = false;
};
} // namespace EchoMusic
