cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-capture LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-capture
    main.cpp
    capture_controller.hpp
    capture_controller.cpp
)

set_source_files_properties(ui/CaptureApp.qml PROPERTIES QT_RESOURCE_ALIAS CaptureApp.qml)
qt_add_qml_module(finn-capture
    URI FinnCapture
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/CaptureApp.qml
)

target_include_directories(finn-capture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-capture PRIVATE
    FinnSDK::FinnUIQml
    Qt6::Svg
)

include(GNUInstallDirs)
install(TARGETS finn-capture RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
