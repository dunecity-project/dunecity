# Shared by the game and the lightweight Windows installer preflight.
set(CPACK_GENERATOR "NSIS;ZIP")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_LIST_DIR}/../dunecity.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_LIST_DIR}/../dunecity.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "dunecity.exe")
set(CPACK_NSIS_DISPLAY_NAME "Dune City")
set(CPACK_NSIS_PACKAGE_NAME "Dune City")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DuneCity")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "DuneCity")
set(CPACK_PACKAGE_FILE_NAME "DuneCity-${PROJECT_VERSION}-Windows-x64")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "dunecity.exe")

# Bracket quoting preserves NSIS variables and Windows paths in CPack's
# generated configuration. Updating never uninstalls user content first.
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\dunecity.exe" "" "$INSTDIR\dunecity.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\dunecity.exe" "Path" "$INSTDIR"
    FileOpen $0 "$INSTDIR\dunecity-installed.txt" w
    FileWrite $0 "Dune City installer-managed installation"
    FileClose $0
]=])
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\dunecity.exe"
    Delete "$INSTDIR\dunecity-installed.txt"
]=])
set(CPACK_PACKAGE_EXECUTABLES "dunecity;Dune City")
# CPack's own desktop links (CPACK_CREATE_DESKTOP_LINKS) are created only from the PATH
# options page, which this installer does not show (CPACK_NSIS_MODIFY_PATH OFF), so they
# would never appear. Create the desktop shortcut beside the Start Menu shortcut instead:
# the Start Menu page's "Do not create shortcuts" box then governs both.
set(CPACK_NSIS_CREATE_ICONS_EXTRA [=[
    CreateShortCut "$DESKTOP\Dune City.lnk" "$INSTDIR\dunecity.exe"
]=])
set(CPACK_NSIS_DELETE_ICONS_EXTRA [=[
    Delete "$DESKTOP\Dune City.lnk"
]=])

