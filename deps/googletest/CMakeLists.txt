#
# GoogleTest dependency - checked in for fast CI builds (release-1.12.1)
# No external fetch needed - builds gtest, gmock, gtest_main, gmock_main
#

add_subdirectory(googletest)

# Set default options for googletest builds
option(gmock_build_tests "Build Google Mock's own tests" OFF)
set(gmock_build_tests OFF CACHE BOOL "" FORCE)

option(gtest_build_samples "Build Google Test's own samples" OFF)
set(gtest_build_samples OFF CACHE BOOL "" FORCE)

option(gtest_hide_internal_symbols "Hide internal symbols" OFF)
set(gtest_hide_internal_symbols OFF CACHE BOOL "" FORCE)
