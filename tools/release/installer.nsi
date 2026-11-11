Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WordFunc.nsh"
Name "EchoMusicNext"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\EchoMusicNext"
InstallDirRegKey HKCU "Software\EchoMusicNext" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!define MUI_ABORTWARNING
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\EchoMusicNext.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
Section "EchoMusicNext" Main
  SetShellVarContext current
  SetRegView 32
  ReadRegDWORD $0 HKLM "Software\Microsoft\VisualStudio\14.0\VC\Runtimes\${ARCH}" "Installed"
  ReadRegStr $1 HKLM "Software\Microsoft\VisualStudio\14.0\VC\Runtimes\${ARCH}" "Version"
  StrCpy $2 $1 1
  ${If} $2 == "v"
    StrCpy $1 $1 "" 1
  ${EndIf}
  ${VersionCompare} $1 "${REDIST_VERSION}" $2
  ${If} $0 != 1
  ${OrIf} $2 == 2
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=vc_redist.exe "${REDIST}"
    ClearErrors
    ExecWait '"$PLUGINSDIR\vc_redist.exe" /install /quiet /norestart' $0
    ${If} ${Errors}
      StrCpy $0 -1
    ${EndIf}
    ${If} $0 == 3010
      SetRebootFlag true
    ${ElseIf} $0 != 0
    ${AndIf} $0 != 1638
      MessageBox MB_ICONSTOP "Microsoft Visual C++ 运行库安装未完成（$0），请重试。" /SD IDOK
      SetErrorLevel 1
      Abort
    ${EndIf}
  ${EndIf}
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\EchoMusicNext" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoMusicNext" "DisplayName" "EchoMusicNext"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoMusicNext" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoMusicNext" "Publisher" "hoowhoami"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoMusicNext" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoMusicNext" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoMusicNext" "DisplayIcon" "$INSTDIR\bin\EchoMusicNext.exe"
  WriteRegStr HKCU "Software\Classes\echomusicnext" "" "URL:EchoMusicNext"
  WriteRegStr HKCU "Software\Classes\echomusicnext" "URL Protocol" ""
  WriteRegStr HKCU "Software\Classes\echomusicnext\shell\open\command" "" '$\"$INSTDIR\bin\EchoMusicNext.exe$\" --open-url $\"%1$\"'
  WriteRegStr HKCU "Software\Classes\.echomod" "" "io.github.hoowhoami.echomusicnext.echomod"
  WriteRegStr HKCU "Software\Classes\io.github.hoowhoami.echomusicnext.echomod" "" "EchoMusicNext Plugin"
  WriteRegStr HKCU "Software\Classes\io.github.hoowhoami.echomusicnext.echomod\shell\open\command" "" '$\"$INSTDIR\bin\EchoMusicNext.exe$\" $\"%1$\"'
  CreateDirectory "$SMPROGRAMS\EchoMusicNext"
  CreateShortcut "$SMPROGRAMS\EchoMusicNext\EchoMusicNext.lnk" "$INSTDIR\bin\EchoMusicNext.exe"
  CreateShortcut "$DESKTOP\EchoMusicNext.lnk" "$INSTDIR\bin\EchoMusicNext.exe"
SectionEnd
Section "Uninstall"
  SetShellVarContext current
  Delete "$DESKTOP\EchoMusicNext.lnk"
  RMDir /r "$SMPROGRAMS\EchoMusicNext"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EchoMusicNext"
  DeleteRegKey HKCU "Software\EchoMusicNext"
  DeleteRegKey HKCU "Software\Classes\echomusicnext"
  DeleteRegKey HKCU "Software\Classes\io.github.hoowhoami.echomusicnext.echomod"
  ReadRegStr $0 HKCU "Software\Classes\.echomod" ""
  ${If} $0 == "io.github.hoowhoami.echomusicnext.echomod"
    DeleteRegKey HKCU "Software\Classes\.echomod"
  ${EndIf}
  !include "${UNINSTALL_FILES}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
