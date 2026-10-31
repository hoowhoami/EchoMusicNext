#include "AppIconShortcuts_p.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif
namespace EchoMusic
{
namespace
{
QString executable()
{
    return QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
}
#ifndef Q_OS_WIN
QString field(const QString &content, const QString &key)
{
    bool main = false;
    for (const auto &line : content.split('\n')) {
        if (line.startsWith('['))
            main = line.trimmed() == "[Desktop Entry]";
        else if (main && line.startsWith(key + '='))
            return line.mid(key.size() + 1).trimmed();
    }
    return {};
}
#endif
} // namespace
QStringList AppIconShortcuts::candidates(const QString &target) const
{
    if (target == "desktop" && !desktopLocations.isEmpty())
        return desktopLocations;
    if (target == "taskbar" && !taskbarLocations.isEmpty())
        return taskbarLocations;
    QStringList roots;
    if (target == "desktop")
        roots << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
#ifdef Q_OS_WIN
    else {
        PWSTR roaming = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
            roots << QString::fromWCharArray(roaming) +
                         "/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar";
            CoTaskMemFree(roaming);
        }
    }
    const QStringList filters{"*.lnk"};
#else
    const QStringList filters{QStringLiteral(ECHO_APPLICATION_ID) + ".desktop",
                              "echomusicnext.desktop", "EchoMusicNext.desktop"};
#endif
    QStringList out;
    for (const auto &root : roots)
        if (!root.isEmpty()) {
            QDir dir(root);
            // Windows 上 Qt 将 .lnk 快捷方式视为符号链接（isSymLink() 对
            // .lnk 返回 true），NoSymLinks 会排除全部候选。
            const auto entries = dir.entryList(filters, QDir::Files);
            for (const auto &entry : entries.mid(0, 512))
                out << dir.filePath(entry);
        }
    return out;
}
QString AppIconShortcuts::currentIcon(const QString &path, int *index, QString &error) const
{
#ifdef Q_OS_WIN
    const auto init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) {
        error = "Cannot initialize shortcut COM worker";
        return {};
    }
    const auto uninit = qScopeGuard([] { CoUninitialize(); });
    IShellLinkW *link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) {
        error = "Cannot open shortcut";
        return {};
    }
    const auto release = qScopeGuard([link] { link->Release(); });
    IPersistFile *file = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        error = "Cannot access shortcut file";
        return {};
    }
    const auto releaseFile = qScopeGuard([file] { file->Release(); });
    wchar_t target[32768]{}, icon[32768]{};
    if (FAILED(file->Load(reinterpret_cast<LPCOLESTR>(path.utf16()), STGM_READ)) ||
        FAILED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH))) {
        error = "Shortcut does not belong to this executable";
        return {};
    }
    // 归属判定按可执行文件名而不是完整路径：固定任务栏/桌面快捷方式可以
    // 指向同应用的另一份安装（固定副本指向已安装目录而当前从开发构建
    // 运行），完整路径全等会让改写在那种环境里永远失败。
    const QFileInfo targetInfo(QString::fromWCharArray(target));
    if (targetInfo.canonicalFilePath().isEmpty() ||
        targetInfo.fileName().compare(QFileInfo(executable()).fileName(), Qt::CaseInsensitive) !=
            0) {
        error = "Shortcut does not belong to this executable";
        return {};
    }
    if (FAILED(link->GetIconLocation(icon, 32768, index))) {
        error = "Cannot read shortcut icon";
        return {};
    }
    return QString::fromWCharArray(icon);
#else
    Q_UNUSED(index);
    QFile file(path);
    if (QFileInfo(path).isSymLink() || file.size() > 1024 * 1024 ||
        !file.open(QIODevice::ReadOnly)) {
        error = "Cannot read desktop entry";
        return {};
    }
    const auto content = QString::fromUtf8(file.readAll());
    const auto command = field(content, "Exec");
    if (!command.startsWith(executable()) && !command.startsWith('"' + executable() + '"') &&
        !command.startsWith("echomusicnext ") && command != "echomusicnext") {
        error = "Desktop entry does not belong to this executable";
        return {};
    }
    return field(content, "Icon");
#endif
}
bool AppIconShortcuts::update(const QString &path, const QString &icon, QString *previous,
                              int *index, QString &error, int newIndex)
{
    const auto old = currentIcon(path, index, error);
    if (!error.isEmpty())
        return false;
    if (previous)
        *previous = old;
#ifdef Q_OS_WIN
    const auto init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) {
        error = "Cannot initialize COM";
        return false;
    }
    const auto uninit = qScopeGuard([] { CoUninitialize(); });
    IShellLinkW *link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) {
        error = "Cannot open shortcut";
        return false;
    }
    const auto release = qScopeGuard([link] { link->Release(); });
    IPersistFile *file = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        error = "Cannot access shortcut";
        return false;
    }
    const auto releaseFile = qScopeGuard([file] { file->Release(); });
    if (FAILED(file->Load(reinterpret_cast<LPCOLESTR>(path.utf16()), STGM_READWRITE)) ||
        FAILED(link->SetIconLocation(reinterpret_cast<LPCWSTR>(icon.utf16()), newIndex)) ||
        FAILED(file->Save(nullptr, TRUE))) {
        error = "Cannot update shortcut icon";
        return false;
    }
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, path.utf16(), nullptr);
#else
    Q_UNUSED(newIndex);
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
        error = "Cannot read desktop entry";
        return false;
    }
    const auto permissions = source.permissions();
    auto lines = QString::fromUtf8(source.readAll()).split('\n');
    source.close();
    bool main = false, replaced = false;
    for (auto &line : lines) {
        if (line.startsWith('['))
            main = line.trimmed() == "[Desktop Entry]";
        else if (main && line.startsWith("Icon=")) {
            line = "Icon=" + icon;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        const auto at = lines.indexOf("[Desktop Entry]");
        if (at < 0) {
            error = "Invalid desktop entry";
            return false;
        }
        lines.insert(at + 1, "Icon=" + icon);
    }
    QSaveFile output(path);
    const auto bytes = lines.join('\n').toUtf8();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit()) {
        error = "Cannot update desktop entry";
        return false;
    }
    QFile::setPermissions(path, permissions);
#endif
    return true;
}
QVariantMap AppIconShortcuts::apply(const QHash<QString, QString> &paths)
{
    QHash<QString, QString> desired, targets;
    for (auto it = paths.begin(); it != paths.end(); ++it) {
        bool found = false;
        for (const auto &path : candidates(it.key())) {
            QString error;
            int index = 0;
            currentIcon(path, &index, error);
            if (error.isEmpty()) {
                desired[path] = it.value();
                targets[path] = it.key();
                found = true;
            }
        }
        if (!found)
            return {{"error", "No matching application shortcut: " + it.key()},
                    {"target", it.key()}};
    }
    const auto previous = m_previous;
    QHash<QString, QString> before;
    QString error, failedTarget;
    for (auto it = desired.begin(); it != desired.end(); ++it) {
        auto old = m_previous.value(it.key());
        int index = 0;
        QString current;
        if (!update(it.key(), it.value(), &current, &index, error)) {
            failedTarget = targets.value(it.key());
            break;
        }
        before[it.key()] = current;
        if (!m_previous.contains(it.key()) || current != old.applied) {
            old.icon = current;
            old.index = index;
        }
        old.applied = it.value();
        m_previous[it.key()] = old;
    }
    if (!error.isEmpty()) {
        for (auto it = before.begin(); it != before.end(); ++it) {
            QString ignored;
            int index = 0;
            update(it.key(), it.value(), nullptr, &index, ignored);
        }
        m_previous = previous;
        return {{"error", error}, {"target", failedTarget}};
    }
    bool restored = false;
    for (const auto &path : m_previous.keys())
        if (!desired.contains(path)) {
            const auto old = m_previous.value(path);
            int index = 0;
            QString problem;
            const auto current = currentIcon(path, &index, problem);
            if (problem.isEmpty() && current == old.applied) {
                index = old.index;
                if (update(path, old.icon, nullptr, &index, problem, old.index))
                    restored = true;
            }
            if (problem.isEmpty() || !QFileInfo::exists(path))
                m_previous.remove(path);
            else
                error = problem;
        }
#ifdef Q_OS_WIN
    // SHCNE_UPDATEITEM 不足以刷新 AUMID 固定按钮的图标缓存；与旧版工具
    // 一致地广播全局关联变更并要求刷新，固定按钮即时重读图标。清除最后
    // 一个目标时 desired 为空但发生过还原，同样必须广播。
    if (!desired.isEmpty() || restored)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
#endif
    return {{"applied", true}, {"error", error}};
}
QVariantMap AppIconShortcuts::restoreDefault(const QString &target,
                                             std::shared_ptr<std::atomic_bool> canceled)
{
    QStringList entries = candidates(target);
    bool restored = false;
    for (const auto &path : entries) {
        if (canceled && canceled->load())
            return {{"canceled", true}};
#ifdef Q_OS_WIN
        const auto icon = QCoreApplication::applicationFilePath();
#else
        const auto icon = QStringLiteral(ECHO_APPLICATION_ID);
#endif
        QString error;
        int index = 0;
        if (update(path, icon, nullptr, &index, error)) {
            restored = true;
            m_previous.remove(path);
        }
    }
    return restored ? QVariantMap{{"applied", true}}
                    : QVariantMap{{"error", "No writable application shortcut found"}};
}
} // namespace EchoMusic
