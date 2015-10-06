; HandyFTP NSIS Modern User Interface

;--------------------------------
; Include Modern UI

  !include "MUI2.nsh"

;--------------------------------
; General

  ; Name and file
  Name "HandyFTP"
  OutFile "hftp10b4win.exe"

  ; Default installation folder
  InstallDir "$PROGRAMFILES\HandyFTP"
  
  ; Get installation folder from registry if available
  InstallDirRegKey HKCU "Software\HandyFTP" ""

  ; Request application privileges for Windows Vista
  RequestExecutionLevel admin

;--------------------------------
; Variables

  Var StartMenuFolder

;--------------------------------
; Interface Settings

  !define MUI_ABORTWARNING

;--------------------------------
; Pages

  !insertmacro MUI_PAGE_LICENSE "..\scripts\license.txt"
  ;!insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
  
  ;Start Menu Folder Page Configuration
  !define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU" 
  !define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\HandyFTP" 
  !define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
  
  !insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
  
  !insertmacro MUI_PAGE_INSTFILES
  
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages
 
  !insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Installer Sections

Section "Dummy Section" SecDummy

  SetOutPath "$INSTDIR"
  
  ; Binaries
  File handyftp.exe
  File *.dll
  ; Configuration
  File handyftp.msg
  File handyftp.ini
  File *.typ
  ; Help
  File readme.txt
  File *.html
  ; Images
  CreateDirectory "$INSTDIR\images"
  File /r images
    
  ; Store installation folder
  WriteRegStr HKCU "Software\HandyFTP" "" $INSTDIR
  
  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    
    ; Create shortcuts
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\HandyFTP.lnk" "$INSTDIR\handyftp.exe"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    
    ; Create Uninstall infor 
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HandyFTP" \
                     "DisplayName" "HandyFTP"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\HandyFTP" \
                     "UninstallString" "$\"$INSTDIR\uninstall.exe$\""  
  !insertmacro MUI_STARTMENU_WRITE_END

SectionEnd

;--------------------------------
; Descriptions

  ; Language strings
  ;LangString DESC_SecDummy ${LANG_ENGLISH} "A test section."

  ; Assign language strings to sections
  ;!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  ;  !insertmacro MUI_DESCRIPTION_TEXT ${SecDummy} $(DESC_SecDummy)
  ;!insertmacro MUI_FUNCTION_DESCRIPTION_END
 
;--------------------------------
;Uninstaller Section

Section "Uninstall"

  ; Binaries
  Delete "$INSTDIR\handyftp.exe"
  Delete "$INSTDIR\dw.dll"
  Delete "$INSTDIR\dwcompat.dll"
  ; Configuration
  Delete "$INSTDIR\handyftp.msg"
  Delete "$INSTDIR\handyftp.ini"
  Delete "$INSTDIR\ips.typ"
  Delete "$INSTDIR\unix.typ"
  Delete "$INSTDIR\os2.typ"
  ; Help
  Delete "$INSTDIR\readme.txt"
  Delete "$INSTDIR\*.html"
  ; Images
  Delete "$INSTDIR\images\*"

  Delete "$INSTDIR\Uninstall.exe"

  RMDir "$INSTDIR\images"
  RMDir "$INSTDIR"
  
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    
  Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\HandyFTP.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"
  
  DeleteRegKey /ifempty HKCU "Software\HandyFTP"

SectionEnd