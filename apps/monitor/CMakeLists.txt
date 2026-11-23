cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-monitor LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-monitor
    main.cpp
    monitor_controller.hpp
    monitor_controller.cpp
    ../../src/system/sys_files.cpp
)

set_source_files_properties(ui/MonitorApp.qml PROPERTIES QT_RESOURCE_ALIAS MonitorApp.qml)
qt_add_qml_module(finn-monitor
    URI FinnMonitor
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/MonitorApp.qml
)

target_include_directories(finn-monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-monitor PRIVATE FinnSDK::FinnUIQml)

include(GNUInstallDirs)
install(TARGETS finn-monitor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
