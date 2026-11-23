cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-terminal LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-terminal
    main.cpp
    terminal_view.hpp
    terminal_view.cpp
)

set_source_files_properties(ui/TerminalApp.qml PROPERTIES QT_RESOURCE_ALIAS TerminalApp.qml)
qt_add_qml_module(finn-terminal
    URI FinnTerminal
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/TerminalApp.qml
)

target_include_directories(finn-terminal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-terminal PRIVATE FinnSDK::FinnUIQml)
if(UNIX AND NOT APPLE)
    target_link_libraries(finn-terminal PRIVATE util)
endif()

include(GNUInstallDirs)
install(TARGETS finn-terminal RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
