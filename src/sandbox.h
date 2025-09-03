#pragma once

#include <string>
#include <chrono>
#include <memory>
#include "constants.h"

namespace sandrun {

// Job execution result
struct JobResult {
    std::string job_id;
    std::string output;
    std::string error;
    int exit_code;
    double cpu_seconds;
    size_t memory_bytes;
    std::chrono::milliseconds wall_time;
    
    // Privacy: clear sensitive data
    void clear() {
        // Explicitly overwrite strings before deallocation
        std::fill(output.begin(), output.end(), '\0');
        std::fill(error.begin(), error.end(), '\0');
        output.clear();
        error.clear();
    }
    
    ~JobResult() { clear(); }
};

// Sandbox configuration
struct SandboxConfig {
    size_t memory_limit_bytes = DEFAULT_MEMORY_LIMIT_BYTES;
    size_t cpu_quota_us = DEFAULT_CPU_QUOTA_US;
    size_t cpu_period_us = DEFAULT_CPU_PERIOD_US;
    std::chrono::seconds timeout = std::chrono::seconds(DEFAULT_TIMEOUT_SECONDS);
    bool allow_network = false;                      // Airgapped by default
    std::string interpreter = "python3";              // Default interpreter
    
    // GPU configuration
    bool gpu_enabled = false;                        // GPU access disabled by default
    int gpu_device_id = 0;                          // Which GPU to use (0-based)
    size_t gpu_memory_limit_bytes = DEFAULT_GPU_MEMORY_LIMIT_BYTES;
};

// Execute code in sandboxed environment
class Sandbox {
public:
    Sandbox(const SandboxConfig& config = SandboxConfig{});
    ~Sandbox();
    
    // Execute code and return result
    // Code is passed by value and cleared after use (privacy)
    JobResult execute(std::string code, const std::string& job_id);
    
    // Kill a running job
    bool kill(const std::string& job_id);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace sandrun