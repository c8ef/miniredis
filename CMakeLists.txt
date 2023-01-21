cmake_minimum_required(VERSION 3.20)

project(miniredis)

add_library(match SHARED src/match.c)
target_include_directories(match PUBLIC ${PROJECT_SOURCE_DIR}/include)

enable_testing()

add_executable(match_test test/match_test.c)
target_include_directories(match_test PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(match_test match)

add_test(match_unit_test match_test)
