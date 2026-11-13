#pragma once
#include "EchoPluginOperation.h"
namespace EchoMusic
{
class EchoPluginContext;
/// System clipboard writing for text and encoded images; image decoding runs on the
/// worker pool, never in the GUI callback. No clipboard reads.
class EchoPluginClipboard final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
public:
    explicit EchoPluginClipboard(EchoPluginContext &);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *write(const QString &text);
    /// PNG/JPEG/BMP/WebP bytes; decoded off the GUI thread with a 4096-per-edge and
    /// 16M-pixel budget. The clipboard keeps the decoded image, not the encoding.
    Q_INVOKABLE EchoMusic::EchoPluginOperation *writeImage(const QByteArray &bytes);

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
