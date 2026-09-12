add_executable(hplf_test_platform test_platform.c)
hplf_configure_target(hplf_test_platform)
target_link_libraries(hplf_test_platform PRIVATE Threads::Threads)

add_test(NAME platform COMMAND hplf_test_platform)
set_tests_properties(platform PROPERTIES
    LABELS "platform;unit"
    TIMEOUT 10
)
