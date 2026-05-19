cmake_minimum_required(VERSION 3.22)
project(CAIDJ VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CAIDJ_BUILD_TESTS "Build CAIDJ unit tests" ON)
option(CAIDJ_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers on supported compilers" OFF)
option(CAIDJ_FETCH_THIRD_PARTY "Fetch README-listed third-party dependencies" OFF)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

# ── Compile options ───────────────────────────────────────────────
if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
    $<$<CONFIG:Release>:-march=native>
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-O0>
  )

  if(CAIDJ_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# ── Dependencies ──────────────────────────────────────────────────
# The implementation below is intentionally self-contained and builds with only
# the C++20 standard library.  Set CAIDJ_FETCH_THIRD_PARTY=ON if you want CMake
# to also fetch the README-listed optional dependencies for experimentation.
include(cmake/Dependencies.cmake)

# ── Include directories ───────────────────────────────────────────
set(CAIDJ_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

# ── Sources (everything except main.cpp) ──────────────────────────
file(GLOB_RECURSE CAIDJ_LIB_SOURCES CONFIGURE_DEPENDS
  src/*.cpp
)

list(REMOVE_ITEM CAIDJ_LIB_SOURCES ${PROJECT_SOURCE_DIR}/src/main.cpp)
list(REMOVE_ITEM CAIDJ_LIB_SOURCES src/main.cpp)

# ── Library target ────────────────────────────────────────────────
add_library(caidj_lib STATIC
  ${CAIDJ_LIB_SOURCES}
)

target_compile_features(caidj_lib PUBLIC cxx_std_20)

target_include_directories(caidj_lib PUBLIC
  ${CAIDJ_INCLUDE_DIR}
)

target_link_libraries(caidj_lib PUBLIC
  Threads::Threads
)

if(TARGET nlohmann_json::nlohmann_json)
  target_link_libraries(caidj_lib PUBLIC nlohmann_json::nlohmann_json)
endif()

if(TARGET spdlog::spdlog)
  target_link_libraries(caidj_lib PUBLIC spdlog::spdlog)
endif()

if(TARGET toml11::toml11)
  target_link_libraries(caidj_lib PUBLIC toml11::toml11)
elseif(TARGET toml11)
  target_link_libraries(caidj_lib PUBLIC toml11)
endif()

# ── Executable target ─────────────────────────────────────────────
add_executable(caidj
  src/main.cpp
)

target_compile_features(caidj PUBLIC cxx_std_20)

target_link_libraries(caidj PRIVATE
  caidj_lib
)

target_include_directories(caidj PUBLIC
  ${CAIDJ_INCLUDE_DIR}
)

# ── Install ───────────────────────────────────────────────────────
install(TARGETS caidj RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS caidj_lib ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# ── Tests ─────────────────────────────────────────────────────────
if(CAIDJ_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
