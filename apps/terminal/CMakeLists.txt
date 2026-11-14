cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(finn-terminal LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(FinnSDK REQUIRED)
endif()

set(CMAKE_AUTOMOC ON)

add_executable(finn-terminal
    main.cpp
)
target_link_libraries(finn-terminal PRIVATE FinnSDK::FinnUI)

include(GNUInstallDirs)
install(TARGETS finn-terminal RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
