cmake_minimum_required(VERSION 3.10.0)
project(MemMapLargePages VERSION 0.1.0 LANGUAGES C CXX)

add_executable(MemMapLargePages main.cpp)

target_link_libraries(MemMapLargePages PRIVATE onecoreuap)