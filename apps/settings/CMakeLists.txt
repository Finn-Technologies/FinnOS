cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-settings LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Network)
set(CMAKE_AUTOMOC ON)

add_executable(finn-settings
    main.cpp
    ../../src/system/desktop_services.cpp
    ../../src/system/desktop_services.hpp
    ../../src/compat/android_runner.cpp
    ../../src/compat/desktop_entry.cpp
    ../../src/compat/wine_runner.cpp
    ../../src/compat/wine_prefix.cpp
)
target_include_directories(finn-settings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(finn-settings PRIVATE
    FinnSDK::FinnUI
    Qt6::DBus
    Qt6::Network
)

include(GNUInstallDirs)
install(TARGETS finn-settings RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
