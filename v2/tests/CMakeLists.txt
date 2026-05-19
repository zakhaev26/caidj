# ── Test executable ────────────────────────────────────────────────
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS *.cpp)

add_executable(caidj_tests ${TEST_SOURCES})
target_compile_features(caidj_tests PUBLIC cxx_std_20)

target_include_directories(caidj_tests PUBLIC
  ${CAIDJ_INCLUDE_DIR}
)

target_link_libraries(caidj_tests PUBLIC
  caidj_lib
)

add_test(NAME caidj_tests COMMAND caidj_tests)
