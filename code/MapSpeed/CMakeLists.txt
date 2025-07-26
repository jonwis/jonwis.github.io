cmake_minimum_required(VERSION 3.19)
project(MapSpeed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MapSpeed src/map_speeds.cpp)
