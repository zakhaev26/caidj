include(FetchContent)

set(FETCHCONTENT_QUIET FALSE)

if(NOT CAIDJ_FETCH_THIRD_PARTY)
  message(STATUS "CAIDJ_FETCH_THIRD_PARTY=OFF: building with the self-contained std-only implementation")
  return()
endif()

# ── GoogleTest ──────────────────────────────────────────────────────
if(CAIDJ_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY  https://github.com/google/googletest.git
    GIT_TAG         v1.14.0
    GIT_SHALLOW     TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# ── nlohmann/json ──────────────────────────────────────────────────
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY  https://github.com/nlohmann/json.git
  GIT_TAG         v3.11.3
  GIT_SHALLOW     TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── toml11 ─────────────────────────────────────────────────────────
FetchContent_Declare(
  toml11
  GIT_REPOSITORY  https://github.com/ToruNiina/toml11.git
  GIT_TAG         v4.2.0
  GIT_SHALLOW     TRUE
)
FetchContent_MakeAvailable(toml11)

# ── spdlog ─────────────────────────────────────────────────────────
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY  https://github.com/gabime/spdlog.git
  GIT_TAG         v1.13.0
  GIT_SHALLOW     TRUE
)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# ── google/benchmark (optional micro-benchmark dependency) ─────────
option(CAIDJ_FETCH_GOOGLE_BENCHMARK "Fetch google/benchmark" OFF)
if(CAIDJ_FETCH_GOOGLE_BENCHMARK)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY  https://github.com/google/benchmark.git
    GIT_TAG         v1.8.3
    GIT_SHALLOW     TRUE
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()
