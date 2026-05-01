project(cppwinrt-proj LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Select architecture-specific thunk stubs and assembler
if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64EC")
    set(THUNK_ASM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/thunk_stubs_arm64ec.asm)
    set(THUNK_USE_ARMASM TRUE)
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
    set(THUNK_ASM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/thunk_stubs_arm64.asm)
    set(THUNK_USE_ARMASM TRUE)
else()
    # x86 and x64 use MASM (ml/ml64) which CMake supports natively
    enable_language(ASM_MASM)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "Win32" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(THUNK_ASM_SOURCE thunk_stubs_x86.asm)
    else()
        set(THUNK_ASM_SOURCE thunk_stubs.asm)
    endif()
    set(THUNK_USE_ARMASM FALSE)
endif()

if(THUNK_USE_ARMASM)
    # ARM64/ARM64EC: assemble with armasm64 via custom command
    set(THUNK_OBJ ${CMAKE_CURRENT_BINARY_DIR}/thunk_stubs.obj)
    set(THUNK_OBJ_TESTS ${CMAKE_CURRENT_BINARY_DIR}/thunk_stubs_tests.obj)
    add_custom_command(
        OUTPUT ${THUNK_OBJ}
        COMMAND armasm64 -nologo -o ${THUNK_OBJ} ${THUNK_ASM_SOURCE}
        DEPENDS ${THUNK_ASM_SOURCE}
        COMMENT "Assembling ${THUNK_ASM_SOURCE} with armasm64"
    )
    add_custom_command(
        OUTPUT ${THUNK_OBJ_TESTS}
        COMMAND armasm64 -nologo -o ${THUNK_OBJ_TESTS} ${THUNK_ASM_SOURCE}
        DEPENDS ${THUNK_ASM_SOURCE}
        COMMENT "Assembling ${THUNK_ASM_SOURCE} with armasm64 (tests)"
    )
    add_executable(cppwinrt-proj experiment.cpp)
    target_link_libraries(cppwinrt-proj PRIVATE ${THUNK_OBJ})
    add_custom_target(thunk_stubs_asm DEPENDS ${THUNK_OBJ})
    add_dependencies(cppwinrt-proj thunk_stubs_asm)

    add_executable(thunk-tests thunk_tests.cpp)
    target_link_libraries(thunk-tests PRIVATE ${THUNK_OBJ_TESTS})
    add_custom_target(thunk_stubs_asm_tests DEPENDS ${THUNK_OBJ_TESTS})
    add_dependencies(thunk-tests thunk_stubs_asm_tests)
else()
    add_executable(cppwinrt-proj experiment.cpp ${THUNK_ASM_SOURCE})
    add_executable(thunk-tests thunk_tests.cpp ${THUNK_ASM_SOURCE})
endif()

find_package(cppwinrt CONFIG REQUIRED)

target_link_libraries(cppwinrt-proj PRIVATE Microsoft::CppWinRT OneCoreUAP)
target_link_libraries(thunk-tests PRIVATE Microsoft::CppWinRT OneCoreUAP)
