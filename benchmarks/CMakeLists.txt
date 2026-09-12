add_executable(hplf_bench_libc
    bench_main.c
    adapters/libc.c
)
hplf_configure_target(hplf_bench_libc)
target_include_directories(hplf_bench_libc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(hplf_bench_locked
    bench_main.c
    adapters/hplf.c
)
hplf_configure_target(hplf_bench_locked)
target_include_directories(hplf_bench_locked PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hplf_bench_locked PRIVATE hplf_static)

if(BUILD_TESTING)
    add_test(NAME benchmark_libc_smoke COMMAND hplf_bench_libc 1000 64)
    add_test(NAME benchmark_locked_smoke COMMAND hplf_bench_locked 1000 64)
    set_tests_properties(benchmark_libc_smoke benchmark_locked_smoke PROPERTIES
        LABELS "benchmark;smoke"
        PASS_REGULAR_EXPRESSION
            "\\\"schema\\\":\\\"hplf-benchmark-v1\\\".*\\\"status\\\":\\\"smoke\\\""
        TIMEOUT 20
    )
endif()
