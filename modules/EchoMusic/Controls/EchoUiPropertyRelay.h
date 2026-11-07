#pragma once
#include <QVariant>
#include <QString>
#include <memory>
class QObject;
namespace EchoMusic
{
// Moves only a binding's output; the original QML creation scope and dependency
// graph remain live. Retiring the lease puts that same binding back on its owner.
class EchoUiPropertyRelay
{
public:
    EchoUiPropertyRelay(QObject *source, const QString &property, QObject *sink,
                        const QString &sinkProperty);
    ~EchoUiPropertyRelay();
    void capture();
    void restore();
    bool bound() const;
    QVariant value() const;

private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
} // namespace EchoMusic
