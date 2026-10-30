#pragma once
#include "EchoPluginOperation.h"
#include <QList>
#include <QStringList>
#include <atomic>
#include <memory>

namespace EchoMusic
{
class EchoPluginContext;
/// Bounded background word breaking; never owns lyric or playback state.
class EchoPluginText final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString backend READ backend CONSTANT)
    Q_PROPERTY(QString unicodeVersion READ unicodeVersion CONSTANT)
public:
    explicit EchoPluginText(EchoPluginContext &);
    static bool available();
    static QString backend();
    static QString unicodeVersion();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *segmentWords(const QStringList &texts,
                                                             const QString &locale = {});
    void shutdown();
    bool isDrained() const
    {
        return m_pending == 0;
    }
signals:
    void drained();

private:
    EchoPluginContext &m_scope;
    QList<std::shared_ptr<std::atomic_bool>> m_cancellations;
    int m_pending = 0;
    bool m_closed = false;
};
} // namespace EchoMusic
