cmake_minimum_required(VERSION 3.26.0)
project(JonWisCodeSamples VERSION 0.1.0 LANGUAGES C CXX)

add_subdirectory(Formatter)
add_subdirectory(MemMapLargePages)
add_subdirectory(DllPathBinding)

# MapSpeed requires vcpkg dependencies - only add if frozen is available
find_package(frozen CONFIG QUIET)
if(frozen_FOUND)
    add_subdirectory(MapSpeed)
    message(STATUS "Building MapSpeed (frozen package found)")
else()
    message(STATUS "Skipping MapSpeed (frozen package not found - install via vcpkg if needed)")
endif()
