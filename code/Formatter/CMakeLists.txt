cmake_minimum_required(VERSION 3.26.0)
project(CharacterFormatter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)

add_executable(CharacterFormatter)

target_sources(CharacterFormatter
    PRIVATE
        format_conversions.cpp
)

