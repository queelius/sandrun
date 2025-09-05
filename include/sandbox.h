#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>
#include <unordered_map>

namespace sandrun {

enum class InterpreterType {
    PYTHON,
    NODEJS,
    RUST,
    GO,
    CPP,
    CUDA
};

enum class SecurityLevel {
    MINIMAL,    // Basic process isolation
    STANDARD,   // Namespace isolation + seccomp
    PARANOID,   // Full isolation + network blocking
    GPU_SECURE  // GPU-specific security policies
};

struct ResourceLimits {
    size_t max_memory_mb = 1024;        // Memory limit in MB
    size_t max_cpu_time_sec = 300;      // CPU time limit
    size_t max_wall_time_sec = 600;     // Wall time limit
    size_t max_file_size_mb = 100;      // Output file size limit
    size_t max_processes = 1;           // Max concurrent processes
    size_t max_open_files = 64;         // Max open file descriptors
    size_t max_gpu_memory_mb = 0;       // GPU memory limit (0 = no GPU)
};

struct GPUConfig {
    bool enabled = false;
    std::string device_type = "cuda";   // cuda, rocm, intel
    std::vector<int> device_ids;        // Specific GPU devices to use
    size_t memory_limit_mb = 0;         // Per-device memory limit
    bool exclusive_mode = true;         // Exclusive GPU access
};

struct ExecutionResult {
    int exit_code = 0;
    std::string stdout_output;
    std::string stderr_output;
    std::chrono::microseconds execution_time{0};
    size_t memory_used_mb = 0;
    size_t gpu_memory_used_mb = 0;
    bool timeout_occurred = false;
    std::string error_message;
    std::vector<std::string> output_files;
};

class SandboxViolationException : public std::runtime_error {
public:
    explicit SandboxViolationException(const std::string& message)
        : std::runtime_error("Sandbox violation: " + message) {}
};

class Sandbox {
public:
    Sandbox(SecurityLevel level = SecurityLevel::STANDARD);
    ~Sandbox();

    // Configuration methods
    void setResourceLimits(const ResourceLimits& limits);
    void setGPUConfig(const GPUConfig& config);
    void setWorkingDirectory(const std::string& path);
    void addEnvironmentVariable(const std::string& key, const std::string& value);
    void allowNetworkAccess(bool allow = true);
    void allowFileSystemAccess(const std::string& path, bool read_write = false);
    
    // Execution methods
    ExecutionResult executeCode(const std::string& code, InterpreterType interpreter);
    ExecutionResult executeScript(const std::string& script_path, InterpreterType interpreter);
    ExecutionResult executeCommand(const std::vector<std::string>& command);
    
    // Security enforcement methods
    void enforceTimeoutLimits();
    void enforceNetworkIsolation();
    void enforceFileSystemIsolation();
    void enforceResourceLimits();
    
    // GPU-specific methods
    bool isGPUAvailable() const;
    std::vector<std::string> getAvailableGPUDevices() const;
    void initializeGPUContext();
    void cleanupGPUContext();
    
    // Monitoring and debugging
    std::vector<std::string> getActiveSyscalls() const;
    std::unordered_map<std::string, double> getResourceUsage() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Utility functions for sandbox management
class SandboxManager {
public:
    static std::unique_ptr<Sandbox> createSecureSandbox(SecurityLevel level);
    static bool testSystemCapabilities();
    static std::vector<std::string> getRequiredCapabilities();
    static void setupSeccompFilter();
    static void setupNamespaceIsolation();
};

} // namespace sandrun