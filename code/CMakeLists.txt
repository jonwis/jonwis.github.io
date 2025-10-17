cmake_minimum_required(VERSION 3.26.0)
project(JonWisCodeSamples VERSION 0.1.0 LANGUAGES C CXX)

find_package(frozen CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)

add_subdirectory(Formatter)
add_subdirectory(MemMapLargePages)
add_subdirectory(DllPathBinding)
add_subdirectory(MapSpeed)
