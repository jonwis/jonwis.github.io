cmake_minimum_required(VERSION 3.26.0)
project(CharacterFormatter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)

add_executable(CharacterFormatter)

target_sources(CharacterFormatter PRIVATE
 "format_conversions.cpp"
 "iterators.cpp")

target_link_libraries(CharacterFormatter PRIVATE "icu.lib")

target_compile_options(CharacterFormatter 
    PRIVATE 
        /source-charset:utf-8
        /execution-charset:utf-8)
