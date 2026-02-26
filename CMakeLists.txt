cmake_minimum_required(VERSION 3.14)
project(ConcurrencyAwareJoins CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -fsanitize=thread")

include_directories(include)

add_executable(benchmark
    src/main.cpp
    src/concurrent_index.cpp
    src/skew_aware_join.cpp
    src/baseline_index.cpp
    src/benchmark.cpp
)

target_link_libraries(benchmark pthread)
