cmake_minimum_required(VERSION 3.14)
project(sandrun VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Security-focused compile flags
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wformat-security
    -Werror=format-security
    -fstack-protector-strong
    -fPIE -fPIC
    -D_FORTIFY_SOURCE=2
)

# Find required system libraries
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Main executable
add_executable(sandrun
    src/main.cpp
    src/sandbox.cpp
    src/http_server.cpp
    src/multipart.cpp
    src/rate_limiter.cpp
    src/job_executor.cpp
    src/job_hash.cpp
    src/proof.cpp
    src/websocket.cpp
    src/file_utils.cpp
    src/environment_manager.cpp
    src/worker_identity.cpp
)

target_link_libraries(sandrun
    PRIVATE
    Threads::Threads
    OpenSSL::Crypto
    seccomp  # For syscall filtering
    cap      # For capability management
)

# Installation
install(TARGETS sandrun
    RUNTIME DESTINATION bin
)

# Enable testing
enable_testing()

# Add tests subdirectory
add_subdirectory(tests)