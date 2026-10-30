#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <memory>
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
    // The native dialog runs as a GUI-thread exec() queued behind open(); the
    // flags keep a teardown that lands inside the modal loop from freeing the
    // dialog under IFileDialog::Show.
    bool m_inExec = false;
    std::shared_ptr<bool> m_cancelled;
    bool m_directory = false;
    // True once the queued exec actually presented the dialog; an accepted()
    // arriving before that is a programmatic completion (tests), whose
    // selection has to fall back to the dialog state.
    bool m_presented = false;
};
} // namespace EchoMusic
