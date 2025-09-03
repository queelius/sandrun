#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <memory>

namespace sandrun {

// Execution trace for proof-of-compute
struct ExecutionTrace {
    struct Syscall {
        int number;
        std::chrono::nanoseconds timestamp;
        uint64_t arg1;
        uint64_t arg2;
    };
    
    std::vector<Syscall> syscalls;
    std::vector<std::string> file_operations;
    std::vector<std::string> checkpoints;  // Hash at each checkpoint
    
    // Add a syscall to the trace
    void record_syscall(int syscall_num, uint64_t arg1 = 0, uint64_t arg2 = 0);
    
    // Add file operation
    void record_file_op(const std::string& op, const std::string& path);
    
    // Create checkpoint hash
    std::string create_checkpoint();
    
    // Clear sensitive data
    void clear();
};

// Proof of compute for a job
struct ProofOfCompute {
    std::string job_id;
    std::string code_hash;           // Hash of input code
    std::string input_hash;          // Hash of input data
    std::string output_hash;         // Hash of output
    std::string execution_hash;      // Hash of execution trace
    std::vector<std::string> checkpoint_hashes;
    
    double cpu_time;                 // CPU seconds used
    double gpu_time;                 // GPU seconds (if applicable)
    size_t memory_peak;              // Peak memory usage
    size_t syscall_count;            // Total syscalls made
    
    std::chrono::system_clock::time_point timestamp;
    
    // Generate deterministic proof hash
    std::string calculate_hash() const;
    
    // Serialize to JSON
    std::string to_json() const;
    
    // Verify proof matches execution
    bool verify(const ExecutionTrace& trace) const;
};

// Proof generator integrated with sandbox
class ProofGenerator {
public:
    ProofGenerator();
    ~ProofGenerator();
    
    // Start recording execution
    void start_recording(const std::string& job_id, const std::string& code);
    
    // Record syscall (called from seccomp handler)
    void record_syscall(int syscall_num, uint64_t arg1 = 0, uint64_t arg2 = 0);
    
    // Create checkpoint (for long-running jobs)
    void checkpoint();
    
    // Finish and generate proof
    ProofOfCompute generate_proof(const std::string& output, 
                                  double cpu_time,
                                  size_t memory_peak);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace sandrun