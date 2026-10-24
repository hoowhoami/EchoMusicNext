#include "WindowsAppIdentity.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif
namespace EchoMusic
{
QString WindowsAppIdentity::appUserModelId()
{
    return QStringLiteral(ECHO_APPLICATION_ID);
}
void WindowsAppIdentity::publish()
{
#ifdef Q_OS_WIN
    // The shell copies the string, so the temporary wide string's lifetime ends
    // with this call.
    const auto aumid = appUserModelId().toStdWString();
    SetCurrentProcessExplicitAppUserModelID(aumid.c_str());
#endif
}
#ifdef Q_OS_WIN
namespace
{
/// CoInitializeEx is per-thread and reference counted. Qt's Windows plugin has
/// usually initialized the GUI thread already; then this increments the count
/// and the destructor decrements it again, which is the documented balance.
class ScopedCom final
{
public:
    ScopedCom()
        : m_ownsCall(
              SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
    {
    }
    ~ScopedCom()
    {
        if (m_ownsCall)
            CoUninitialize();
    }
    ScopedCom(const ScopedCom &) = delete;
    ScopedCom &operator=(const ScopedCom &) = delete;

private:
    bool m_ownsCall;
};
/// True when the shortcut already points at this executable and carries the
/// shell id; rewriting it every launch would make the Start Menu reindex for no
/// reason.
bool shortcutMatches(const QString &shortcutPath, const QString &targetPath)
{
    const auto nativeShortcut = QDir::toNativeSeparators(shortcutPath).toStdWString();
    const auto expectedTarget = QDir::toNativeSeparators(targetPath);
    IShellLinkW *link = nullptr;
    if (FAILED(
            CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return false;
    IPersistFile *file = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        link->Release();
        return false;
    }
    bool matches = false;
    if (SUCCEEDED(file->Load(nativeShortcut.c_str(), STGM_READ))) {
        wchar_t target[MAX_PATH]{};
        WIN32_FIND_DATAW data{};
        if (SUCCEEDED(link->GetPath(target, MAX_PATH, &data, SLGP_RAWPATH)))
            matches =
                QString::fromWCharArray(target).compare(expectedTarget, Qt::CaseInsensitive) == 0;
    }
    if (matches) {
        IPropertyStore *store = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&store)))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            matches = SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value));
            if (matches) {
                PWSTR text = nullptr;
                matches = SUCCEEDED(PropVariantToStringAlloc(value, &text)) && text &&
                          WindowsAppIdentity::appUserModelId() == QString::fromWCharArray(text);
                if (text)
                    CoTaskMemFree(text);
            }
            PropVariantClear(&value);
            store->Release();
        } else {
            matches = false;
        }
    }
    file->Release();
    link->Release();
    return matches;
}
} // namespace
#endif
bool WindowsAppIdentity::ensureStartMenuShortcut()
{
#ifdef Q_OS_WIN
    const auto target = QCoreApplication::applicationFilePath();
    const auto programs = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (target.isEmpty() || programs.isEmpty())
        return false;
    // Same folder and file name the installer uses, so an installed copy keeps
    // one Start Menu entry and this only repairs a missing or stale one.
    const auto folder = QDir(programs).filePath(QStringLiteral("EchoMusicNext"));
    const auto shortcut = QDir(folder).filePath(QStringLiteral("EchoMusicNext.lnk"));
    ScopedCom com;
    if (QFileInfo::exists(shortcut) && shortcutMatches(shortcut, target))
        return true;
    if (!QDir().mkpath(folder))
        return false;
    return writeShortcut(shortcut, target, appUserModelId());
#else
    return false;
#endif
}
bool WindowsAppIdentity::writeShortcut(const QString &shortcutPath, const QString &targetPath,
                                       const QString &appUserModelId)
{
#ifdef Q_OS_WIN
    const auto nativeShortcut = QDir::toNativeSeparators(shortcutPath).toStdWString();
    const auto nativeTarget = QDir::toNativeSeparators(targetPath).toStdWString();
    const auto nativeWorkingDirectory =
        QDir::toNativeSeparators(QFileInfo(targetPath).absolutePath()).toStdWString();
    const auto nativeAumid = appUserModelId.toStdWString();
    ScopedCom com;
    IShellLinkW *link = nullptr;
    if (FAILED(
            CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return false;
    link->SetPath(nativeTarget.c_str());
    link->SetWorkingDirectory(nativeWorkingDirectory.c_str());
    link->SetIconLocation(nativeTarget.c_str(), 0);
    link->SetDescription(L"EchoMusicNext");
    bool written = false;
    IPropertyStore *store = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&store)))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(InitPropVariantFromString(nativeAumid.c_str(), &value))) {
            written = SUCCEEDED(store->SetValue(PKEY_AppUserModel_ID, value)) &&
                      SUCCEEDED(store->Commit());
            PropVariantClear(&value);
        }
        store->Release();
    }
    IPersistFile *file = nullptr;
    if (written && SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        written = SUCCEEDED(file->Save(nativeShortcut.c_str(), TRUE));
        file->Release();
    } else {
        written = false;
    }
    link->Release();
    return written;
#else
    Q_UNUSED(shortcutPath);
    Q_UNUSED(targetPath);
    Q_UNUSED(appUserModelId);
    return false;
#endif
}
} // namespace EchoMusic
