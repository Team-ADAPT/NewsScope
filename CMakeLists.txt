cmake_minimum_required(VERSION 3.12)
project(NewsScope)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Main library sources
set(SOURCES
    src/preprocessing.cpp
    src/source_validator.cpp
    src/phrase_indexer.cpp
    src/string_matcher.cpp
    src/frequency_analyzer.cpp
    src/temporal_analyzer.cpp
    src/greedy_filter.cpp
    src/claim_verifier.cpp
    src/scoring_engine.cpp
    src/thread_pool.cpp
)

# Create library
add_library(newsscope ${SOURCES})

# Demo application
add_executable(newsscope_demo src/main.cpp)
target_link_libraries(newsscope_demo newsscope pthread)

# Web portal server
add_executable(newsscope_webserver src/web_portal.cpp)
target_link_libraries(newsscope_webserver newsscope pthread)

# Enable testing
enable_testing()

# Standalone test executables (each test file has its own main())
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/test_*.cpp")
foreach(TEST_SRC ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} newsscope pthread)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# Benchmark executable
add_executable(benchmark_throughput
    benchmark/benchmark_throughput.cpp
)
target_link_libraries(benchmark_throughput newsscope pthread)

add_executable(benchmark_latency
    benchmark/benchmark_latency.cpp
)
target_link_libraries(benchmark_latency newsscope pthread)

add_executable(benchmark_memory
    benchmark/benchmark_memory.cpp
)
target_link_libraries(benchmark_memory newsscope pthread)
