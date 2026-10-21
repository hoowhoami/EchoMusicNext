#pragma once
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
namespace EchoMusic
{
/// One live installation-scoped configuration. Validation occurs before the
/// bounded atomic writer; accepted writes drain before update/retirement.
class AppearanceConfiguration final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the appearance catalog")
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QVariantMap defaults READ defaults CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    AppearanceConfiguration(QString file, QVariantMap defaults,
                            std::function<QString(const QVariantMap &)> validator, QObject *parent);
    AppearanceConfiguration(AppearanceConfiguration &, std::function<bool()> guard,
                            QObject *parent);
    ~AppearanceConfiguration() override;
    QVariantMap values() const
    {
        return m_proxy && m_delegate ? m_delegate->values() : m_values;
    }
    QVariantMap defaults() const
    {
        return m_proxy && m_delegate ? m_delegate->defaults() : m_defaults;
    }
    QString errorMessage() const
    {
        return m_proxy ? (m_delegate ? m_delegate->errorMessage()
                                     : QStringLiteral("Configuration expired"))
                       : m_error;
    }
    bool active() const
    {
        return m_proxy ? m_delegate && m_delegate->active() && m_guard && m_guard() : m_active;
    }
    bool busy() const
    {
        return m_proxy ? m_delegate && m_delegate->busy() : m_pending != 0;
    }
    Q_INVOKABLE bool patch(const QVariantMap &);
    Q_INVOKABLE bool reset();
    void publish();
    void retire();
    static bool isJson(const QVariantMap &);
signals:
    void changed();
    void errorChanged();
    void busyChanged();
    void drained();

private:
    QPointer<AppearanceConfiguration> m_delegate;
    std::function<bool()> m_guard;
    bool m_proxy = false;
    bool apply(const QVariantMap &);
    QString m_file, m_error;
    QVariantMap m_defaults, m_values, m_requested;
    std::function<QString(const QVariantMap &)> m_validate;
    QThreadPool m_workers;
    int m_pending = 0;
    bool m_active = false;
};
} // namespace EchoMusic
