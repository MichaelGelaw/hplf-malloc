add_executable(hplf_worker_pool_libc
    worker_pool.c
    adapters/libc.c
)
hplf_configure_target(hplf_worker_pool_libc)
target_include_directories(hplf_worker_pool_libc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hplf_worker_pool_libc PRIVATE Threads::Threads)

add_executable(hplf_worker_pool_hplf
    worker_pool.c
    adapters/hplf.c
)
hplf_configure_target(hplf_worker_pool_hplf)
target_include_directories(hplf_worker_pool_hplf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hplf_worker_pool_hplf PRIVATE hplf_static Threads::Threads)

if(BUILD_TESTING)
    find_program(HPLF_EXAMPLE_BASH_PROGRAM bash REQUIRED)
    add_test(NAME worker_pool
        COMMAND ${HPLF_EXAMPLE_BASH_PROGRAM}
            ${PROJECT_SOURCE_DIR}/scripts/test_worker_pool.sh
            $<TARGET_FILE:hplf_worker_pool_libc>
            $<TARGET_FILE:hplf_worker_pool_hplf>
            BUILD
    )
    set_tests_properties(worker_pool PROPERTIES
        LABELS "integration;worker-pool"
        PASS_REGULAR_EXPRESSION "worker-pool validation mode=BUILD"
        TIMEOUT 30
    )
endif()
