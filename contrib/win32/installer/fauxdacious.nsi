; fauxdacious.nsi

; WARNING:  THIS IS UNTESTED!

; Copyright 2013-2016 Carlo Bramini, John Lindgren and Jim Turner
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice,
;    this list of conditions, and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions, and the following disclaimer in the documentation
;    provided with the distribution.
;
; This software is provided "as is" and without any warranty, express or
; implied. In no event shall the authors be liable for any damages arising from
; the use of this software.

; Imports
!include "MUI2.nsh"

; Version
!define VERSION "4.0.0"

; Program name
Name "Fauxdacious ${VERSION}"

; Installer file name
OutFile "fauxdacious-${VERSION}-win32.exe"

; Installer icon
!define MUI_ICON "fauxdacious.ico"
!define MUI_UNICON "fauxdacious.ico"

; Installer options
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; Default installation directory
InstallDir "$PROGRAMFILES\Fauxdacious"

; Registry uninstall key
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Fauxdacious"

; Path to uninstaller
!define UNINSTALLER "$INSTDIR\uninstall.exe"

; Installer pages
!insertmacro MUI_PAGE_LICENSE README.txt
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

Section "Fauxdacious" InstallSection
  SectionIn 1 2 RO

  SetOutPath "$INSTDIR"
  File "README.txt"

  SetOutPath "$INSTDIR\bin"
  File /r "bin\*.*"

  SetOutPath "$INSTDIR\etc"
  File /r "etc\*.*"

  SetOutPath "$INSTDIR\lib"
  File /r "lib\*.*"

  SetOutPath "$INSTDIR\share"
  File /r "share\*.*"

  ; create uninstaller
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "Fauxdacious"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Jim Turner"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "${UNINSTALLER}"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "${UNINSTALLER}"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

  ; estimate installed size
  SectionGetSize InstallSection $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0

  WriteUninstaller ${UNINSTALLER}

SectionEnd

; Optional sections
Section "Add to Start Menu" StartMenuSection
  SectionIn 1 2

  SetShellVarContext all
  SetOutPath "$INSTDIR\bin" ; sets the shortcut's working directory
  CreateShortCut "$SMPROGRAMS\Fauxdacious.lnk" "$INSTDIR\bin\fauxdacious.exe"

SectionEnd

Section "Add to Desktop" DesktopSection
  SectionIn 1 2

  SetShellVarContext all
  SetOutPath "$INSTDIR\bin" ; sets the shortcut's working directory
  CreateShortCut "$DESKTOP\Fauxdacious.lnk" "$INSTDIR\bin\fauxdacious.exe"

SectionEnd

Section "Uninstall" UninstallSection

  RMDir /r "$INSTDIR"

  SetShellVarContext all
  Delete "$SMPROGRAMS\Fauxdacious.lnk"
  Delete "$DESKTOP\Fauxdacious.lnk"

  DeleteRegKey HKLM "${UNINST_KEY}"

SectionEnd
