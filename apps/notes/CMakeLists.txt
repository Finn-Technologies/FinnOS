cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-notes LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
set(CMAKE_AUTOMOC ON)

qt_add_executable(finn-notes
    main.cpp
    notes_controller.hpp
    notes_controller.cpp
)

set_source_files_properties(ui/NotesApp.qml PROPERTIES QT_RESOURCE_ALIAS NotesApp.qml)
qt_add_qml_module(finn-notes
    URI FinnNotes
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/NotesApp.qml
)

target_include_directories(finn-notes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(finn-notes PRIVATE FinnSDK::FinnUIQml)

include(GNUInstallDirs)
install(TARGETS finn-notes RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
