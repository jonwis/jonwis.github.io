cmake_minimum_required(VERSION 3.26.0)
project(JonWisCodeSamples VERSION 0.1.0 LANGUAGES C CXX)

find_package(frozen CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)

enable_testing()

add_subdirectory(code/Formatter)
add_subdirectory(code/MemMapLargePages)
add_subdirectory(code/DllPathBinding)
add_subdirectory(code/MapSpeed)
add_subdirectory(code/Coroutines)
add_subdirectory(code/temp-dll)
add_subdirectory(code/cppwinrt-proj)
# add_subdirectory(code/min-winml)
