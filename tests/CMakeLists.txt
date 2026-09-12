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
