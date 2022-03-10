cmake_minimum_required(VERSION 3.14)
project(safe-cpp-csv-parser)

set(CMAKE_CXX_STANDARD 11)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)

FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(csv_test tests/csv_test.cc)
target_include_directories(csv_test PUBLIC include)
target_link_libraries(csv_test gtest_main)

include(GoogleTest)
gtest_discover_tests(csv_test)
