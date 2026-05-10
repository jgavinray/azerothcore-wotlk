# GoogleTest - single source of truth: deps/googletest (release-1.12.1)
# Zero network calls, no download
# The actual source is at deps/googletest/googletest/

# Use absolute path for reliable resolution
add_subdirectory(${CMAKE_SOURCE_DIR}/deps/googletest/googletest
                 ${CMAKE_BINARY_DIR}/googletest-build
                 EXCLUDE_FROM_ALL)
