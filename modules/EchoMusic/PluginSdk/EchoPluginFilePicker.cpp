#include "EchoPluginFilePicker.h"
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
    : QObject(parent)
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
        if (m_dialog)
            emit selected(m_dialog->selectedFiles());
        deleteLater();
    });
    connect(m_dialog, &QFileDialog::rejected, this, [this] {
        emit rejected();
        deleteLater();
    });
}
EchoPluginFilePicker::~EchoPluginFilePicker()
{
    if (m_dialog) {
        m_dialog->disconnect(this);
        delete m_dialog.data();
    }
}
void EchoPluginFilePicker::open()
{
    if (!m_dialog)
        return;
    if (auto *parent = QGuiApplication::focusWindow()) {
        m_dialog->winId();
        m_dialog->windowHandle()->setTransientParent(parent);
    }
    m_dialog->open();
}
void EchoPluginFilePicker::cancel()
{
    if (m_dialog) {
        m_dialog->disconnect(this);
        m_dialog->close();
        delete m_dialog.data();
        m_dialog = nullptr;
    }
    deleteLater();
}
} // namespace EchoMusic
