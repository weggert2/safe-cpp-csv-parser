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

add_compile_options(
    -Wall
    -Wextra
    -Werror
    -fPIC
    -fno-exceptions
    -Wold-style-cast
    -Wdouble-promotion
    -Wshadow
    -Wformat=2
    -Wno-narrowing
)

target_include_directories(csv_test PUBLIC include)
target_link_libraries(csv_test gtest_main)

include(GoogleTest)
gtest_discover_tests(csv_test)

add_custom_command(
    TARGET csv_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/tests/1.csv
    ${CMAKE_SOURCE_DIR}/tests/2.csv
    ${CMAKE_SOURCE_DIR}/tests/3.csv
    ${CMAKE_SOURCE_DIR}/tests/4.csv
    ${CMAKE_SOURCE_DIR}/tests/5.csv
    ${CMAKE_SOURCE_DIR}/tests/6.csv
    ${CMAKE_SOURCE_DIR}/tests/7.csv
    ${CMAKE_SOURCE_DIR}/tests/8.csv
    ${CMAKE_SOURCE_DIR}/tests/9.csv
    ${CMAKE_SOURCE_DIR}/tests/10.csv
    ${CMAKE_CURRENT_BINARY_DIR}
)