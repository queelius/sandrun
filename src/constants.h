#pragma once

#include <cstddef>  // for size_t

namespace sandrun {

// Memory limits
constexpr size_t DEFAULT_MEMORY_LIMIT_BYTES = 512 * 1024 * 1024;  // 512MB
constexpr size_t MAX_OUTPUT_SIZE = 10 * 1024 * 1024;              // 10MB max output
constexpr size_t MAX_REQUEST_SIZE = 100 * 1024 * 1024;            // 100MB max request
constexpr size_t MAX_JOB_FILES_SIZE = 100 * 1024 * 1024;         // 100MB max for job files
constexpr size_t TMPFS_SIZE_LIMIT = 100 * 1024 * 1024;           // 100MB tmpfs

// Time limits
constexpr size_t DEFAULT_CPU_QUOTA_US = 10 * 1000 * 1000;        // 10 CPU seconds
constexpr size_t DEFAULT_CPU_PERIOD_US = 60 * 1000 * 1000;       // Per 60 seconds
constexpr int DEFAULT_TIMEOUT_SECONDS = 300;                      // 5 minutes
constexpr int JOB_CLEANUP_AFTER_SECONDS = 60;                     // Auto-delete after 1 minute

// Process limits
constexpr int MAX_PROCESSES_PER_JOB = 32;                        // Max threads/processes
constexpr int MAX_OPEN_FILES = 256;                              // Max file descriptors

// GPU limits
constexpr size_t DEFAULT_GPU_MEMORY_LIMIT_BYTES = 8ULL * 1024 * 1024 * 1024;  // 8GB default
constexpr int DEFAULT_GPU_TIMEOUT_SECONDS = 600;                  // 10 minutes for GPU jobs
constexpr int MAX_GPUS_PER_JOB = 1;                              // Single GPU per job for now

// Rate limiting
constexpr int MAX_CONCURRENT_JOBS_PER_IP = 2;                    // Per IP limit
constexpr int MAX_JOBS_PER_HOUR = 10;                           // Hourly job limit
constexpr double CPU_SECONDS_PER_MINUTE = 10.0;                  // CPU quota per minute
constexpr int RATE_LIMIT_CLEANUP_MINUTES = 60;                   // Cleanup inactive IPs

// Buffer sizes
constexpr size_t PIPE_BUFFER_SIZE = 4096;                        // Read buffer size
constexpr size_t INITIAL_HTTP_BUFFER = 8192;                     // Initial HTTP buffer
constexpr size_t SECURE_DELETE_CHUNK = 1024 * 1024;              // 1MB chunks for secure delete

// Network
constexpr int DEFAULT_PORT = 8443;                               // Default server port
constexpr int LISTEN_BACKLOG = 10;                               // Socket listen backlog

} // namespace sandrun