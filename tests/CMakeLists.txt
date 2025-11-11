cmake_minimum_required(VERSION 3.14)

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Google Test setup
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Unit tests
add_executable(unit_tests
    unit/test_sandbox.cpp
    unit/test_rate_limiter.cpp
    unit/test_proof.cpp
    unit/test_multipart.cpp
    unit/test_job_executor.cpp
    unit/test_file_utils.cpp
    unit/test_worker_identity.cpp
    unit/test_environment_manager.cpp
    unit/test_job_hash.cpp
    unit/test_http_server.cpp
    unit/test_websocket.cpp
    ${CMAKE_SOURCE_DIR}/src/sandbox.cpp
    ${CMAKE_SOURCE_DIR}/src/rate_limiter.cpp
    ${CMAKE_SOURCE_DIR}/src/proof.cpp
    ${CMAKE_SOURCE_DIR}/src/multipart.cpp
    ${CMAKE_SOURCE_DIR}/src/job_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/file_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/worker_identity.cpp
    ${CMAKE_SOURCE_DIR}/src/environment_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/job_hash.cpp
    ${CMAKE_SOURCE_DIR}/src/http_server.cpp
    ${CMAKE_SOURCE_DIR}/src/websocket.cpp
)

target_link_libraries(unit_tests
    gtest_main
    Threads::Threads
    OpenSSL::Crypto
    seccomp
    cap
)

# Integration tests
add_executable(integration_tests
    integration/test_job_execution.cpp
    integration/test_gpu_support.cpp
    integration/test_job_verification.cpp
    integration/test_worker_signing.cpp
    integration/test_environment_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/sandbox.cpp
    ${CMAKE_SOURCE_DIR}/src/http_server.cpp
    ${CMAKE_SOURCE_DIR}/src/multipart.cpp
    ${CMAKE_SOURCE_DIR}/src/rate_limiter.cpp
    ${CMAKE_SOURCE_DIR}/src/job_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/job_hash.cpp
    ${CMAKE_SOURCE_DIR}/src/proof.cpp
    ${CMAKE_SOURCE_DIR}/src/websocket.cpp
    ${CMAKE_SOURCE_DIR}/src/file_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/environment_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/worker_identity.cpp
)

target_link_libraries(integration_tests
    gtest_main
    Threads::Threads
    OpenSSL::Crypto
    seccomp
    cap
)

# Add tests
add_test(NAME unit_tests COMMAND unit_tests)
add_test(NAME integration_tests COMMAND integration_tests)