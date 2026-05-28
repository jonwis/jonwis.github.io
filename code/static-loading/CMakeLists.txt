cmake_minimum_required(VERSION 3.20)
project(static_loading LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy")

add_library(panmap SHARED
    src/panmap.cpp
    src/panmap.h
    src/panmap.rc
)
target_compile_definitions(panmap PRIVATE PANMAP_BUILD)
target_link_options(panmap PRIVATE /MANIFEST:NO)
set_target_properties(panmap PROPERTIES OUTPUT_NAME "panmap")

add_library(batmeter SHARED
    src/batmeter.cpp
    src/batmeter.h
)
target_compile_definitions(batmeter PRIVATE BATMETER_BUILD)
target_link_options(batmeter PRIVATE /MANIFEST:NO)
set_target_properties(batmeter PROPERTIES OUTPUT_NAME "batmeter")

add_executable(testing
    src/main.cpp
    src/testing.rc
)
target_link_libraries(testing PRIVATE panmap batmeter)
target_link_options(testing PRIVATE /MANIFEST:NO)

add_custom_command(TARGET testing POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}/core"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:testing>" "${DEPLOY_DIR}/testing.exe"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:panmap>" "${DEPLOY_DIR}/panmap.dll"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:batmeter>" "${DEPLOY_DIR}/core/batmeter.dll"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/manifests/core.manifest" "${DEPLOY_DIR}/core/core.manifest"
)
