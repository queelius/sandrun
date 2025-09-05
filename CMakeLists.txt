cmake_minimum_required(VERSION 3.16)
project(Sandrun VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags for testing and coverage
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fprofile-arcs -ftest-coverage --coverage")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Check for system capabilities
pkg_check_modules(SECCOMP REQUIRED libseccomp)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${CURL_INCLUDE_DIRS})

# Core library source files
set(SANDRUN_SOURCES
    src/sandbox.cpp
    src/proof.cpp
    src/rate_limiter.cpp
    src/multipart.cpp
    src/http_server.cpp
    src/job_executor.cpp
    src/execution_trace.cpp
    src/gpu_manager.cpp
    src/security_policy.cpp
    src/blockchain_client.cpp
    src/ipfs_client.cpp
    src/consensus_engine.cpp
)

# Create main library
add_library(sandrun STATIC ${SANDRUN_SOURCES})
target_link_libraries(sandrun 
    ${SECCOMP_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
    Threads::Threads
    dl
)

# Main executable
add_executable(sandrun-server src/main.cpp)
target_link_libraries(sandrun-server sandrun)

# Google Test setup
enable_testing()
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Download and build GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Unit test sources
set(UNIT_TEST_SOURCES
    tests/unit/test_sandbox.cpp
    tests/unit/test_rate_limiter.cpp
    tests/unit/test_proof.cpp
    tests/unit/test_multipart.cpp
    tests/unit/test_execution_trace.cpp
    tests/unit/test_gpu_manager.cpp
    tests/unit/test_security_policy.cpp
    tests/unit/test_consensus_engine.cpp
)

# Integration test sources
set(INTEGRATION_TEST_SOURCES
    tests/integration/test_job_execution.cpp
    tests/integration/test_gpu_support.cpp
    tests/integration/test_api_endpoints.cpp
    tests/integration/test_distributed_consensus.cpp
    tests/integration/test_blockchain_integration.cpp
    tests/integration/test_ipfs_integration.cpp
    tests/integration/test_performance.cpp
)

# Create unit test executable
add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests 
    sandrun
    gtest
    gtest_main
    gmock
    gmock_main
)

# Create integration test executable
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests 
    sandrun
    gtest
    gtest_main
    gmock
    gmock_main
)

# Add tests to CTest
add_test(NAME UnitTests COMMAND unit_tests)
add_test(NAME IntegrationTests COMMAND integration_tests)

# Coverage target
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    find_program(LCOV_PATH lcov)
    find_program(GENHTML_PATH genhtml)
    
    if(LCOV_PATH AND GENHTML_PATH)
        add_custom_target(coverage
            COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info
            COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/googletest/*' '*/tests/*' --output-file coverage_filtered.info
            COMMAND ${GENHTML_PATH} coverage_filtered.info --output-directory coverage_html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            DEPENDS unit_tests integration_tests
            COMMENT "Generating code coverage report"
        )
    endif()
endif()

# Custom test target that runs all tests with verbose output
add_custom_target(test_all
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/unit_tests --gtest_color=yes --gtest_output=xml:unit_test_results.xml
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/integration_tests --gtest_color=yes --gtest_output=xml:integration_test_results.xml
    DEPENDS unit_tests integration_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Performance benchmark target
add_custom_target(benchmark
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/integration_tests --gtest_filter="*Performance*" --gtest_color=yes
    DEPENDS integration_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)