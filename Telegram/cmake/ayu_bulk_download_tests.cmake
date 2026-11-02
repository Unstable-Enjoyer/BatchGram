# Console unit test for the bulk video download logic (AyuGram).
# Off by default: configure with -DAYU_BULK_DOWNLOAD_TESTS=ON.
# Compiles only the pure logic file and its test, no UI or session code.

add_executable(ayu_bulk_download_tests)
init_target(ayu_bulk_download_tests "(tests)")

target_include_directories(ayu_bulk_download_tests PRIVATE ${src_loc})

nice_target_sources(ayu_bulk_download_tests ${src_loc}
PRIVATE
    ayu/features/bulk_download/bulk_download_logic.cpp
    ayu/features/bulk_download/bulk_download_logic.h
    ayu/features/bulk_download/bulk_download_logic_tests.cpp
)

# bulk_download_logic.h includes base/basic_types.h from lib_base, which
# also brings Qt, gsl and the lib_base include directory.
target_link_libraries(ayu_bulk_download_tests
PRIVATE
    desktop-app::lib_base
)

set_target_properties(ayu_bulk_download_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
