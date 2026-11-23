cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-files LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-files
    main.cpp
    files_controller.hpp
    files_controller.cpp
)

set_source_files_properties(ui/FilesApp.qml PROPERTIES QT_RESOURCE_ALIAS FilesApp.qml)
qt_add_qml_module(finn-files
    URI FinnFiles
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/FilesApp.qml
)

target_include_directories(finn-files PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-files PRIVATE FinnSDK::FinnUIQml)

include(GNUInstallDirs)
install(TARGETS finn-files RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
