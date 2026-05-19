# CMake generated Testfile for 
# Source directory: C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests
# Build directory: C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/build/debug/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[caidj_tests]=] "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/build/debug/tests/Debug/caidj_tests.exe")
  set_tests_properties([=[caidj_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;15;add_test;C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[caidj_tests]=] "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/build/debug/tests/Release/caidj_tests.exe")
  set_tests_properties([=[caidj_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;15;add_test;C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[caidj_tests]=] "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/build/debug/tests/MinSizeRel/caidj_tests.exe")
  set_tests_properties([=[caidj_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;15;add_test;C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[caidj_tests]=] "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/build/debug/tests/RelWithDebInfo/caidj_tests.exe")
  set_tests_properties([=[caidj_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;15;add_test;C:/Users/Soubhik Kumar Gon/Desktop/codes/caidj/tests/CMakeLists.txt;0;")
else()
  add_test([=[caidj_tests]=] NOT_AVAILABLE)
endif()
