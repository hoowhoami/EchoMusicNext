#include "EchoFileDialog.h"
#include "UiaEventGate.h"
#include <QApplication>
#include <QFileDialog>
#include <QWindow>
namespace EchoMusic
{
static_assert(int(EchoFileDialog::DontUseNativeDialog) == int(QFileDialog::DontUseNativeDialog));
EchoFileDialog::EchoFileDialog(QObject *parent) : QObject(parent) {}
EchoFileDialog::~EchoFileDialog()
{
    if (!m_dialog)
        return;
    m_dialog->disconnect(this);
    if (m_inExec) {
        // Teardown landed inside the queued exec frame (route retirement, page
        // destruction). Reject the native dialog - the nested loop pumps the
        // close through - and keep the dialog alive until that frame returns;
        // deleting it now would free it under IFileDialog::Show.
        if (m_cancelled)
            *m_cancelled = true;
        if (m_dialog->isVisible())
            m_dialog->reject();
        m_dialog->deleteLater();
        return;
    }
    // A native helper may still have a queued completion. Disconnect before
    // closing; neither it nor the fallback widget survives the QML owner.
    delete m_dialog;
}
bool EchoFileDialog::visible() const
{
    return m_dialog && m_dialog->isVisible();
}
void EchoFileDialog::open()
{
    if (m_dialog || !qobject_cast<QApplication *>(QCoreApplication::instance()))
        return;
    auto *dialog = new QFileDialog;
    m_dialog = dialog;
    dialog->setWindowTitle(m_title);
    dialog->setObjectName(objectName());
    if (!m_font.family().isEmpty())
        dialog->setFont(m_font);
    dialog->setOptions(QFileDialog::Options(m_options));
    dialog->setFileMode(m_fileMode == OpenFolder  ? QFileDialog::Directory
                        : m_fileMode == OpenFiles ? QFileDialog::ExistingFiles
                        : m_fileMode == SaveFile  ? QFileDialog::AnyFile
                                                  : QFileDialog::ExistingFile);
    dialog->setAcceptMode(m_fileMode == SaveFile ? QFileDialog::AcceptSave
                                                 : QFileDialog::AcceptOpen);
    if (m_fileMode == OpenFolder)
        dialog->setOption(QFileDialog::ShowDirsOnly);
    dialog->setNameFilters(m_nameFilters);
    dialog->setDefaultSuffix(m_fileMode == OpenFolder ? QString{} : m_defaultSuffix);
    if (m_currentFolder.isLocalFile())
        dialog->setDirectoryUrl(m_currentFolder);
    if (!m_currentFile.isEmpty())
        dialog->selectFile(m_currentFile);
    dialog->setWindowModality(m_parentWindow ? Qt::WindowModal : Qt::ApplicationModal);
    // Create the raster HWND before showing the native helper, and establish
    // ownership explicitly for a Qt Quick parent (there is no QWidget parent).
    dialog->winId();
    if (m_parentWindow)
        dialog->windowHandle()->setTransientParent(m_parentWindow);
    // The QFileDialog widget itself never shows (the native helper does), but
    // its Show/Hide events mark the dialog session; forward them as
    // visibleChanged so QML sees the dialog appear and disappear.
    dialog->installEventFilter(this);
    connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
        // A programmatic accept()/reject() before the queued exec starts must
        // not let that exec present the dialog afterwards.
        if (m_cancelled)
            *m_cancelled = true;
        if (m_dialog != dialog)
            return;
        if (result == QDialog::Accepted) {
            m_selected = dialog->selectedUrls();
            emit selectionChanged();
        }
        m_dialog = nullptr;
        m_gateSession = nullptr;
        dialog->deleteLater();
        emit visibleChanged();
        if (result == QDialog::Accepted)
            emit accepted();
        else
            emit rejected();
    });
    if (m_options & DontUseNativeDialog) {
        // The widget fallback is a plain raster window: show() is safe and
        // keeps the loader's asynchronous close semantics.
        dialog->show();
        emit visibleChanged();
        return;
    }
#ifndef Q_OS_WIN
    // The GUI-thread exec workaround is specific to Windows common dialogs.
    // Cocoa sheets and other helpers retain their nonblocking show/close flow.
    dialog->show();
    emit visibleChanged();
    return;
#endif
    // Issue #1 gate: native common dialog initialization raises MSAA events
    // (bridged to UIA by oleacc) before it pumps a message, and while the
    // taskbar lyric probe holds a UIA event subscription that delivery can
    // wedge the dialog thread before Show presents anything. Hold a gate
    // session for the dialog's lifetime: the probe drops its subscription now
    // (bounded wait) and restores it once the dialog has closed.
    m_gateSession = std::make_shared<UiaEventGate::Session>();
    if (!UiaEventGate::waitDrained(1000))
        qWarning("EchoFileDialog: taskbar lyric did not confirm the UIA "
                 "subscription suspension in time; opening the dialog anyway");
    if (qEnvironmentVariableIsSet("ECHO_FILE_DIALOG_LEGACY")) {
        dialog->show();
        emit visibleChanged();
        return;
    }
    // Qt shows non-exec native dialogs on a worker thread. That path used to
    // deadlock behind the taskbar lyric (issue #1: the lyric's UIA event
    // subscription wedged provider-side event raises process-wide, so
    // IFileDialog::Show created the dialog window but never presented it).
    // The lyric side is fixed, but this wrapper keeps the single-apartment
    // GUI-thread exec: queued so open() still returns immediately and no
    // member frame ever sits under the modal loop (a teardown during the loop
    // cannot resume into freed state).
    m_cancelled = std::make_shared<bool>(false);
    m_inExec = true;
    QPointer<EchoFileDialog> guard(this);
    QPointer<QFileDialog> dialogGuard(dialog);
    const auto cancelled = m_cancelled;
    QMetaObject::invokeMethod(
        dialog,
        [this, guard, dialogGuard, cancelled] {
            if (*cancelled || !dialogGuard)
                return;
            dialogGuard->exec();
            if (!guard)
                return;
            m_inExec = false;
        },
        Qt::QueuedConnection);
}
void EchoFileDialog::close()
{
    if (!m_dialog)
        return;
    // Also covers the exec-pending state: reject() before the loop starts
    // emits finished(rejected) and the queued exec then sees the cancel flag.
    if (m_cancelled)
        *m_cancelled = true;
    m_dialog->reject();
}
bool EchoFileDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_dialog && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        emit visibleChanged();
    return QObject::eventFilter(watched, event);
}
} // namespace EchoMusic
