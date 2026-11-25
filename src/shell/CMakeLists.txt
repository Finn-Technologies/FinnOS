cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-shell VERSION 1.0.0 LANGUAGES CXX C)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Svg)
find_package(Qt6 COMPONENTS DBus Network QUIET)

set(CMAKE_AUTOMOC ON)

set(SHELL_QML_FILES
    ui/SystemUI.qml
    ui/common/WallpaperLayer.qml
    ui/common/NotificationStack.qml
    ui/common/LockScreenOverlay.qml
    ui/desktop/DesktopShell.qml
    ui/desktop/DesktopDock.qml
    ui/desktop/HomeMenuPopover.qml
    ui/desktop/SpotlightPopover.qml
    ui/desktop/ControlCenterPopover.qml
    ui/desktop/NotificationCenterPopover.qml
    ui/desktop/OverviewPopover.qml
    ui/tablet/TabletShell.qml
    ui/mobile/MobileShell.qml
    ui/tv/TvShell.qml
)

foreach(_qml ${SHELL_QML_FILES})
    get_filename_component(_alias "${_qml}" NAME)
    set_source_files_properties("${_qml}" PROPERTIES QT_RESOURCE_ALIAS "${_alias}")
endforeach()

qt_add_executable(finn-shell
    main.cpp
    core/shell_corona.cpp
    core/shell_corona.hpp
    search.cpp
    search.hpp
    ../session/app_registry.cpp
    ../compat/android_runner.cpp
    ../compat/appimage_runner.cpp
    ../compat/desktop_entry.cpp
    ../compat/linux_sandbox.cpp
    ../compat/macos_runner.cpp
    ../compat/ohos_runner.cpp
    ../compat/pe_icon.cpp
    ../compat/wine_prefix.cpp
    ../compat/wine_runner.cpp
    ../compat/windows_registry.cpp
)

qt_add_qml_module(finn-shell
    URI FinnShell
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ${SHELL_QML_FILES}
)

target_include_directories(finn-shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(finn-shell PRIVATE -Wno-missing-field-initializers)
endif()

target_link_libraries(finn-shell PRIVATE
    FinnSDK::FinnUIQml
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg
)

if(Qt6DBus_FOUND)
    target_compile_definitions(finn-shell PRIVATE FINN_HAVE_QT_DBUS=1)
    target_link_libraries(finn-shell PRIVATE Qt6::DBus)
endif()
if(Qt6Network_FOUND)
    target_link_libraries(finn-shell PRIVATE Qt6::Network)
endif()

include(GNUInstallDirs)
install(TARGETS finn-shell RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
