#pragma once
#include <QObject>
#include <QSet>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Transient Loader-owned presentation barrier. This never enters the route's
/// value snapshot: destroyed viewports release their own readiness claims.
class EchoPagePresentation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool suppressEntry READ suppressEntry NOTIFY stateChanged)
    Q_PROPERTY(bool retiring READ retiring WRITE setRetiring NOTIFY stateChanged)
public:
    explicit EchoPagePresentation(QObject *parent = nullptr) : QObject(parent) {}
    bool ready() const
    {
        return m_ready;
    }
    bool suppressEntry() const
    {
        return m_retiring || (m_restoring && !m_presented);
    }
    bool retiring() const
    {
        return m_retiring;
    }
    void setRetiring(bool retiring);
    Q_INVOKABLE void begin(bool restoring);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void finishLoading();
    Q_INVOKABLE bool waitFor(QObject *viewport);
    Q_INVOKABLE void release(QObject *viewport);
    Q_INVOKABLE void presented();
signals:
    void readyChanged();
    void stateChanged();

private:
    void updateReady();
    QSet<QObject *> m_waiters;
    bool m_loaded = false, m_ready = false, m_restoring = false, m_presented = false,
         m_retiring = false;
};
} // namespace EchoMusic
