cmake_minimum_required(VERSION 3.16)
project(text_analyzer_backend C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable warnings (works on GCC/Clang)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ------------------------------------------------------------
# Core Library (Eigenentwicklung)
# ------------------------------------------------------------
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

# ------------------------------------------------------------
# Unit Tests (TDD) – [EXTERN] Unity Test Framework
# ------------------------------------------------------------
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

# ------------------------------------------------------------
# API Dependencies – [EXTERN] CivetWeb + [EXTERN] yyjson
# ------------------------------------------------------------

find_package(Threads REQUIRED)

add_library(civetweb
  external/civetweb/src/civetweb.c
)

target_include_directories(civetweb PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/external/civetweb/include
)

# [EXTERN] CivetWeb: disable built-in SSL/TLS for simpler builds
# We run plain HTTP; TLS can be terminated by a reverse proxy (nginx/caddy) later.
target_compile_definitions(civetweb PRIVATE NO_SSL)

target_link_libraries(civetweb PUBLIC Threads::Threads)


add_library(yyjson
  external/yyjson/src/yyjson.c
)

target_include_directories(yyjson PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/external/yyjson/src
)

# ------------------------------------------------------------
# API Executable
# ------------------------------------------------------------

add_executable(api_server
  src/api/main.c
)

target_include_directories(api_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(api_server PRIVATE
  core
  civetweb
  yyjson
  Threads::Threads
)
