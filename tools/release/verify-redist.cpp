#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <winver.h>
#include <softpub.h>

#include <cstdio>
#include <cwchar>
#include <exception>
#include <iterator>
#include <vector>

namespace
{
int verify(const wchar_t *file)
{
    // Keep the verified file immutable while checking its signer and version.
    struct FileHandle
    {
        HANDLE value;
        ~FileHandle()
        {
            if (value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    } handle{CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "Cannot open runtime file (Win32 error %lu)\n", GetLastError());
        return 1;
    }

    WINTRUST_FILE_INFO info{};
    info.cbStruct = sizeof(info);
    info.pcwszFilePath = file;
    info.hFile = handle.value;
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &info;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status =
        WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust);

    bool microsoft = false;
    if (status == ERROR_SUCCESS) {
        // Inspect the leaf of the verified primary signer, never a certificate
        // merely extracted from the file or its timestamp countersignature.
        const auto module = GetModuleHandleW(L"wintrust.dll");
        const auto providerData = reinterpret_cast<decltype(&WTHelperProvDataFromStateData)>(
            GetProcAddress(module, "WTHelperProvDataFromStateData"));
        const auto providerSigner = reinterpret_cast<decltype(&WTHelperGetProvSignerFromChain)>(
            GetProcAddress(module, "WTHelperGetProvSignerFromChain"));
        const auto data = providerData ? providerData(trust.hWVTStateData) : nullptr;
        const auto signer = data && providerSigner ? providerSigner(data, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain && signer->pasCertChain &&
            signer->pasCertChain[0].pCert) {
            wchar_t name[128]{};
            const DWORD length = CertGetNameStringW(
                signer->pasCertChain[0].pCert, CERT_NAME_ATTR_TYPE, 0,
                const_cast<char *>(szOID_COMMON_NAME), name, DWORD(std::size(name)));
            microsoft = length > 1 && length < std::size(name) &&
                        std::wcscmp(name, L"Microsoft Corporation") == 0;
        }
    }
    // WinTrust state must be closed after both successful and failed checks.
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trust);
    if (status != ERROR_SUCCESS) {
        std::fprintf(stderr, "Runtime Authenticode verification failed (0x%08lx)\n",
                     static_cast<unsigned long>(status));
        return 1;
    }
    if (!microsoft) {
        std::fprintf(stderr, "Runtime signer must be Microsoft Corporation\n");
        return 1;
    }

    const DWORD size = GetFileVersionInfoSizeW(file, nullptr);
    std::vector<BYTE> version(size);
    VS_FIXEDFILEINFO *fixed = nullptr;
    UINT fixedSize = 0;
    if (!size || !GetFileVersionInfoW(file, 0, size, version.data()) ||
        !VerQueryValueW(version.data(), L"\\", reinterpret_cast<void **>(&fixed), &fixedSize) ||
        !fixed || fixedSize < sizeof(*fixed) || fixed->dwSignature != VS_FFI_SIGNATURE ||
        HIWORD(fixed->dwFileVersionMS) != 14) {
        std::fprintf(stderr, "Invalid Microsoft VC14 runtime file version\n");
        return 1;
    }
    std::printf("%u.%u.%u.%u\n", unsigned(HIWORD(fixed->dwFileVersionMS)),
                unsigned(LOWORD(fixed->dwFileVersionMS)), unsigned(HIWORD(fixed->dwFileVersionLS)),
                unsigned(LOWORD(fixed->dwFileVersionLS)));
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "Usage: verify-redist.exe <vc_redist.exe>\n");
        return 1;
    }
    try {
        return verify(argv[1]);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Runtime verification failed: %s\n", error.what());
        return 1;
    }
}
