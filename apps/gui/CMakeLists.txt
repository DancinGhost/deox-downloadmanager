# SPDX-License-Identifier: MIT
# deox-dm-gui: Qt 6 Widgets front end
#
#   model/   application state without widgets: settings, download tasks,
#            the queue and the table model
#   views/   windows, dialogs and painting
#   main.cpp start-up: wires the model to the main window
#
# Includes are written relative to this directory ("model/download_queue.hpp").

set(CMAKE_AUTOMOC ON)

add_library(deoxdm_gui_lib STATIC
    model/app_settings.cpp
    model/app_settings.hpp
    model/download_model.cpp
    model/download_model.hpp
    model/download_queue.cpp
    model/download_queue.hpp
    model/download_task.cpp
    model/download_task.hpp
    views/add_dialog.cpp
    views/add_dialog.hpp
    views/icons.cpp
    views/icons.hpp
    views/main_window.cpp
    views/main_window.hpp
    views/progress_delegate.cpp
    views/progress_delegate.hpp
    views/settings_dialog.cpp
    views/settings_dialog.hpp
)
target_include_directories(deoxdm_gui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(deoxdm_gui_lib
    PUBLIC deoxdm_engine Qt6::Widgets
    PRIVATE deoxdm_warnings
)

# WIN32 makes it a GUI program on Windows (no console window); MACOSX_BUNDLE
# makes it an .app bundle on macOS. Both are ignored elsewhere.
add_executable(deox-dm-gui WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(deox-dm-gui PRIVATE deoxdm_gui_lib deoxdm_warnings)

install(TARGETS deox-dm-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)
