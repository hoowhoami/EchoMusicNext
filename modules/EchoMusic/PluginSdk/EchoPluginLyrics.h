#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include "EchoPluginPlayer.h"
#include <QJSValue>
namespace EchoMusic
{
class EchoPluginContext;
/// Reads and writes the host's unique lyric pipeline. A write is a scoped
/// overlay, never an application DB/file write or a second lyric store.
class EchoPluginLyrics final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(EchoMusic::EchoPluginReadModel *model READ model CONSTANT)
public:
    explicit EchoPluginLyrics(EchoPluginContext &scope);
    QVariantMap snapshot() const;
    EchoPluginReadModel *model() const;
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerResolver(const QString &id,
                                                                    const QJSValue &definition);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    onSnapshot(const QString &id, const QJSValue &callback, bool immediate = true);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *write(const QString &revision,
                                                      const QVariantMap &document);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *clear(const QString &revision);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *reload(const QString &revision,
                                                       bool preserveCurrent = true);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *command(const QString &action,
                                                        const QVariantMap &arguments = {});
signals:
    void changed();

private:
    QString writeKey() const;
    EchoPluginContext &m_scope;
    QPointer<EchoPluginRegistration> m_write;
};
} // namespace EchoMusic
