#include "EchoPluginFilePicker.h"
#include "UiaEventGate.h"
#include <QFileDialog>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QWindow>
namespace EchoMusic
{
namespace
{
QPointer<QFileDialog> currentDialog;
}
bool EchoPluginFilePicker::busy()
{
    return currentDialog;
}
QString EchoPluginFilePicker::validate(bool directory, const QVariantMap &options)
{
    if (options.value("purpose").toString().trimmed().isEmpty() ||
        options.value("purpose").toString().size() > 512 ||
        !QStringList{"read", "read-write"}.contains(options.value("access", "read").toString()) ||
        options.value("defaultPath").toString().size() > 4096)
        return "A purpose and valid access mode are required";
    const auto filters = options.value("filters").toList();
    if (filters.size() > 16 || (directory && !filters.isEmpty()))
        return "Invalid file filters";
    for (const auto &entry : filters) {
        const auto filter = entry.toMap();
        const auto extensions = filter.value("extensions").toStringList();
        if (filter.value("name").toString().size() > 100 || extensions.isEmpty() ||
            extensions.size() > 32)
            return "Invalid file filter";
        for (const auto &extension : extensions)
            if (!QRegularExpression("^(\\*|[A-Za-z0-9][A-Za-z0-9._-]{0,31})$")
                     .match(extension)
                     .hasMatch())
                return "Invalid file extension";
    }
    return {};
}
EchoPluginFilePicker::EchoPluginFilePicker(const QString &name, bool directory,
                                           const QVariantMap &options, QObject *parent)
    : QObject(parent), m_directory(directory)
{
    m_dialog = new QFileDialog();
    currentDialog = m_dialog;
    m_dialog->setObjectName("pluginNativeFileDialog");
    m_dialog->setWindowTitle(
        name + " · " + options.value("purpose").toString() +
        (options.value("access", "read") == "read-write" ? tr("（读写）") : tr("（只读）")));
    m_dialog->setFileMode(directory                            ? QFileDialog::Directory
                          : options.value("multiple").toBool() ? QFileDialog::ExistingFiles
                                                               : QFileDialog::ExistingFile);
    m_dialog->setOption(QFileDialog::ShowDirsOnly, directory);
    m_dialog->setSupportedSchemes({"file"});
    m_dialog->setWindowModality(Qt::ApplicationModal);
    if (!options.value("defaultPath").toString().isEmpty())
        m_dialog->setDirectory(options.value("defaultPath").toString());
    QStringList filters;
    for (const auto &entry : options.value("filters").toList()) {
        const auto filter = entry.toMap();
        QStringList patterns;
        for (const auto &extension : filter.value("extensions").toStringList())
            patterns.append(extension == "*" ? "*" : "*." + extension);
        filters.append(filter.value("name").toString() + " (" + patterns.join(' ') + ')');
    }
    if (!filters.isEmpty())
        m_dialog->setNameFilters(filters);
    connect(m_dialog, &QFileDialog::accepted, this, [this] {
        if (!m_dialog)
            return;
        auto files = m_dialog->selectedFiles();
        // A programmatic accept before the exec presented the dialog has no
        // native selection state; a directory pick then means the folder the
        // dialog was opened at (what an interactive accept would return).
        if (files.isEmpty() && !m_presented && m_directory)
            files.append(m_dialog->directoryUrl().toLocalFile());
        emit selected(files);
        deleteLater();
    });
    connect(m_dialog, &QFileDialog::rejected, this, [this] {
        emit rejected();
        deleteLater();
    });
    // A programmatic accept()/reject() before the queued exec starts must not
    // let that exec present the dialog afterwards.
    connect(m_dialog, &QDialog::finished, this, [this] {
        if (m_cancelled)
            *m_cancelled = true;
    });
}
EchoPluginFilePicker::~EchoPluginFilePicker()
{
    if (!m_dialog)
        return;
    m_dialog->disconnect(this);
    if (m_inExec) {
        // Teardown landed inside the queued exec frame. Reject (the nested
        // loop pumps the close through) and defer the deletion until that
        // frame has returned; deleting now would free the dialog under
        // IFileDialog::Show.
        if (m_cancelled)
            *m_cancelled = true;
        if (m_dialog->isVisible())
            m_dialog->reject();
        m_dialog->deleteLater();
        return;
    }
    delete m_dialog.data();
}
void EchoPluginFilePicker::open()
{
    if (!m_dialog)
        return;
    if (auto *parent = QGuiApplication::focusWindow()) {
        m_dialog->winId();
        m_dialog->windowHandle()->setTransientParent(parent);
    }
#ifndef Q_OS_WIN
    // Keep non-Windows pickers asynchronous; the common-dialog/UIA workaround
    // must not place a Cocoa or widget caller under a nested exec frame.
    m_presented = true;
    m_dialog->show();
    return;
#endif
    // Issue #1 gate: hold it before any native presentation (see
    // UiaEventGate.h); released when the picker deletes itself with the
    // dialog.
    m_gate = std::make_unique<UiaEventGate::Session>();
    if (!UiaEventGate::waitDrained(1000))
        qWarning("EchoPluginFilePicker: taskbar lyric did not confirm the UIA "
                 "subscription suspension in time; opening the dialog anyway");
    // Qt shows non-exec native dialogs on a worker thread. That path used to
    // deadlock behind the taskbar lyric (issue #1, fixed at the source: the
    // lyric's UIA event subscription wedged provider-side event raises). Run
    // the native dialog as a GUI-thread exec(), queued so open() still
    // returns immediately and no member frame sits under the modal loop.
    // Issue #1 diagnostic/regression switch: ECHO_FILE_DIALOG_LEGACY=1 keeps
    // the worker-thread path reachable for regression verification.
    if (qEnvironmentVariableIsSet("ECHO_FILE_DIALOG_LEGACY")) {
        m_presented = true;
        m_dialog->show();
        return;
    }
    m_cancelled = std::make_shared<bool>(false);
    m_inExec = true;
    QPointer<EchoPluginFilePicker> guard(this);
    QPointer<QFileDialog> dialogGuard(m_dialog.data());
    const auto cancelled = m_cancelled;
    QMetaObject::invokeMethod(
        m_dialog.data(),
        [this, guard, dialogGuard, cancelled] {
            if (*cancelled || !dialogGuard)
                return;
            if (guard)
                m_presented = true;
            dialogGuard->exec();
            if (!guard)
                return;
            m_inExec = false;
        },
        Qt::QueuedConnection);
}
void EchoPluginFilePicker::cancel()
{
    if (m_dialog) {
        m_dialog->disconnect(this);
        if (m_cancelled)
            *m_cancelled = true;
        if (m_inExec) {
            if (m_dialog->isVisible())
                m_dialog->reject();
            m_dialog->deleteLater();
        } else {
            m_dialog->close();
            delete m_dialog.data();
        }
        m_dialog = nullptr;
    }
    deleteLater();
}
} // namespace EchoMusic
