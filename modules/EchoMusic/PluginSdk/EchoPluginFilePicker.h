#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
class QFileDialog;
namespace EchoMusic
{
/// Short-lived native dialog controller, allocated only by an explicit request.
/// It is not an SDK type; native paths never pass through plugin callbacks.
class EchoPluginFilePicker final : public QObject
{
    Q_OBJECT
public:
    EchoPluginFilePicker(const QString &pluginName, bool directory, const QVariantMap &options,
                         QObject *parent);
    ~EchoPluginFilePicker() override;
    static bool busy();
    void open();
    void cancel();
    static QString validate(bool directory, const QVariantMap &options);
signals:
    void selected(const QStringList &paths);
    void rejected();

private:
    QPointer<QFileDialog> m_dialog;
};
} // namespace EchoMusic
