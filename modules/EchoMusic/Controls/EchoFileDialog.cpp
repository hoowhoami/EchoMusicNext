#include "EchoFileDialog.h"
#include <QApplication>
#include <QFileDialog>
#include <QWindow>
namespace EchoMusic
{
static_assert(int(EchoFileDialog::DontUseNativeDialog) == int(QFileDialog::DontUseNativeDialog));
EchoFileDialog::EchoFileDialog(QObject *parent) : QObject(parent) {}
EchoFileDialog::~EchoFileDialog()
{
    // A native helper may still have a queued completion. Disconnect before
    // closing; neither it nor the fallback widget survives the QML owner.
    if (m_dialog) {
        m_dialog->disconnect(this);
        delete m_dialog;
    }
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
    dialog->setWindowModality(m_parentWindow ? Qt::WindowModal : Qt::ApplicationModal);
    // Create the raster HWND before showing the native helper, and establish
    // ownership explicitly for a Qt Quick parent (there is no QWidget parent).
    dialog->winId();
    if (m_parentWindow)
        dialog->windowHandle()->setTransientParent(m_parentWindow);
    connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
        if (m_dialog != dialog)
            return;
        if (result == QDialog::Accepted) {
            m_selected = dialog->selectedUrls();
            emit selectionChanged();
        }
        m_dialog = nullptr;
        dialog->deleteLater();
        emit visibleChanged();
        if (result == QDialog::Accepted)
            emit accepted();
        else
            emit rejected();
    });
    dialog->show();
    emit visibleChanged();
}
void EchoFileDialog::close()
{
    if (m_dialog)
        m_dialog->reject();
}
} // namespace EchoMusic
