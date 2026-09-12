cmake_minimum_required(VERSION 3.25)

project(hplf VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "hplf currently supports Linux only")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(HPLF_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(HPLF_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(HPLF_ENABLE_ASAN_UBSAN AND HPLF_ENABLE_TSAN)
    message(FATAL_ERROR "ASan/UBSan and TSan must run in separate builds")
endif()

find_package(Threads REQUIRED)
include(CTest)

function(hplf_configure_target target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(${target} PRIVATE
        _DEFAULT_SOURCE
        _POSIX_C_SOURCE=200809L
    )

    if(HPLF_ENABLE_ASAN_UBSAN)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    elseif(HPLF_ENABLE_TSAN)
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
