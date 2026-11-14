cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-viewer LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
set(CMAKE_AUTOMOC ON)

add_executable(finn-viewer
    main.cpp
)
target_link_libraries(finn-viewer PRIVATE
    FinnSDK::FinnUI
    Qt6::Svg
)

include(GNUInstallDirs)
install(TARGETS finn-viewer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
