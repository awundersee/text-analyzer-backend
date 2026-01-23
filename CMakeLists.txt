cmake_minimum_required(VERSION 3.16)
project(text_analyzer_backend C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable warnings (works on GCC/Clang)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Core library
add_library(core
  src/core/tokenizer.c
  src/core/stopwords.c
  src/core/freq.c
  src/core/aggregate.c
  src/core/bigrams.c
  src/core/bigram_aggregate.c
  src/core/topk.c
)

target_include_directories(core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# -----------------------------
# [EXTERN] Unity test framework (git submodule)
# -----------------------------
enable_testing()

add_executable(unit_tests
  tests/test_tokenizer.c
  tests/test_aggregate.c
  tests/test_bigrams.c
  tests/test_bigram_aggregate.c
  tests/test_topk.c
  external/unity/src/unity.c
)

# Ensure runtime test data is available from the build directory
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/data/stopwords_de.txt
  ${CMAKE_CURRENT_BINARY_DIR}/stopwords_de.txt
  COPYONLY
)

target_include_directories(unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/external/unity/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(unit_tests PRIVATE core)

add_test(NAME unit_tests COMMAND unit_tests)