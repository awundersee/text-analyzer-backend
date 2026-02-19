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

  src/core/dict.c
  src/core/id_freq.c
  src/core/id_bigrams.c  
  src/metrics/metrics.c
)

target_include_directories(core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ------------------------------------------------------------
# View-Filter (Eigenentwicklung)
# ------------------------------------------------------------

add_library(view
  src/view/topk.c
)

target_include_directories(view PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(view PUBLIC core)

# ------------------------------------------------------------
# yyjson ergänzen
# ------------------------------------------------------------

add_library(yyjson
  external/yyjson/src/yyjson.c
)

target_include_directories(yyjson PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/external/yyjson/src
)

# ------------------------------------------------------------
# App Layer (Analyse-Orchestrierung)
# ------------------------------------------------------------
add_library(app STATIC 
  src/app/analyze.c
  src/app/pipeline_string.c
  src/app/pipeline_id.c
  src/input/request_validate.c
  )

target_include_directories(app PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(app PUBLIC
  core
  view
  yyjson
)

# ------------------------------------------------------------
# Unit Tests (TDD) – [EXTERN] Unity Test Framework
# ------------------------------------------------------------
enable_testing()

add_executable(unit_tests
  tests/unit/test_tokenizer.c
  tests/unit/test_aggregate.c
  tests/unit/test_bigrams.c
  tests/unit/test_bigram_aggregate.c
  tests/unit/test_topk.c
  tests/unit/test_pipeline_parity.c
  tests/unit/test_pipeline_force.c
  tests/unit/test_request_validate.c
  external/unity/src/unity.c
)

# Ensure runtime test data is available from the build directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/data)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/data/stopwords_de.txt
  ${CMAKE_CURRENT_BINARY_DIR}/data/stopwords_de.txt
  COPYONLY
)

target_include_directories(unit_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/external/unity/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(unit_tests PRIVATE
  app
)

add_test(NAME unit_tests COMMAND $<TARGET_FILE:unit_tests>)

# ------------------------------------------------------------
# Run unit tests automatically on build (portable single/multi-config)
# ------------------------------------------------------------

if(CMAKE_CONFIGURATION_TYPES) # Multi-config generator (Xcode, VS)
  add_custom_target(run_unit_tests ALL
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> --output-on-failure
    DEPENDS unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests (ctest)"
  )
else() # Single-config generator (Unix Makefiles, Ninja)
  add_custom_target(run_unit_tests ALL
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests (ctest)"
  )
endif()

# ------------------------------------------------------------
# Manual targets: API tests / Performance tests (run on demand)
# ------------------------------------------------------------

add_custom_target(test_api
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/scripts/run_api_tests.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running API tests"
)

add_custom_target(test_perf_pipeline
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/scripts/run_perf_tests_dual_pipelines.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running pipeline performance tests"
)

add_custom_target(test_perf_mem
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/scripts/run_perf_tests_dual_pipelines_mem.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running pipeline performance tests"
)

add_custom_target(test_perf_api_concurrency
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/scripts/run_perf_api_concurrency.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running API concurrency memory tests"
)

add_custom_target(test_topk_measure
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/scripts/run_perf_topk_measure.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running CLI topK performance tests"
)

add_custom_target(test_topk_measure_perf
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/scripts/run_perf_cli_topk_k20_id.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running CLI Top-20 Comparison Performance tests"
)

add_custom_target(test_stress_single
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/stress/scripts/run_stress_cli_single_page.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running CLI single stress tests"
)

add_custom_target(test_stress_multi
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/stress/scripts/run_stress_cli_multi_page.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running CLI multi stress tests"
)

add_custom_target(test_error
  COMMAND ${CMAKE_COMMAND} -E env
          CLI_BIN=$<TARGET_FILE:analyze_cli>
          API_URL=http://127.0.0.1:8080/analyze
          bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/error/scripts/run_suite.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running all error test suites (CLI + API)"
)

add_custom_target(test_perf_api_prod_avg5
  COMMAND ${CMAKE_COMMAND} -E env
          API_URL=https://textanalyse.wundersee.dev/analyze
          DATA_DIR=tests/performance/data
          RUNS=5
          OUT_DIR=tests/performance/results
          bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/scripts/run_api_prod_avg5.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running API prod performance test (avg of 5 runs)"
)

add_custom_target(test_concurrency_api_prod
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance/scripts/run_api_prod_concurrency.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running API prod concurrency tests"
)

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
  app
  civetweb
  Threads::Threads
  m
)

# ------------------------------------------------------------
# CLI Executable (Algorithmus-Benchmark, ohne HTTP)
# ------------------------------------------------------------
add_executable(analyze_cli
  src/cli/main.c
  src/cli/batch.c
)

target_include_directories(analyze_cli PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(analyze_cli PRIVATE
  app
  m
)