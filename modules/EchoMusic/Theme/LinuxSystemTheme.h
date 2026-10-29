#pragma once
#include <QDBusConnection>
#include <QDBusVariant>
#include <QObject>

namespace EchoMusic
{
/// Read-only desktop appearance adapter. The store remains the preference owner;
/// portal subscriptions and pending replies die with this nonvisual child.
class LinuxSystemTheme final : public QObject
{
    Q_OBJECT
public:
    explicit LinuxSystemTheme(QObject *parent = nullptr);
    explicit LinuxSystemTheme(const QDBusConnection &bus, QObject *parent = nullptr);
    Qt::ColorScheme scheme() const
    {
        return m_scheme;
    }
signals:
    void schemeChanged(Qt::ColorScheme scheme);
private slots:
    void settingChanged(const QString &group, const QString &key, const QDBusVariant &value);

private:
    bool eventFilter(QObject *object, QEvent *event) override;
    void read();
    void update();
    QDBusConnection m_bus;
    quint64 m_revision = 0;
    Qt::ColorScheme m_portalScheme = Qt::ColorScheme::Unknown;
    Qt::ColorScheme m_scheme = Qt::ColorScheme::Unknown;
};
} // namespace EchoMusic
