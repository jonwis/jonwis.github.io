cmake_minimum_required(VERSION 3.19)
project(map-speeds LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable CMake's integration with vcpkg
# (This is automatic if you use the toolchain file, but you can add this for clarity)
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Enable link-time code generation (LTCG)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

add_executable(map-speeds src/map_speeds.cpp)

find_package(frozen CONFIG REQUIRED)
target_link_libraries(map-speeds PRIVATE frozen::frozen)
