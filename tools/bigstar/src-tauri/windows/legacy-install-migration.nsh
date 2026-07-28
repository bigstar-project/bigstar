; Existing v0.9.x installations used this product identity.
!define BIGSTAR_LEGACY_PRODUCT_NAME "NSMB Mario vs Luigi Online"
!define BIGSTAR_LEGACY_MANUFACTURER "melonds"
!define BIGSTAR_LEGACY_UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${BIGSTAR_LEGACY_PRODUCT_NAME}"
!define BIGSTAR_LEGACY_INSTALL_KEY "Software\${BIGSTAR_LEGACY_MANUFACTURER}\${BIGSTAR_LEGACY_PRODUCT_NAME}"

Var BigstarLegacyInstallDir
Var BigstarLegacyUninstallCommand
Var BigstarLegacyMigrationFound
Var BigstarLegacyMigrationExitCode
Var BigstarLegacyStartMenuShortcutFound
Var BigstarLegacyDesktopShortcutFound

!macro BIGSTAR_DETECT_LEGACY_SHORTCUT SHORTCUT_PATH RESULT_VAR
  StrCpy ${RESULT_VAR} 0
  !insertmacro IsShortcutTarget "${SHORTCUT_PATH}" "$BigstarLegacyInstallDir\nsmb-mvl-gui.exe"
  Pop $0
  ${If} $0 = 1
    StrCpy ${RESULT_VAR} 1
  ${EndIf}
!macroend

!macro BIGSTAR_REMOVE_LEGACY_SHORTCUT SHORTCUT_PATH
  !insertmacro IsShortcutTarget "${SHORTCUT_PATH}" "$BigstarLegacyInstallDir\nsmb-mvl-gui.exe"
  Pop $0
  ${If} $0 = 1
    !insertmacro UnpinShortcut "${SHORTCUT_PATH}"
    Delete "${SHORTCUT_PATH}"
  ${EndIf}
!macroend

!macro BIGSTAR_CREATE_MIGRATED_SHORTCUT SHORTCUT_PATH
  CreateShortcut "${SHORTCUT_PATH}" "$INSTDIR\${MAINBINARYNAME}.exe"
  !insertmacro SetLnkAppUserModelId "${SHORTCUT_PATH}"
!macroend

!macro BIGSTAR_MIGRATE_MELONDS_CONFIG SOURCE_DIR
  ${If} ${FileExists} "$INSTDIR\melonDS.toml"
    DetailPrint "Keeping the existing Bigstar melonDS configuration"
  ${ElseIf} ${FileExists} "${SOURCE_DIR}\melonDS.toml"
    ClearErrors
    CopyFiles /SILENT "${SOURCE_DIR}\melonDS.toml" "$INSTDIR"
    ${If} ${Errors}
      DetailPrint "Could not migrate the legacy melonDS configuration from ${SOURCE_DIR}"
    ${Else}
      DetailPrint "Migrated the legacy melonDS configuration from ${SOURCE_DIR}"
    ${EndIf}
  ${EndIf}
!macroend

; Detect the old per-user NSIS installation without changing it. Cleanup is
; intentionally deferred until the new Bigstar Insiders files and registry
; entries have been installed successfully.
!macro NSIS_HOOK_PREINSTALL
  StrCpy $BigstarLegacyInstallDir ""
  StrCpy $BigstarLegacyUninstallCommand ""
  StrCpy $BigstarLegacyMigrationFound 0
  StrCpy $BigstarLegacyStartMenuShortcutFound 0
  StrCpy $BigstarLegacyDesktopShortcutFound 0

  ReadRegStr $BigstarLegacyInstallDir SHCTX "${BIGSTAR_LEGACY_INSTALL_KEY}" ""
  ReadRegStr $BigstarLegacyUninstallCommand SHCTX "${BIGSTAR_LEGACY_UNINSTALL_KEY}" "UninstallString"
  ${If} $BigstarLegacyInstallDir != ""
  ${AndIf} $BigstarLegacyUninstallCommand != ""
  ${AndIf} $BigstarLegacyInstallDir != "$INSTDIR"
  ${AndIf} ${FileExists} "$BigstarLegacyInstallDir\nsmb-mvl-gui.exe"
  ${AndIf} ${FileExists} "$BigstarLegacyInstallDir\uninstall.exe"
    StrCpy $BigstarLegacyMigrationFound 1
    !insertmacro BIGSTAR_DETECT_LEGACY_SHORTCUT "$SMPROGRAMS\${BIGSTAR_LEGACY_PRODUCT_NAME}.lnk" $BigstarLegacyStartMenuShortcutFound
    !insertmacro BIGSTAR_DETECT_LEGACY_SHORTCUT "$DESKTOP\${BIGSTAR_LEGACY_PRODUCT_NAME}.lnk" $BigstarLegacyDesktopShortcutFound
    DetailPrint "Legacy Bigstar installation detected at $BigstarLegacyInstallDir"
  ${EndIf}
!macroend

; `/UPDATE` guarantees that the old uninstaller keeps application data.
; Failure is non-fatal because the new installation is already complete and
; leaving the old installation intact is safer than deleting paths manually.
!macro NSIS_HOOK_POSTINSTALL
  ; Windows melonDS is built in portable mode, so its configuration is stored
  ; next to melonDS.exe instead of in the launcher's app-data directory.
  ; Copy it before running the old uninstaller and never overwrite a config
  ; that the user has already created in Bigstar Insiders.
  ${If} ${FileExists} "$INSTDIR\bigstar.exe"
    ${If} $BigstarLegacyInstallDir != ""
      !insertmacro BIGSTAR_MIGRATE_MELONDS_CONFIG "$BigstarLegacyInstallDir"
    ${EndIf}
    ; The old uninstaller may already be gone after a previous migration
    ; attempt, so also check the standard per-user installation directory.
    !insertmacro BIGSTAR_MIGRATE_MELONDS_CONFIG "$LOCALAPPDATA\${BIGSTAR_LEGACY_PRODUCT_NAME}"
  ${EndIf}

  ${If} $BigstarLegacyMigrationFound = 1
  ${AndIf} ${FileExists} "$INSTDIR\bigstar.exe"
    ClearErrors
    ExecWait '"$BigstarLegacyInstallDir\uninstall.exe" /UPDATE /P _?=$BigstarLegacyInstallDir' $BigstarLegacyMigrationExitCode
    ${If} ${Errors}
      DetailPrint "Legacy Bigstar uninstaller could not be started; the old installation was preserved"
    ${ElseIf} $BigstarLegacyMigrationExitCode <> 0
      DetailPrint "Legacy Bigstar uninstaller exited with $BigstarLegacyMigrationExitCode; the old installation may remain"
    ${Else}
      !insertmacro BIGSTAR_REMOVE_LEGACY_SHORTCUT "$SMPROGRAMS\${BIGSTAR_LEGACY_PRODUCT_NAME}.lnk"
      !insertmacro BIGSTAR_REMOVE_LEGACY_SHORTCUT "$DESKTOP\${BIGSTAR_LEGACY_PRODUCT_NAME}.lnk"
      DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${BIGSTAR_LEGACY_PRODUCT_NAME}"
      DeleteRegKey SHCTX "${BIGSTAR_LEGACY_INSTALL_KEY}"
      DeleteRegKey /ifempty SHCTX "Software\${BIGSTAR_LEGACY_MANUFACTURER}"
      DetailPrint "Legacy Bigstar installation was removed successfully"
    ${EndIf}

    ; Tauri's NSIS updater runs with `/UPDATE`, which intentionally skips
    ; creating new shortcuts. Recreate only the shortcut types that existed
    ; for the old product, even when the old uninstaller could not run.
    ${If} $BigstarLegacyStartMenuShortcutFound = 1
      !insertmacro BIGSTAR_CREATE_MIGRATED_SHORTCUT "$SMPROGRAMS\${PRODUCTNAME}.lnk"
    ${EndIf}
    ${If} $BigstarLegacyDesktopShortcutFound = 1
      !insertmacro BIGSTAR_CREATE_MIGRATED_SHORTCUT "$DESKTOP\${PRODUCTNAME}.lnk"
    ${EndIf}
  ${EndIf}
!macroend
