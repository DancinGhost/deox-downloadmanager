# SPDX-License-Identifier: MIT
# Installation of everything that is not a program: license texts and
# desktop integration. One subdirectory per platform family; a new port adds
# its own (e.g. packaging/windows/ for an installer, packaging/macos/ for the
# bundle's Info.plist and icon).

# License texts, for binary packages on every platform
install(FILES
    ${PROJECT_SOURCE_DIR}/LICENSE
    ${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# freedesktop.org desktop entry and icon (Linux and BSD desktops)
if(DEOXDM_GUI_ENABLED AND UNIX AND NOT APPLE)
    install(FILES linux/io.github.DancinGhost.DeoxDownloadManager.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/io.github.DancinGhost.DeoxDownloadManager.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
