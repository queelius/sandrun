#include "sandbox.h"
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <seccomp.h>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace sandrun {

class Sandbox::Impl {
public:
    SecurityLevel security_level_;
    ResourceLimits resource_limits_;
    GPUConfig gpu_config_;
    std::string working_directory_;
    std::unordered_map<std::string, std::string> environment_vars_;
    bool allow_network_ = false;
    std::vector<std::pair<std::string, bool>> allowed_paths_;  // path, read_write
    pid_t child_pid_ = -1;
    bool namespace_setup_ = false;
    
    Impl(SecurityLevel level) : security_level_(level) {
        working_directory_ = "/tmp/sandrun_" + std::to_string(getpid());
        std::filesystem::create_directories(working_directory_);
    }
    
    ~Impl() {
        cleanupGPUContext();
        if (child_pid_ > 0) {
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, nullptr, 0);
        }
        if (std::filesystem::exists(working_directory_)) {
            std::filesystem::remove_all(working_directory_);
        }
    }
    
    ExecutionResult executeCode(const std::string& code, InterpreterType interpreter) {
        std::string script_file = createTempScript(code, interpreter);
        return executeScript(script_file, interpreter);
    }
    
    ExecutionResult executeScript(const std::string& script_path, InterpreterType interpreter) {
        std::vector<std::string> command = buildCommand(script_path, interpreter);
        return executeCommand(command);
    }
    
    ExecutionResult executeCommand(const std::vector<std::string>& command) {
        ExecutionResult result;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Create pipes for stdout/stderr
        int stdout_pipe[2], stderr_pipe[2];
        if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
            result.error_message = "Failed to create pipes";
            result.exit_code = -1;
            return result;
        }
        
        // Fork process with namespace isolation
        child_pid_ = createIsolatedProcess();
        if (child_pid_ == -1) {
            result.error_message = "Failed to fork process";
            result.exit_code = -1;
            return result;
        }
        
        if (child_pid_ == 0) {
            // Child process
            executeInChildProcess(command, stdout_pipe, stderr_pipe);
            _exit(127); // Should never reach here
        }
        
        // Parent process
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Set up resource monitoring
        std::thread monitor_thread([this, &result, start_time]() {
            monitorResourceUsage(result, start_time);
        });
        
        // Read output from child
        result.stdout_output = readFromPipe(stdout_pipe[0]);
        result.stderr_output = readFromPipe(stderr_pipe[0]);
        
        // Wait for child to complete
        int status;
        int wait_result = waitpid(child_pid_, &status, 0);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
        
        if (wait_result == -1) {
            result.error_message = "Failed to wait for child process";
            result.exit_code = -1;
        } else if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exit_code = -WTERMSIG(status);
            if (WTERMSIG(status) == SIGKILL) {
                result.timeout_occurred = true;
                result.error_message = "Process killed due to timeout or resource limits";
            }
        }
        
        monitor_thread.join();
        
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        child_pid_ = -1;
        
        collectOutputFiles(result);
        return result;
    }
    
private:
    std::string createTempScript(const std::string& code, InterpreterType interpreter) {
        std::string extension = getScriptExtension(interpreter);
        std::string script_path = working_directory_ + "/script" + extension;
        
        std::ofstream script_file(script_path);
        if (!script_file) {
            throw std::runtime_error("Failed to create temporary script file");
        }
        
        script_file << code;
        script_file.close();
        
        // Make executable if needed
        if (interpreter == InterpreterType::CPP) {
            chmod(script_path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
        }
        
        return script_path;
    }
    
    std::vector<std::string> buildCommand(const std::string& script_path, InterpreterType interpreter) {
        switch (interpreter) {
            case InterpreterType::PYTHON:
                return {"python3", script_path};
            case InterpreterType::NODEJS:
                return {"node", script_path};
            case InterpreterType::RUST:
                return {"rustc", script_path, "-o", script_path + ".exe", "&&", script_path + ".exe"};
            case InterpreterType::GO:
                return {"go", "run", script_path};
            case InterpreterType::CPP:
                return {"g++", "-o", script_path + ".exe", script_path, "&&", script_path + ".exe"};
            case InterpreterType::CUDA:
                return {"nvcc", "-o", script_path + ".exe", script_path, "&&", script_path + ".exe"};
            default:
                throw std::invalid_argument("Unsupported interpreter type");
        }
    }
    
    std::string getScriptExtension(InterpreterType interpreter) {
        switch (interpreter) {
            case InterpreterType::PYTHON: return ".py";
            case InterpreterType::NODEJS: return ".js";
            case InterpreterType::RUST: return ".rs";
            case InterpreterType::GO: return ".go";
            case InterpreterType::CPP: return ".cpp";
            case InterpreterType::CUDA: return ".cu";
            default: return ".txt";
        }
    }
    
    pid_t createIsolatedProcess() {
        int clone_flags = SIGCHLD;
        
        if (security_level_ >= SecurityLevel::STANDARD) {
            clone_flags |= CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC;
            
            if (!allow_network_) {
                clone_flags |= CLONE_NEWNET;
            }
            
            if (security_level_ >= SecurityLevel::PARANOID) {
                clone_flags |= CLONE_NEWUSER;
            }
        }
        
        // Use clone for namespace creation
        char* stack = (char*)malloc(1024 * 1024);
        char* stack_top = stack + 1024 * 1024;
        
        pid_t pid = clone([](void* arg) -> int {
            return 0;  // Placeholder - actual execution happens in executeInChildProcess
        }, stack_top, clone_flags, nullptr);
        
        if (pid > 0) {
            namespace_setup_ = true;
        }
        
        return pid;
    }
    
    void executeInChildProcess(const std::vector<std::string>& command, 
                              int stdout_pipe[2], int stderr_pipe[2]) {
        // Redirect stdout/stderr to pipes
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        
        // Set resource limits
        applyResourceLimits();
        
        // Set up security policies
        if (security_level_ >= SecurityLevel::STANDARD) {
            setupSeccompFilter();
        }
        
        // Change working directory
        if (chdir(working_directory_.c_str()) != 0) {
            perror("chdir");
            _exit(1);
        }
        
        // Set environment variables
        for (const auto& [key, value] : environment_vars_) {
            setenv(key.c_str(), value.c_str(), 1);
        }
        
        // Execute command
        std::vector<char*> argv;
        for (const auto& arg : command) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(argv[0], argv.data());
        perror("execvp");
        _exit(127);
    }
    
    void applyResourceLimits() {
        struct rlimit limit;
        
        // Memory limit
        limit.rlim_cur = limit.rlim_max = resource_limits_.max_memory_mb * 1024 * 1024;
        setrlimit(RLIMIT_AS, &limit);
        
        // CPU time limit
        limit.rlim_cur = limit.rlim_max = resource_limits_.max_cpu_time_sec;
        setrlimit(RLIMIT_CPU, &limit);
        
        // File size limit
        limit.rlim_cur = limit.rlim_max = resource_limits_.max_file_size_mb * 1024 * 1024;
        setrlimit(RLIMIT_FSIZE, &limit);
        
        // Process limit
        limit.rlim_cur = limit.rlim_max = resource_limits_.max_processes;
        setrlimit(RLIMIT_NPROC, &limit);
        
        // File descriptor limit
        limit.rlim_cur = limit.rlim_max = resource_limits_.max_open_files;
        setrlimit(RLIMIT_NOFILE, &limit);
        
        // Set wall time alarm
        alarm(resource_limits_.max_wall_time_sec);
    }
    
    void setupSeccompFilter() {
        scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ERRNO(EPERM));
        if (!ctx) {
            return;
        }
        
        // Allow essential syscalls
        std::vector<int> allowed_syscalls = {
            SCMP_SYS(read), SCMP_SYS(write), SCMP_SYS(exit), SCMP_SYS(exit_group),
            SCMP_SYS(brk), SCMP_SYS(mmap), SCMP_SYS(munmap), SCMP_SYS(mprotect),
            SCMP_SYS(open), SCMP_SYS(close), SCMP_SYS(fstat), SCMP_SYS(lseek),
            SCMP_SYS(rt_sigaction), SCMP_SYS(rt_sigprocmask), SCMP_SYS(rt_sigreturn),
            SCMP_SYS(execve), SCMP_SYS(access), SCMP_SYS(arch_prctl)
        };
        
        for (int syscall : allowed_syscalls) {
            seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0);
        }
        
        // Load and apply the filter
        seccomp_load(ctx);
        seccomp_release(ctx);
    }
    
    void monitorResourceUsage(ExecutionResult& result, 
                             std::chrono::high_resolution_clock::time_point start_time) {
        while (child_pid_ > 0 && kill(child_pid_, 0) == 0) {
            // Check wall time timeout
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - start_time);
            
            if (elapsed.count() > resource_limits_.max_wall_time_sec) {
                kill(child_pid_, SIGKILL);
                result.timeout_occurred = true;
                break;
            }
            
            // Monitor memory usage
            std::string status_file = "/proc/" + std::to_string(child_pid_) + "/status";
            std::ifstream status(status_file);
            std::string line;
            
            while (std::getline(status, line)) {
                if (line.find("VmRSS:") == 0) {
                    std::istringstream iss(line);
                    std::string key, value, unit;
                    iss >> key >> value >> unit;
                    size_t memory_kb = std::stoull(value);
                    result.memory_used_mb = memory_kb / 1024;
                    break;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::string readFromPipe(int fd) {
        std::string output;
        char buffer[4096];
        ssize_t bytes_read;
        
        while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            output.append(buffer, bytes_read);
        }
        
        return output;
    }
    
    void collectOutputFiles(ExecutionResult& result) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(working_directory_)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename != "script.py" && filename != "script.js" && 
                        filename.find("script") != 0) {
                        result.output_files.push_back(entry.path().string());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error&) {
            // Ignore filesystem errors during output collection
        }
    }
    
    void cleanupGPUContext() {
        if (gpu_config_.enabled) {
            // GPU cleanup would go here - placeholder for actual CUDA/ROCm cleanup
        }
    }
};

// Sandbox implementation
Sandbox::Sandbox(SecurityLevel level) : pImpl(std::make_unique<Impl>(level)) {}

Sandbox::~Sandbox() = default;

void Sandbox::setResourceLimits(const ResourceLimits& limits) {
    pImpl->resource_limits_ = limits;
}

void Sandbox::setGPUConfig(const GPUConfig& config) {
    pImpl->gpu_config_ = config;
}

void Sandbox::setWorkingDirectory(const std::string& path) {
    pImpl->working_directory_ = path;
    std::filesystem::create_directories(path);
}

void Sandbox::addEnvironmentVariable(const std::string& key, const std::string& value) {
    pImpl->environment_vars_[key] = value;
}

void Sandbox::allowNetworkAccess(bool allow) {
    pImpl->allow_network_ = allow;
}

void Sandbox::allowFileSystemAccess(const std::string& path, bool read_write) {
    pImpl->allowed_paths_.emplace_back(path, read_write);
}

ExecutionResult Sandbox::executeCode(const std::string& code, InterpreterType interpreter) {
    return pImpl->executeCode(code, interpreter);
}

ExecutionResult Sandbox::executeScript(const std::string& script_path, InterpreterType interpreter) {
    return pImpl->executeScript(script_path, interpreter);
}

ExecutionResult Sandbox::executeCommand(const std::vector<std::string>& command) {
    return pImpl->executeCommand(command);
}

bool Sandbox::isGPUAvailable() const {
    return pImpl->gpu_config_.enabled && std::filesystem::exists("/dev/nvidia0");
}

std::vector<std::string> Sandbox::getAvailableGPUDevices() const {
    std::vector<std::string> devices;
    for (int i = 0; i < 16; ++i) {
        std::string device_path = "/dev/nvidia" + std::to_string(i);
        if (std::filesystem::exists(device_path)) {
            devices.push_back(device_path);
        }
    }
    return devices;
}

void Sandbox::initializeGPUContext() {
    if (pImpl->gpu_config_.enabled) {
        // GPU initialization would go here - placeholder for CUDA/ROCm init
    }
}

void Sandbox::cleanupGPUContext() {
    pImpl->cleanupGPUContext();
}

std::vector<std::string> Sandbox::getActiveSyscalls() const {
    // Placeholder - would require ptrace or eBPF integration
    return {"read", "write", "mmap", "brk"};
}

std::unordered_map<std::string, double> Sandbox::getResourceUsage() const {
    std::unordered_map<std::string, double> usage;
    usage["cpu_percent"] = 0.0;  // Placeholder
    usage["memory_mb"] = 0.0;    // Placeholder  
    usage["gpu_memory_mb"] = 0.0; // Placeholder
    return usage;
}

// SandboxManager implementation
std::unique_ptr<Sandbox> SandboxManager::createSecureSandbox(SecurityLevel level) {
    return std::make_unique<Sandbox>(level);
}

bool SandboxManager::testSystemCapabilities() {
    // Test if we can create namespaces
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - try to create a namespace
        if (unshare(CLONE_NEWPID) == 0) {
            _exit(0);  // Success
        }
        _exit(1);  // Failure
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return false;
}

std::vector<std::string> SandboxManager::getRequiredCapabilities() {
    return {"CAP_SYS_ADMIN", "CAP_SYS_PTRACE", "CAP_SYS_CHROOT"};
}

} // namespace sandrun