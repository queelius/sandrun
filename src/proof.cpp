#include "proof.h"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <memory>

namespace sandrun {

// Hash helper function
static std::string sha256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.size());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// ExecutionTrace implementation
void ExecutionTrace::record_syscall(int syscall_num, uint64_t arg1, uint64_t arg2) {
    Syscall sc;
    sc.number = syscall_num;
    sc.timestamp = std::chrono::steady_clock::now().time_since_epoch();
    sc.arg1 = arg1;
    sc.arg2 = arg2;
    syscalls.push_back(sc);
}

void ExecutionTrace::record_file_op(const std::string& op, const std::string& path) {
    file_operations.push_back(op + ":" + path);
}

std::string ExecutionTrace::create_checkpoint() {
    // Create hash of current state
    std::stringstream ss;
    ss << "syscalls:" << syscalls.size();
    ss << ",files:" << file_operations.size();
    
    // Add recent syscalls to hash
    size_t start = syscalls.size() > 100 ? syscalls.size() - 100 : 0;
    for (size_t i = start; i < syscalls.size(); ++i) {
        ss << "," << syscalls[i].number;
    }
    
    std::string checkpoint_hash = sha256(ss.str());
    checkpoints.push_back(checkpoint_hash);
    return checkpoint_hash;
}

void ExecutionTrace::clear() {
    syscalls.clear();
    file_operations.clear();
    checkpoints.clear();
}

// ProofOfCompute implementation
std::string ProofOfCompute::calculate_hash() const {
    std::stringstream ss;
    ss << job_id;
    ss << code_hash;
    ss << input_hash;
    ss << output_hash;
    ss << execution_hash;
    
    for (const auto& checkpoint : checkpoint_hashes) {
        ss << checkpoint;
    }
    
    ss << cpu_time;
    ss << gpu_time;
    ss << memory_peak;
    ss << syscall_count;
    
    return sha256(ss.str());
}

std::string ProofOfCompute::to_json() const {
    // Simple JSON serialization (would use jsoncpp in production)
    std::stringstream json;
    json << "{\n";
    json << "  \"job_id\": \"" << job_id << "\",\n";
    json << "  \"code_hash\": \"" << code_hash << "\",\n";
    json << "  \"input_hash\": \"" << input_hash << "\",\n";
    json << "  \"output_hash\": \"" << output_hash << "\",\n";
    json << "  \"execution_hash\": \"" << execution_hash << "\",\n";
    json << "  \"checkpoint_hashes\": [";
    
    for (size_t i = 0; i < checkpoint_hashes.size(); ++i) {
        if (i > 0) json << ", ";
        json << "\"" << checkpoint_hashes[i] << "\"";
    }
    json << "],\n";
    
    json << "  \"cpu_time\": " << cpu_time << ",\n";
    json << "  \"gpu_time\": " << gpu_time << ",\n";
    json << "  \"memory_peak\": " << memory_peak << ",\n";
    json << "  \"syscall_count\": " << syscall_count << ",\n";
    
    // Add timestamp
    auto time_t_timestamp = std::chrono::system_clock::to_time_t(timestamp);
    json << "  \"timestamp\": \"" << std::put_time(std::gmtime(&time_t_timestamp), "%Y-%m-%dT%H:%M:%SZ") << "\",\n";
    
    json << "  \"proof_hash\": \"" << calculate_hash() << "\"\n";
    json << "}";
    
    return json.str();
}

bool ProofOfCompute::verify(const ExecutionTrace& trace) const {
    // Verify execution trace matches proof
    // In production, would do more thorough verification
    
    if (trace.syscalls.size() != syscall_count) {
        return false;
    }
    
    if (trace.checkpoints != checkpoint_hashes) {
        return false;
    }
    
    // Calculate trace hash and compare
    std::stringstream trace_data;
    for (const auto& sc : trace.syscalls) {
        trace_data << sc.number << ",";
    }
    std::string trace_hash = sha256(trace_data.str());
    
    return trace_hash == execution_hash;
}

// ProofGenerator implementation
class ProofGenerator::Impl {
public:
    ExecutionTrace current_trace;
    std::string current_job_id;
    std::string current_code_hash;
    std::chrono::steady_clock::time_point start_time;
    bool recording = false;
    
    void start_recording(const std::string& job_id, const std::string& code) {
        current_job_id = job_id;
        current_code_hash = sha256(code);
        current_trace.clear();
        start_time = std::chrono::steady_clock::now();
        recording = true;
    }
    
    void record_syscall(int syscall_num, uint64_t arg1, uint64_t arg2) {
        if (recording) {
            current_trace.record_syscall(syscall_num, arg1, arg2);
        }
    }
    
    void checkpoint() {
        if (recording) {
            current_trace.create_checkpoint();
        }
    }
    
    ProofOfCompute generate_proof(const std::string& output, 
                                  double cpu_time,
                                  size_t memory_peak) {
        ProofOfCompute proof;
        proof.job_id = current_job_id;
        proof.code_hash = current_code_hash;
        proof.input_hash = ""; // Would hash input files
        proof.output_hash = sha256(output);
        
        // Generate execution hash from trace
        std::stringstream trace_data;
        for (const auto& sc : current_trace.syscalls) {
            trace_data << sc.number << "," << sc.arg1 << "," << sc.arg2 << ";";
        }
        proof.execution_hash = sha256(trace_data.str());
        
        proof.checkpoint_hashes = current_trace.checkpoints;
        proof.cpu_time = cpu_time;
        proof.gpu_time = 0; // Would get from GPU monitoring
        proof.memory_peak = memory_peak;
        proof.syscall_count = current_trace.syscalls.size();
        proof.timestamp = std::chrono::system_clock::now();
        
        recording = false;
        return proof;
    }
};

ProofGenerator::ProofGenerator() : impl(std::make_unique<Impl>()) {}
ProofGenerator::~ProofGenerator() = default;

void ProofGenerator::start_recording(const std::string& job_id, const std::string& code) {
    impl->start_recording(job_id, code);
}

void ProofGenerator::record_syscall(int syscall_num, uint64_t arg1, uint64_t arg2) {
    impl->record_syscall(syscall_num, arg1, arg2);
}

void ProofGenerator::checkpoint() {
    impl->checkpoint();
}

ProofOfCompute ProofGenerator::generate_proof(const std::string& output, 
                                              double cpu_time,
                                              size_t memory_peak) {
    return impl->generate_proof(output, cpu_time, memory_peak);
}

} // namespace sandrun