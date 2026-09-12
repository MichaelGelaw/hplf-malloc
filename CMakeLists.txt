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
include(GNUInstallDirs)

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

set(HPLF_SOURCES
    ${PROJECT_SOURCE_DIR}/src/allocator.c
    ${PROJECT_SOURCE_DIR}/src/os_memory.c
    ${PROJECT_SOURCE_DIR}/src/size_class.c
    ${PROJECT_SOURCE_DIR}/src/slab.c
)

add_library(hplf_static STATIC ${HPLF_SOURCES})
add_library(hplf_shared SHARED ${HPLF_SOURCES})

foreach(target IN ITEMS hplf_static hplf_shared)
    hplf_configure_target(${target})
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(${target} PRIVATE Threads::Threads)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME hplf)
endforeach()

set_target_properties(hplf_shared PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS hplf_static hplf_shared
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/hplf/allocator.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hplf
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
