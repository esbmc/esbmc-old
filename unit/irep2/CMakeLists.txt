add_library(cache_test_utils cache_test_utils.cpp)
target_link_libraries(cache_test_utils PUBLIC util_esbmc)

add_library(cache_fuzz_utils cache_fuzz_utils.cpp)
target_link_libraries(cache_fuzz_utils PUBLIC util_esbmc cache_test_utils)

new_unit_test(irep2test "irep2.test.cpp" "util_esbmc;irep2;bigint")
new_unit_test(green_reorder_test "green_reordering.test.cpp" "util_esbmc;irep2;bigint;cache_test_utils;green_reordering")
new_fast_fuzz_test(green_reorder_fuzz "green_reordering.fuzz.cpp" "util_esbmc;irep2;bigint;cache_test_utils;cache_fuzz_utils;green_reordering;")
