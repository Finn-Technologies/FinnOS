cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-viewer LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-viewer
    main.cpp
    viewer_controller.hpp
    viewer_controller.cpp
)

set_source_files_properties(ui/ViewerApp.qml PROPERTIES QT_RESOURCE_ALIAS ViewerApp.qml)
qt_add_qml_module(finn-viewer
    URI FinnViewer
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/ViewerApp.qml
)

target_include_directories(finn-viewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-viewer PRIVATE
    FinnSDK::FinnUIQml
    Qt6::Svg
)

include(GNUInstallDirs)
install(TARGETS finn-viewer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
