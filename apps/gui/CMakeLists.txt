# deox-dm-gui: Qt 6 Widgets front end

set(CMAKE_AUTOMOC ON)

add_library(deoxdm_gui_lib STATIC
    add_dialog.cpp
    add_dialog.hpp
    app_settings.cpp
    app_settings.hpp
    download_model.cpp
    download_model.hpp
    download_queue.cpp
    download_queue.hpp
    download_task.cpp
    download_task.hpp
    icons.cpp
    icons.hpp
    main_window.cpp
    main_window.hpp
    progress_delegate.cpp
    progress_delegate.hpp
    settings_dialog.cpp
    settings_dialog.hpp
)
target_include_directories(deoxdm_gui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(deoxdm_gui_lib
    PUBLIC deoxdm_engine Qt6::Widgets
    PRIVATE deoxdm_warnings
)

add_executable(deox-dm-gui WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(deox-dm-gui PRIVATE deoxdm_gui_lib deoxdm_warnings)

install(TARGETS deox-dm-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)
