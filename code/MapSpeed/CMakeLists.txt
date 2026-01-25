cmake_minimum_required(VERSION 3.19)
project(map-speeds LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

add_executable(map-speeds src/map_speeds.cpp)

find_package(frozen CONFIG REQUIRED)
target_link_libraries(map-speeds PRIVATE frozen::frozen)
