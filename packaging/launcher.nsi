; Built through build-wrapper.ps1, which supplies paths and a content-derived payload ID.
Unicode true
Name "W Vids"
Caption "Preparing W Vids"
OutFile "${OUTPUT_FILE}"
Icon "${APP_ICON}"
RequestExecutionLevel user
SilentInstall silent
AutoCloseWindow true
CRCCheck force
SetCompressor /SOLID lzma
SetDatablockOptimize on
ShowInstDetails nevershow

Var Mutex

Function .onInit
    SetShellVarContext current
    ; Assign this here so command-line /D cannot change the extraction destination.
    StrCpy $INSTDIR "$LOCALAPPDATA\wvids\runtime\${PAYLOAD_ID}"
    ; Test builds use an isolated workspace destination, never the user's real cache.
    !ifdef TEST_CACHE_ROOT
        StrCpy $INSTDIR "${TEST_CACHE_ROOT}\${PAYLOAD_ID}"
    !endif
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\wvids-${PAYLOAD_ID}") p.r0'
    StrCpy $Mutex $0
    StrCmp $Mutex 0 lock_failed
    System::Call 'kernel32::WaitForSingleObject(p r0, i 120000) i.r1'
    StrCmp $1 0 locked
    StrCmp $1 128 locked ; Recover after an interrupted launcher.
lock_failed:
    MessageBox MB_OK|MB_ICONSTOP "W Vids could not prepare its files. Another launch may still be preparing them. Please try again."
    SetErrorLevel 1
    Quit
locked:
FunctionEnd

Section
    IfFileExists "$INSTDIR\.ready" 0 unpack
    ; Repair missing dependencies as well as interrupted extractions.
    !insertmacro CheckPayload
    Goto launch
unpack:
    ClearErrors
    CreateDirectory "$INSTDIR"
    SetOutPath "$INSTDIR"
    Delete "$INSTDIR\.ready"
    ClearErrors
    SetOverwrite on
    File /r "${PAYLOAD_DIR}\*"
    IfErrors unpack_failed
    FileOpen $0 "$INSTDIR\.ready" w
    IfErrors unpack_failed
    FileWrite $0 "${PAYLOAD_ID}"
    FileClose $0
    IfErrors unpack_failed
launch:
    System::Call 'kernel32::ReleaseMutex(p $Mutex)'
    System::Call 'kernel32::CloseHandle(p $Mutex)'
    SetOutPath "$INSTDIR"
    ClearErrors
    Exec '"$INSTDIR\w-vids.exe"'
    IfErrors launch_failed
    SetErrorLevel 0
    Goto done
unpack_failed:
    Delete "$INSTDIR\.ready"
    MessageBox MB_OK|MB_ICONSTOP "W Vids could not unpack its files. Check available disk space and access to your local app-data folder, then try again."
    SetErrorLevel 1
    Quit
launch_failed:
    MessageBox MB_OK|MB_ICONSTOP "W Vids could not start. Please try downloading the application again."
    SetErrorLevel 1
done:
SectionEnd
