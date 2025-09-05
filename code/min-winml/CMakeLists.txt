
# Automatically configure vcpkg toolchain if available, before project() is called
cmake_minimum_required(VERSION 3.10.0)

project(minimal-full-winml-sample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(minimal-full-winml-sample
	windowsappsdk/inc/abi/winml/WindowsMLAutoInitializer.cpp
	main.cpp)

target_include_directories(minimal-full-winml-sample PRIVATE 
	${CMAKE_CURRENT_SOURCE_DIR}/windowsappsdk/inc/abi/winml
	${CMAKE_CURRENT_SOURCE_DIR}/windowsappsdk/inc/abi/runtime
)

# Link wil if found (vcpkg will provide it)
find_package(wil CONFIG REQUIRED)
target_link_libraries(minimal-full-winml-sample
	PRIVATE WIL::WIL
	onecoreuap.lib
	windowsapp.lib
	)
