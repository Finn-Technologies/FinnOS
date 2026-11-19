cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-store LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)

add_executable(finn-store
    main.cpp
    ../../src/session/app_registry.cpp
    ../../src/compat/desktop_entry.cpp
    ../../src/compat/flatpak_installer.cpp
    ../../src/compat/macos_runner.cpp
    ../../src/compat/ohos_runner.cpp
    ../../src/compat/wine_runner.cpp
    ../../src/compat/android_runner.cpp
    ../../src/system/sys_files.cpp
)
target_include_directories(finn-store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(finn-store PRIVATE FinnSDK::FinnUI)

include(GNUInstallDirs)
install(TARGETS finn-store RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
