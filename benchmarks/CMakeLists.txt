add_executable(hplf_bench_libc
    bench_main.c
    workloads.c
    adapters/libc.c
)
hplf_configure_target(hplf_bench_libc)
target_include_directories(hplf_bench_libc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hplf_bench_libc PRIVATE Threads::Threads)

add_executable(hplf_bench_locked
    bench_main.c
    workloads.c
    adapters/hplf.c
)
hplf_configure_target(hplf_bench_locked)
target_include_directories(hplf_bench_locked PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hplf_bench_locked PRIVATE hplf_static Threads::Threads)

add_executable(hplf_bench_cached_mutex
    bench_main.c
    workloads.c
    adapters/hplf.c
)
hplf_configure_target(hplf_bench_cached_mutex)
target_compile_definitions(hplf_bench_cached_mutex PRIVATE
    HPLF_BENCHMARK_ADAPTER_NAME="hplf_cached_mutex"
)
target_include_directories(hplf_bench_cached_mutex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hplf_bench_cached_mutex PRIVATE
    hplf_cached_mutex_static
    Threads::Threads
)

if(BUILD_TESTING)
    set(HPLF_BENCHMARK_SMOKE_ARGUMENTS
        --threads 2
        --seed 17
        --warmup-seconds 0.005
        --seconds 0.01
        --live-slots 32
        --output -
    )
    foreach(workload IN ITEMS
            fixed random mixed burst large handoff ring churn queue)
        add_test(NAME benchmark_${workload}_smoke
            COMMAND hplf_bench_locked
                --workload ${workload}
                ${HPLF_BENCHMARK_SMOKE_ARGUMENTS}
        )
        set_tests_properties(benchmark_${workload}_smoke PROPERTIES
            LABELS "benchmark;smoke"
            PASS_REGULAR_EXPRESSION
                "\\\"status\\\":\\\"smoke\\\".*\\\"workload\\\":\\\"${workload}\\\""
            TIMEOUT 20
        )
    endforeach()

    foreach(thread_count IN ITEMS 1 2 4)
        add_test(NAME benchmark_libc_${thread_count}_thread_smoke
            COMMAND hplf_bench_libc
                --workload fixed
                --threads ${thread_count}
                --seed 17
                --warmup-seconds 0.005
                --seconds 0.01
                --live-slots 32
                --output -
        )
        set_tests_properties(benchmark_libc_${thread_count}_thread_smoke PROPERTIES
            LABELS "benchmark;smoke"
            PASS_REGULAR_EXPRESSION
                "\\\"status\\\":\\\"smoke\\\".*\\\"threads\\\":${thread_count}"
            TIMEOUT 20
        )
    endforeach()

    foreach(thread_count IN ITEMS 1 4)
        add_test(NAME benchmark_locked_${thread_count}_thread_smoke
            COMMAND hplf_bench_locked
                --workload fixed
                --threads ${thread_count}
                --seed 17
                --warmup-seconds 0.005
                --seconds 0.01
                --live-slots 32
                --output -
        )
        set_tests_properties(benchmark_locked_${thread_count}_thread_smoke PROPERTIES
            LABELS "benchmark;smoke"
            PASS_REGULAR_EXPRESSION
                "\\\"status\\\":\\\"smoke\\\".*\\\"threads\\\":${thread_count}"
            TIMEOUT 20
        )
    endforeach()

    foreach(workload IN ITEMS handoff ring churn)
        add_test(NAME benchmark_cached_mutex_${workload}_smoke
            COMMAND hplf_bench_cached_mutex
                --workload ${workload}
                --threads 4
                --seed 29
                --warmup-seconds 0.005
                --seconds 0.01
                --live-slots 32
                --output -
        )
        set_tests_properties(benchmark_cached_mutex_${workload}_smoke PROPERTIES
            LABELS "benchmark;smoke;cache;concurrency"
            PASS_REGULAR_EXPRESSION
                "\"variant\":\"hplf_cached_mutex\".*\"workload\":\"${workload}\""
            TIMEOUT 20
        )
    endforeach()

    foreach(thread_count IN ITEMS 1 4)
        add_test(NAME benchmark_cached_mutex_${thread_count}_thread_smoke
            COMMAND hplf_bench_cached_mutex
                --workload fixed
                --threads ${thread_count}
                --seed 17
                --warmup-seconds 0.005
                --seconds 0.01
                --live-slots 32
                --output -
        )
        set_tests_properties(benchmark_cached_mutex_${thread_count}_thread_smoke
            PROPERTIES
            LABELS "benchmark;smoke;cache"
            PASS_REGULAR_EXPRESSION
                "\"variant\":\"hplf_cached_mutex\".*\"threads\":${thread_count}"
            TIMEOUT 20
        )
    endforeach()

    foreach(fixed_size IN ITEMS 16 4096)
        add_test(NAME benchmark_fixed_${fixed_size}_byte_smoke
            COMMAND hplf_bench_locked
                --workload fixed
                --threads 1
                --seed 17
                --warmup-seconds 0.005
                --seconds 0.01
                --live-slots 32
                --size ${fixed_size}
                --output -
        )
        set_tests_properties(benchmark_fixed_${fixed_size}_byte_smoke PROPERTIES
            LABELS "benchmark;smoke"
            PASS_REGULAR_EXPRESSION
                "\\\"fixed_size_bytes\\\":${fixed_size}"
            TIMEOUT 20
        )
    endforeach()

    add_test(NAME benchmark_invalid_workload
        COMMAND hplf_bench_locked --workload missing)
    add_test(NAME benchmark_invalid_threads
        COMMAND hplf_bench_locked --threads 0)
    add_test(NAME benchmark_missing_value
        COMMAND hplf_bench_locked --seed)
    set_tests_properties(
        benchmark_invalid_workload
        benchmark_invalid_threads
        benchmark_missing_value
        PROPERTIES
        LABELS "benchmark;smoke"
        WILL_FAIL TRUE
    )
endif()
