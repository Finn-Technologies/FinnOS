cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-files LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

add_executable(finn-files
    main.cpp
    ../../src/compat/desktop_entry.cpp
    ../../src/compat/flatpak_installer.cpp
    ../../src/compat/macos_runner.cpp
    ../../src/compat/ohos_runner.cpp
    ../../src/compat/wine_runner.cpp
    ../../src/compat/wine_prefix.cpp
    ../../src/compat/msi_installer.cpp
)
target_include_directories(finn-files PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_link_libraries(finn-files PRIVATE FinnSDK::FinnUI)

include(GNUInstallDirs)
install(TARGETS finn-files RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
