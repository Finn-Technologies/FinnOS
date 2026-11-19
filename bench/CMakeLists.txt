cmake_minimum_required(VERSION 3.22)

add_executable(blur_bench blur_bench.cpp)
target_include_directories(blur_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
