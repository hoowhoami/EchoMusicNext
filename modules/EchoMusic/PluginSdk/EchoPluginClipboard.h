#pragma once
#include "EchoPluginOperation.h"
namespace EchoMusic
{
class EchoPluginContext;
/// GUI-thread, text-only system clipboard writing. No clipboard reads.
class EchoPluginClipboard final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
public:
    explicit EchoPluginClipboard(EchoPluginContext &);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *write(const QString &text);

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
