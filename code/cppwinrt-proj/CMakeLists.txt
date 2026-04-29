project(cppwinrt-proj LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cppwinrt-proj 
    experiment.cpp)

find_package(cppwinrt CONFIG REQUIRED)

target_link_libraries(cppwinrt-proj PRIVATE Microsoft::CppWinRT OneCoreUAP)
