add_executable(hplf_test_platform test_platform.c)
hplf_configure_target(hplf_test_platform)
target_link_libraries(hplf_test_platform PRIVATE Threads::Threads)

add_test(NAME platform COMMAND hplf_test_platform)
set_tests_properties(platform PROPERTIES
    LABELS "platform;unit"
    TIMEOUT 10
)

add_executable(hplf_test_checked_math test_checked_math.c)
hplf_configure_target(hplf_test_checked_math)
target_include_directories(hplf_test_checked_math PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME checked_math COMMAND hplf_test_checked_math)
set_tests_properties(checked_math PROPERTIES
    LABELS "unit"
    TIMEOUT 10
)

add_executable(hplf_test_os_memory
    test_os_memory.c
    ${PROJECT_SOURCE_DIR}/src/os_memory.c
)
hplf_configure_target(hplf_test_os_memory)
target_compile_definitions(hplf_test_os_memory PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_os_memory PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME os_memory COMMAND hplf_test_os_memory)
set_tests_properties(os_memory PROPERTIES
    LABELS "unit"
    TIMEOUT 10
)

add_executable(hplf_test_layout test_layout.c)
hplf_configure_target(hplf_test_layout)
target_include_directories(hplf_test_layout PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_layout PRIVATE hplf_static)

add_test(NAME layout COMMAND hplf_test_layout)
set_tests_properties(layout PROPERTIES
    LABELS "unit"
    TIMEOUT 10
)

add_executable(hplf_test_size_class
    test_size_class.c
    ${PROJECT_SOURCE_DIR}/src/size_class.c
)
hplf_configure_target(hplf_test_size_class)
target_include_directories(hplf_test_size_class PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_test(NAME size_class COMMAND hplf_test_size_class)
set_tests_properties(size_class PROPERTIES
    LABELS "unit"
    TIMEOUT 10
)

add_library(hplf_test_allocator_core STATIC ${HPLF_SOURCES})
hplf_configure_target(hplf_test_allocator_core)
target_compile_definitions(hplf_test_allocator_core PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_allocator_core
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(hplf_test_allocator_core PRIVATE Threads::Threads)

add_executable(hplf_test_slab test_slab.c)
hplf_configure_target(hplf_test_slab)
target_compile_definitions(hplf_test_slab PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_slab PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_slab PRIVATE
    hplf_test_allocator_core
    Threads::Threads
)

add_test(NAME slab COMMAND hplf_test_slab)
set_tests_properties(slab PROPERTIES
    LABELS "unit"
    TIMEOUT 20
)

add_library(hplf_test_pattern STATIC support/pattern.c)
hplf_configure_target(hplf_test_pattern)
target_include_directories(hplf_test_pattern PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)

add_executable(hplf_test_api test_api.c)
hplf_configure_target(hplf_test_api)
target_compile_definitions(hplf_test_api PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_api PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_api PRIVATE
    hplf_test_allocator_core
    hplf_test_pattern
)

add_test(NAME api COMMAND hplf_test_api)
set_tests_properties(api PROPERTIES
    LABELS "api;unit"
    TIMEOUT 20
)

add_executable(hplf_test_trim test_trim.c)
hplf_configure_target(hplf_test_trim)
target_compile_definitions(hplf_test_trim PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_trim PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_trim PRIVATE hplf_test_allocator_core)

add_test(NAME trim COMMAND hplf_test_trim)
set_tests_properties(trim PROPERTIES
    LABELS "api;unit"
    TIMEOUT 20
)

add_executable(hplf_test_randomized test_randomized.c)
hplf_configure_target(hplf_test_randomized)
target_compile_definitions(hplf_test_randomized PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_randomized PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_randomized PRIVATE
    hplf_test_allocator_core
    hplf_test_pattern
)

add_test(NAME randomized COMMAND hplf_test_randomized)
set_tests_properties(randomized PROPERTIES
    LABELS "api;randomized"
    TIMEOUT 120
)

add_executable(hplf_test_cache
    test_cache.c
    ${PROJECT_SOURCE_DIR}/src/thread_cache.c
    ${PROJECT_SOURCE_DIR}/src/transfer_mutex.c
    ${PROJECT_SOURCE_DIR}/src/size_class.c
)
hplf_configure_target(hplf_test_cache)
target_compile_definitions(hplf_test_cache PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_cache PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_cache PRIVATE Threads::Threads)

add_test(NAME cache COMMAND hplf_test_cache)
set_tests_properties(cache PROPERTIES
    LABELS "unit;concurrency;cache"
    TIMEOUT 20
)

add_library(hplf_test_cached_mutex_core STATIC ${HPLF_CACHED_MUTEX_SOURCES})
hplf_configure_target(hplf_test_cached_mutex_core)
target_compile_definitions(hplf_test_cached_mutex_core PRIVATE
    HPLF_TESTING
    HPLF_VARIANT_CACHED_MUTEX
)
target_include_directories(hplf_test_cached_mutex_core
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(hplf_test_cached_mutex_core PRIVATE Threads::Threads)

add_executable(hplf_test_cached_allocator test_cached_allocator.c)
hplf_configure_target(hplf_test_cached_allocator)
target_compile_definitions(hplf_test_cached_allocator PRIVATE
    HPLF_TESTING
    HPLF_VARIANT_CACHED_MUTEX
)
target_include_directories(hplf_test_cached_allocator PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_cached_allocator PRIVATE
    hplf_test_cached_mutex_core
    Threads::Threads
)

add_test(NAME cached_allocator COMMAND hplf_test_cached_allocator)
set_tests_properties(cached_allocator PROPERTIES
    LABELS "api;unit;concurrency;cache"
    PASS_REGULAR_EXPRESSION "cached-mutex local_hits="
    TIMEOUT 20
)

add_executable(hplf_test_thread_lifecycle test_thread_lifecycle.c)
hplf_configure_target(hplf_test_thread_lifecycle)
target_compile_definitions(hplf_test_thread_lifecycle PRIVATE
    HPLF_TESTING
    HPLF_VARIANT_CACHED_MUTEX
)
target_include_directories(hplf_test_thread_lifecycle
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(hplf_test_thread_lifecycle PRIVATE
    hplf_test_cached_mutex_core
    Threads::Threads
)
foreach(scenario IN ITEMS
        churn
        cross-free
        key-failure
        map-failure
        registration-failure
        late-callback
        callback-order)
    add_test(NAME thread_lifecycle_${scenario}
        COMMAND hplf_test_thread_lifecycle ${scenario}
    )
    set_tests_properties(thread_lifecycle_${scenario} PROPERTIES
        LABELS "api;concurrency;cache;lifecycle"
        PASS_REGULAR_EXPRESSION "thread-lifecycle scenario=${scenario} ok"
        TIMEOUT 120
    )
endforeach()

add_executable(hplf_test_cached_api test_api.c)
hplf_configure_target(hplf_test_cached_api)
target_compile_definitions(hplf_test_cached_api PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_cached_api PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_cached_api PRIVATE
    hplf_test_cached_mutex_core
    hplf_test_pattern
)
add_test(NAME cached_api COMMAND hplf_test_cached_api)
set_tests_properties(cached_api PROPERTIES LABELS "api;cache" TIMEOUT 20)

add_executable(hplf_test_cached_randomized test_randomized.c)
hplf_configure_target(hplf_test_cached_randomized)
target_compile_definitions(hplf_test_cached_randomized PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_cached_randomized PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_cached_randomized PRIVATE
    hplf_test_cached_mutex_core
    hplf_test_pattern
)
add_test(NAME cached_randomized COMMAND hplf_test_cached_randomized)
set_tests_properties(cached_randomized PROPERTIES
    LABELS "api;randomized;cache"
    TIMEOUT 120
)

add_executable(hplf_test_cached_trim test_trim.c)
hplf_configure_target(hplf_test_cached_trim)
target_compile_definitions(hplf_test_cached_trim PRIVATE HPLF_TESTING)
target_include_directories(hplf_test_cached_trim PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hplf_test_cached_trim PRIVATE hplf_test_cached_mutex_core)
add_test(NAME cached_trim COMMAND hplf_test_cached_trim)
set_tests_properties(cached_trim PROPERTIES LABELS "api;cache" TIMEOUT 20)

find_program(HPLF_BASH_PROGRAM bash REQUIRED)
set(HPLF_CLIENT_SANITIZER none)
if(HPLF_ENABLE_ASAN_UBSAN)
    set(HPLF_CLIENT_SANITIZER address,undefined)
elseif(HPLF_ENABLE_TSAN)
    set(HPLF_CLIENT_SANITIZER thread)
endif()

add_test(NAME installed_client
    COMMAND ${HPLF_BASH_PROGRAM}
        ${PROJECT_SOURCE_DIR}/scripts/test_install.sh
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_BINARY_DIR}
        ${HPLF_CLIENT_SANITIZER}
        ${CMAKE_C_COMPILER}
)
set_tests_properties(installed_client PROPERTIES
    LABELS "integration;packaging"
    PASS_REGULAR_EXPRESSION "installed-client mode=SHARED"
    RUN_SERIAL TRUE
    TIMEOUT 120
)
