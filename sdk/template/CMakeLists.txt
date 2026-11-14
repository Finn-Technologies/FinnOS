cmake_minimum_required(VERSION 3.22)
project(FinnCounterApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(FinnSDK REQUIRED)

add_executable(finn-counter
    main.cpp
)

target_link_libraries(finn-counter PRIVATE FinnSDK::FinnUI)
