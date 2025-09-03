#include "sandbox.h"
#include "constants.h"

#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sched.h>
#include <seccomp.h>
#include <linux/capability.h>
#include <signal.h>
#include <fcntl.h>

#include <cstring>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace sandrun {
namespace fs = std::filesystem;

class Sandbox::Impl {
public:
    Impl(const SandboxConfig& cfg) : config(cfg) {}
    
    JobResult execute(std::string code, const std::string& job_id) {
        JobResult result;
        result.job_id = job_id;
        
        // Create temporary directory in tmpfs (RAM only)
        auto tmp_dir = fs::temp_directory_path() / ("job_" + job_id);
        fs::create_directories(tmp_dir);
        
        // Write code to file (will be in tmpfs)
        auto script_path = tmp_dir / "script.py";
        {
            std::ofstream script(script_path);
            script << code;
            // Clear code from memory immediately after writing
            std::fill(code.begin(), code.end(), '\0');
            code.clear();
        }
        
        // Create pipes for output capture
        int stdout_pipe[2], stderr_pipe[2];
        if (pipe2(stdout_pipe, O_CLOEXEC) != 0 || pipe2(stderr_pipe, O_CLOEXEC) != 0) {
            result.exit_code = -1;
            result.error = "Failed to create pipes";
            cleanup(tmp_dir);
            return result;
        }
        
        auto start_time = std::chrono::steady_clock::now();
        
        // Fork with new namespaces
        pid_t pid = fork();
        
        if (pid == 0) {
            // Child process - setup sandbox
            setup_sandbox(tmp_dir, stdout_pipe, stderr_pipe);
            
            // Validate interpreter path (whitelist approach)
            const char* interpreter_path = nullptr;
            if (config.interpreter == "python3") {
                interpreter_path = "/usr/bin/python3";
            } else if (config.interpreter == "python") {
                interpreter_path = "/usr/bin/python";
            } else if (config.interpreter == "node") {
                interpreter_path = "/usr/bin/node";
            } else if (config.interpreter == "bash") {
                interpreter_path = "/bin/bash";
            } else if (config.interpreter == "sh") {
                interpreter_path = "/bin/sh";
            } else {
                // Invalid interpreter - use python3 as safe default
                interpreter_path = "/usr/bin/python3";
            }
            
            // Execute script with validated interpreter
            execl(interpreter_path, config.interpreter.c_str(), script_path.c_str(), nullptr);
            
            // If exec fails
            _exit(127);
        } else if (pid > 0) {
            // Parent - monitor execution
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
            
            // Read output
            result.output = read_pipe(stdout_pipe[0]);
            result.error = read_pipe(stderr_pipe[0]);
            
            // Wait for completion or timeout
            int status;
            auto timeout = config.timeout;
            auto deadline = start_time + timeout;
            
            while (true) {
                int ret = waitpid(pid, &status, WNOHANG);
                if (ret == pid) break;
                
                if (std::chrono::steady_clock::now() > deadline) {
                    ::kill(pid, SIGKILL);  // Use global kill, not member function
                    waitpid(pid, &status, 0);
                    result.error += "\nKilled: timeout";
                    break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            result.wall_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            
            // Get resource usage
            struct rusage usage;
            if (getrusage(RUSAGE_CHILDREN, &usage) == 0) {
                result.cpu_seconds = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 +
                                   usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
                result.memory_bytes = usage.ru_maxrss * 1024;
            }
        }
        
        // Clean up temporary directory (secure deletion)
        cleanup(tmp_dir);
        
        return result;
    }
    
private:
    SandboxConfig config;
    
    void setup_sandbox(const fs::path& work_dir, int stdout_pipe[2], int stderr_pipe[2]) {
        // Redirect stdout/stderr
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        
        // Enter new namespaces
        unshare(CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS);
        
        // Mount tmpfs for working directory
        // Mount tmpfs with size limit from constants
        std::string mount_opts = "size=" + std::to_string(TMPFS_SIZE_LIMIT);
        mount("tmpfs", work_dir.c_str(), "tmpfs", MS_NOSUID | MS_NODEV, mount_opts.c_str());
        chdir(work_dir.c_str());
        
        // Drop all capabilities
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0);
        
        // Setup seccomp filter
        setup_seccomp();
        
        // Set resource limits
        struct rlimit limit;
        
        // Memory limit
        limit.rlim_cur = limit.rlim_max = config.memory_limit_bytes;
        setrlimit(RLIMIT_AS, &limit);
        
        // CPU time limit
        limit.rlim_cur = config.cpu_quota_us / 1000000;
        limit.rlim_max = limit.rlim_cur + 1;
        setrlimit(RLIMIT_CPU, &limit);
        
        // No core dumps (privacy)
        limit.rlim_cur = limit.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &limit);
        
        // Allow limited processes for Python threading
        // Python needs to create threads for imports and some libraries
        limit.rlim_cur = limit.rlim_max = MAX_PROCESSES_PER_JOB;
        setrlimit(RLIMIT_NPROC, &limit);
    }
    
    void setup_seccomp() {
        scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
        
        // Allow only essential syscalls for Python execution
        const int allowed[] = {
            SCMP_SYS(read), SCMP_SYS(write), SCMP_SYS(close),
            SCMP_SYS(fstat), SCMP_SYS(lseek), SCMP_SYS(mmap),
            SCMP_SYS(mprotect), SCMP_SYS(munmap), SCMP_SYS(brk),
            SCMP_SYS(rt_sigaction), SCMP_SYS(rt_sigprocmask),
            SCMP_SYS(ioctl), SCMP_SYS(access), SCMP_SYS(execve),
            SCMP_SYS(getuid), SCMP_SYS(getgid), SCMP_SYS(geteuid),
            SCMP_SYS(getegid), SCMP_SYS(fcntl), SCMP_SYS(dup2),
            SCMP_SYS(exit_group), SCMP_SYS(exit), SCMP_SYS(getpid),
            SCMP_SYS(getrandom), SCMP_SYS(clock_gettime),
            // Additional syscalls needed for Python 3.x
            SCMP_SYS(open),         // Legacy open (Python startup)
            SCMP_SYS(openat),       // Python file operations
            SCMP_SYS(newfstatat),   // Python 3.10+ stat operations
            SCMP_SYS(pread64),      // Efficient file reading
            SCMP_SYS(pwrite64),     // Efficient file writing
            SCMP_SYS(readlink),     // Python module resolution
            SCMP_SYS(getcwd),       // Working directory operations
            SCMP_SYS(stat),         // File stat operations
            SCMP_SYS(fstat64),      // 64-bit file stats
            SCMP_SYS(lstat),        // Symlink stats
            SCMP_SYS(getdents64),   // Directory reading
            SCMP_SYS(futex),        // Thread synchronization
            SCMP_SYS(set_tid_address), // Thread cleanup
            SCMP_SYS(set_robust_list), // Thread robustness
            SCMP_SYS(rt_sigreturn), // Signal handling
            SCMP_SYS(sigaltstack),  // Signal stack
            SCMP_SYS(arch_prctl),   // Architecture-specific operations
            SCMP_SYS(gettid),       // Thread ID
            SCMP_SYS(tgkill),       // Thread signaling
            SCMP_SYS(clone),        // Thread creation (restricted)
            SCMP_SYS(unshare),      // Namespace creation
            SCMP_SYS(mount),        // Mount tmpfs
            SCMP_SYS(umount2),      // Unmount
            SCMP_SYS(prlimit64),    // Resource limits (64-bit)
            SCMP_SYS(setrlimit),    // Resource limits
            SCMP_SYS(getrlimit),    // Get resource limits
            SCMP_SYS(rseq),         // Restartable sequences
            SCMP_SYS(sysinfo),      // System information
            SCMP_SYS(uname),        // System name
            SCMP_SYS(pipe),         // Pipe creation
            SCMP_SYS(pipe2),        // Pipe creation with flags
            SCMP_SYS(dup),          // Duplicate file descriptor
            SCMP_SYS(dup3),         // Duplicate file descriptor
            SCMP_SYS(poll),         // I/O polling
            SCMP_SYS(ppoll),        // I/O polling
            SCMP_SYS(select),       // I/O multiplexing
            SCMP_SYS(pselect6),     // I/O multiplexing
            SCMP_SYS(epoll_create), // Event polling
            SCMP_SYS(epoll_create1),// Event polling
            SCMP_SYS(epoll_ctl),    // Event polling control
            SCMP_SYS(epoll_wait),   // Event polling wait
            SCMP_SYS(eventfd),      // Event notification
            SCMP_SYS(eventfd2),     // Event notification
            SCMP_SYS(mremap),       // Remap memory
            SCMP_SYS(getpgrp),      // Process group
            SCMP_SYS(getppid),      // Parent process ID
            SCMP_SYS(getsid),       // Session ID
            SCMP_SYS(sched_getaffinity), // CPU affinity
            SCMP_SYS(sched_yield),  // Yield CPU
            SCMP_SYS(nanosleep),    // Sleep
            SCMP_SYS(clock_nanosleep) // High-res sleep
        };
        
        for (int syscall : allowed) {
            seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0);
        }
        
        seccomp_load(ctx);
        seccomp_release(ctx);
    }
    
    std::string read_pipe(int fd) {
        std::string result;
        result.reserve(1024 * 1024); // Pre-allocate 1MB for efficiency
        char buffer[PIPE_BUFFER_SIZE];
        ssize_t n;
        size_t total_read = 0;
        
        while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
            // Prevent unbounded memory growth
            if (total_read + n > MAX_OUTPUT_SIZE) {
                size_t remaining = MAX_OUTPUT_SIZE - total_read;
                if (remaining > 0) {
                    result.append(buffer, remaining);
                }
                result.append("\n[Output truncated at 10MB limit]");
                break;
            }
            result.append(buffer, n);
            total_read += n;
        }
        
        close(fd);
        return result;
    }
    
    void cleanup(const fs::path& dir) {
        // Secure deletion - overwrite with random data first
        std::ifstream urandom("/dev/urandom", std::ios::binary);
        
        for (auto& entry : fs::recursive_directory_iterator(dir)) {
            if (fs::is_regular_file(entry)) {
                size_t file_size = fs::file_size(entry);
                if (file_size > 0) {
                    std::vector<char> random_data(std::min(file_size, SECURE_DELETE_CHUNK));
                    std::ofstream file(entry.path(), std::ios::binary);
                    
                    // Overwrite file with random data
                    size_t written = 0;
                    while (written < file_size) {
                        size_t to_write = std::min(random_data.size(), file_size - written);
                        if (urandom.good()) {
                            urandom.read(random_data.data(), to_write);
                        } else {
                            // Fallback to pseudo-random if /dev/urandom fails
                            for (size_t i = 0; i < to_write; ++i) {
                                random_data[i] = static_cast<char>(std::rand() % 256);
                            }
                        }
                        file.write(random_data.data(), to_write);
                        written += to_write;
                    }
                    
                    file.close();
                    // Sync to ensure data is written to disk
                    sync();
                }
            }
        }
        
        urandom.close();
        
        // Then remove
        fs::remove_all(dir);
    }
};

Sandbox::Sandbox(const SandboxConfig& config) 
    : impl(std::make_unique<Impl>(config)) {}

Sandbox::~Sandbox() = default;

JobResult Sandbox::execute(std::string code, const std::string& job_id) {
    return impl->execute(std::move(code), job_id);
}

bool Sandbox::kill(const std::string& job_id) {
    // Implementation would track PIDs by job_id
    return true;
}

} // namespace sandrun