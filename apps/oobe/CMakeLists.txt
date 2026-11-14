cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-oobe LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS DBus Network)

add_executable(finn-oobe
    main.cpp
    ../../src/system/desktop_services.cpp
    ../../src/system/sys_files.cpp
)
target_include_directories(finn-oobe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(finn-oobe PRIVATE FinnSDK::FinnUI Qt6::DBus Qt6::Network)

include(GNUInstallDirs)
install(TARGETS finn-oobe RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
