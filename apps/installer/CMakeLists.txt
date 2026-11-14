cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-installer LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)

add_executable(finn-installer
    main.cpp
    ../../src/system/sys_files.cpp
)
target_include_directories(finn-installer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(finn-installer PRIVATE FinnSDK::FinnUI)

include(GNUInstallDirs)
install(TARGETS finn-installer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
