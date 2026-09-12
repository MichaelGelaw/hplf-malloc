cmake_minimum_required(VERSION 3.25)

project(hplf_installed_client LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT DEFINED HPLF_SANITIZER)
    set(HPLF_SANITIZER none)
endif()

if(NOT HPLF_LINK_MODE STREQUAL "STATIC" AND
   NOT HPLF_LINK_MODE STREQUAL "SHARED")
    message(FATAL_ERROR "HPLF_LINK_MODE must be STATIC or SHARED")
endif()
if(NOT HPLF_SANITIZER STREQUAL "none" AND
   NOT HPLF_SANITIZER STREQUAL "address,undefined" AND
   NOT HPLF_SANITIZER STREQUAL "thread")
    message(FATAL_ERROR "Unsupported HPLF_SANITIZER value")
endif()

if(HPLF_LINK_MODE STREQUAL "STATIC")
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES .so)
endif()

find_path(HPLF_INCLUDE_DIR
    NAMES hplf/allocator.h
    PATHS "${HPLF_ROOT}/include"
    NO_DEFAULT_PATH
    REQUIRED
)
find_library(HPLF_LIBRARY
    NAMES hplf
    PATHS "${HPLF_ROOT}/lib"
    NO_DEFAULT_PATH
    REQUIRED
)
find_package(Threads REQUIRED)

add_executable(hplf_installed_client main.c)
target_compile_options(hplf_installed_client PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(hplf_installed_client PRIVATE
    HPLF_CLIENT_LINK_MODE="${HPLF_LINK_MODE}"
    _POSIX_C_SOURCE=200809L
)
target_include_directories(hplf_installed_client PRIVATE "${HPLF_INCLUDE_DIR}")
target_link_libraries(hplf_installed_client PRIVATE
    "${HPLF_LIBRARY}"
    Threads::Threads
)

if(NOT HPLF_SANITIZER STREQUAL "none")
    target_compile_options(hplf_installed_client PRIVATE
        "-fsanitize=${HPLF_SANITIZER}"
        -fno-omit-frame-pointer
    )
    target_link_options(hplf_installed_client PRIVATE
        "-fsanitize=${HPLF_SANITIZER}"
    )
endif()
