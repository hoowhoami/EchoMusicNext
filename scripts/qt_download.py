"""Use pinned aqt downloads with the verified Qt 6.12.1 Windows repository layout.

aqtinstall 3.3.0 predates split MSVC x64 repositories. This adapter only supplies
metadata locations. aqt retains TLS, metadata/archive checksums, extraction and
SDK relocation; no Qt source or installed library is patched here.
"""

from importlib.metadata import version
import sys

from qt_sdk import VERSION


def windows_repositories(host, arch):
    if VERSION != "6.12.1":
        raise RuntimeError("Audit the official repository layout before changing Qt")
    if (host, arch) == ("windows", "win64_msvc2022_64"):
        base = "windows_x86/desktop/qt6_6121/qt6_6121_msvc2022_64"
    elif (host, arch) == ("windows_arm64", "win64_msvc2022_arm64"):
        base = "windows_arm64/desktop/qt6_6121/qt6_6121"
    else:
        raise RuntimeError(f"Unsupported Windows kit: {host}/{arch}")
    return tuple("online/qtsdkrepository/" + path for path in (base,))


def main():
    if version("aqtinstall") != "3.3.0":
        raise RuntimeError("Repository adapter requires the audited aqtinstall 3.3.0")
    import aqt
    from aqt import installer
    from aqt.archives import QtArchives, UpdateXmls

    class DesktopArchives(QtArchives):
        def _get_archives(self):
            if self.os_name not in ("windows", "windows_arm64"):
                return super()._get_archives()
            if str(self.version) != VERSION or self.target != "desktop":
                raise RuntimeError("Repository adapter only accepts the pinned desktop SDK")
            updates = [
                UpdateXmls(path, self._download_update_xml(path + "/Updates.xml"))
                for path in windows_repositories(self.os_name, self.arch)
            ]
            self._parse_update_xmls(updates, self._target_packages())

    # Dependency injection is confined to this short-lived installer process.
    # The dependency is version-locked; dry-run checks resolve actual packages
    # for both Windows kits without claiming they run on this macOS machine.
    installer.QtArchives = DesktopArchives
    return aqt.main()


if __name__ == "__main__":
    sys.exit(main())
