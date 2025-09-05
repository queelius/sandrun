#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>

namespace sandrun {

struct ExecutionStep {
    std::chrono::microseconds timestamp;
    std::string operation;          // syscall name, instruction type, etc.
    std::vector<std::string> args;  // operation arguments
    std::string result;             // operation result
    std::string hash;              // SHA256 of step data
};

struct ProofOfCompute {
    std::string job_id;
    std::string node_id;
    std::string code_hash;         // SHA256 of input code
    std::vector<ExecutionStep> trace;
    std::string final_hash;        // SHA256 of complete trace
    std::chrono::system_clock::time_point timestamp;
    std::string signature;         // Digital signature of proof
    size_t trace_length;
    std::string metadata;          // JSON metadata
};

struct ConsensusResult {
    bool is_valid = false;
    double confidence_score = 0.0;  // 0.0 to 1.0
    std::vector<std::string> agreeing_nodes;
    std::vector<std::string> disagreeing_nodes;
    std::string canonical_hash;
    std::string error_message;
};

class ProofGenerator {
public:
    ProofGenerator();
    ~ProofGenerator();

    // Trace collection methods
    void startTrace(const std::string& job_id);
    void recordStep(const std::string& operation, const std::vector<std::string>& args, const std::string& result);
    void recordSyscall(const std::string& syscall, const std::vector<std::string>& args, int result);
    void recordGPUOperation(const std::string& kernel, const std::vector<std::string>& params);
    ExecutionStep finalizeStep();
    
    // Proof generation
    ProofOfCompute generateProof(const std::string& node_id, const std::string& code);
    std::string computeTraceHash(const std::vector<ExecutionStep>& trace);
    std::string signProof(const ProofOfCompute& proof, const std::string& private_key);
    
    // Verification methods
    bool verifyProof(const ProofOfCompute& proof, const std::string& public_key);
    bool verifyTraceIntegrity(const std::vector<ExecutionStep>& trace);
    bool verifyTimestampSequence(const std::vector<ExecutionStep>& trace);
    
    // Determinism enforcement
    void enableDeterministicMode();
    void seedRandomNumberGenerator(uint64_t seed);
    void normalizeFloatingPointPrecision();
    void enforceStrictMemoryOrdering();
    
    // Configuration
    void setTraceVerbosity(int level);  // 0=minimal, 1=standard, 2=detailed
    void enableGPUTracing(bool enable = true);
    void setMaxTraceLength(size_t max_steps);
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

class ConsensusEngine {
public:
    ConsensusEngine();
    ~ConsensusEngine();
    
    // Consensus protocol methods
    ConsensusResult validateProofs(const std::vector<ProofOfCompute>& proofs);
    ConsensusResult compareTraces(const std::vector<std::vector<ExecutionStep>>& traces);
    double calculateSimilarity(const std::vector<ExecutionStep>& trace1, const std::vector<ExecutionStep>& trace2);
    
    // Byzantine fault tolerance
    std::string findCanonicalTrace(const std::vector<ProofOfCompute>& proofs, double threshold = 0.67);
    std::vector<std::string> detectMaliciousNodes(const std::vector<ProofOfCompute>& proofs);
    
    // Verification strategies
    bool verifyWithMajorityConsensus(const std::vector<ProofOfCompute>& proofs);
    bool verifyWithStakeWeighting(const std::vector<ProofOfCompute>& proofs, const std::vector<double>& stakes);
    bool verifyWithReputationScoring(const std::vector<ProofOfCompute>& proofs, const std::vector<double>& reputation);
    
    // Configuration
    void setConsensusThreshold(double threshold);  // 0.5 to 1.0
    void setMaxValidationTime(std::chrono::seconds timeout);
    void enableByzantineFaultTolerance(bool enable = true);
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Utility functions for proof operations
namespace proof_utils {
    std::string hashExecutionStep(const ExecutionStep& step);
    std::string combineHashes(const std::vector<std::string>& hashes);
    bool isValidSHA256(const std::string& hash);
    std::vector<uint8_t> serializeProof(const ProofOfCompute& proof);
    ProofOfCompute deserializeProof(const std::vector<uint8_t>& data);
}

} // namespace sandrun