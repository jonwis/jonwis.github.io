project(coroutines LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

add_executable(coroutines coroutines.cpp)
find_package(wil CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
target_link_libraries(coroutines PRIVATE WIL::WIL  OneCoreUAP)