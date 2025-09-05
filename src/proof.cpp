#include "proof.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <json/json.h>

namespace sandrun {

class ProofGenerator::Impl {
public:
    std::string current_job_id_;
    std::vector<ExecutionStep> current_trace_;
    std::chrono::system_clock::time_point start_time_;
    int trace_verbosity_ = 1;
    bool gpu_tracing_enabled_ = false;
    size_t max_trace_length_ = 10000;
    bool deterministic_mode_ = false;
    uint64_t rng_seed_ = 0;
    
    std::string computeHash(const std::string& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash);
        
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
    
    std::string serializeExecutionStep(const ExecutionStep& step) {
        Json::Value json;
        json["timestamp"] = static_cast<Json::Int64>(step.timestamp.count());
        json["operation"] = step.operation;
        
        Json::Value args_array(Json::arrayValue);
        for (const auto& arg : step.args) {
            args_array.append(arg);
        }
        json["args"] = args_array;
        json["result"] = step.result;
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, json);
    }
    
    std::string normalizeFloatingPoint(const std::string& value) {
        if (!deterministic_mode_) {
            return value;
        }
        
        try {
            double d = std::stod(value);
            std::stringstream ss;
            ss << std::fixed << std::setprecision(6) << d;
            return ss.str();
        } catch (...) {
            return value;  // Not a floating point number
        }
    }
    
    bool shouldIncludeStep(const std::string& operation) {
        if (trace_verbosity_ == 0) {
            // Minimal: only major operations
            return operation == "execute_start" || operation == "execute_end" || 
                   operation.find("error") != std::string::npos;
        } else if (trace_verbosity_ == 1) {
            // Standard: exclude very low-level operations
            return operation != "memory_access" && operation != "register_update";
        } else {
            // Detailed: include everything
            return true;
        }
    }
};

class ConsensusEngine::Impl {
public:
    double consensus_threshold_ = 0.67;
    std::chrono::seconds max_validation_time_{300};
    bool byzantine_fault_tolerance_ = true;
    
    double calculateStepSimilarity(const ExecutionStep& step1, const ExecutionStep& step2) {
        double similarity = 0.0;
        int factors = 0;
        
        // Compare operation names
        if (step1.operation == step2.operation) {
            similarity += 0.4;
        }
        factors++;
        
        // Compare argument counts
        if (step1.args.size() == step2.args.size()) {
            similarity += 0.2;
            
            // Compare individual arguments
            double arg_similarity = 0.0;
            for (size_t i = 0; i < step1.args.size(); ++i) {
                if (step1.args[i] == step2.args[i]) {
                    arg_similarity += 1.0;
                }
            }
            if (!step1.args.empty()) {
                arg_similarity /= step1.args.size();
                similarity += arg_similarity * 0.2;
            }
        }
        factors++;
        
        // Compare results
        if (step1.result == step2.result) {
            similarity += 0.2;
        }
        factors++;
        
        return similarity;
    }
    
    std::vector<std::pair<std::string, int>> groupSimilarTraces(
        const std::vector<ProofOfCompute>& proofs, double similarity_threshold) {
        
        std::vector<std::pair<std::string, int>> groups;
        std::vector<bool> assigned(proofs.size(), false);
        
        for (size_t i = 0; i < proofs.size(); ++i) {
            if (assigned[i]) continue;
            
            std::string canonical_hash = proofs[i].final_hash;
            int group_size = 1;
            assigned[i] = true;
            
            for (size_t j = i + 1; j < proofs.size(); ++j) {
                if (assigned[j]) continue;
                
                double similarity = calculateTraceSimilarity(proofs[i].trace, proofs[j].trace);
                if (similarity >= similarity_threshold) {
                    group_size++;
                    assigned[j] = true;
                }
            }
            
            groups.emplace_back(canonical_hash, group_size);
        }
        
        return groups;
    }
    
    double calculateTraceSimilarity(const std::vector<ExecutionStep>& trace1,
                                  const std::vector<ExecutionStep>& trace2) {
        if (trace1.empty() && trace2.empty()) return 1.0;
        if (trace1.empty() || trace2.empty()) return 0.0;
        
        // Use dynamic programming for sequence alignment
        size_t m = trace1.size();
        size_t n = trace2.size();
        std::vector<std::vector<double>> dp(m + 1, std::vector<double>(n + 1, 0.0));
        
        // Fill the DP table
        for (size_t i = 1; i <= m; ++i) {
            for (size_t j = 1; j <= n; ++j) {
                double step_sim = calculateStepSimilarity(trace1[i-1], trace2[j-1]);
                
                dp[i][j] = std::max({
                    dp[i-1][j-1] + step_sim,  // Match
                    dp[i-1][j],               // Skip from trace1
                    dp[i][j-1]                // Skip from trace2
                });
            }
        }
        
        // Normalize by the maximum possible score
        double max_length = std::max(m, n);
        return dp[m][n] / max_length;
    }
    
    bool detectMaliciousBehavior(const std::vector<ProofOfCompute>& proofs) {
        if (!byzantine_fault_tolerance_ || proofs.size() < 3) {
            return false;
        }
        
        // Count how many proofs are similar to each other
        std::map<std::string, int> hash_counts;
        for (const auto& proof : proofs) {
            hash_counts[proof.final_hash]++;
        }
        
        // Find the most common hash
        auto max_it = std::max_element(hash_counts.begin(), hash_counts.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        
        if (max_it != hash_counts.end()) {
            double majority_ratio = static_cast<double>(max_it->second) / proofs.size();
            
            // If less than 67% agree, might indicate Byzantine behavior
            return majority_ratio < 0.67;
        }
        
        return false;
    }
};

// ProofGenerator implementation
ProofGenerator::ProofGenerator() : pImpl(std::make_unique<Impl>()) {}

ProofGenerator::~ProofGenerator() = default;

void ProofGenerator::startTrace(const std::string& job_id) {
    pImpl->current_job_id_ = job_id;
    pImpl->current_trace_.clear();
    pImpl->start_time_ = std::chrono::system_clock::now();
    
    // Record start step
    recordStep("trace_start", {job_id}, "initialized");
}

void ProofGenerator::recordStep(const std::string& operation, 
                               const std::vector<std::string>& args, 
                               const std::string& result) {
    if (!pImpl->shouldIncludeStep(operation)) {
        return;
    }
    
    if (pImpl->current_trace_.size() >= pImpl->max_trace_length_) {
        // Trace too long, skip detailed steps
        if (pImpl->trace_verbosity_ > 0) {
            pImpl->trace_verbosity_ = 0;  // Switch to minimal mode
        } else {
            return;  // Skip this step entirely
        }
    }
    
    ExecutionStep step;
    step.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    step.operation = operation;
    step.args = args;
    step.result = pImpl->normalizeFloatingPoint(result);
    
    // Compute hash for this step
    std::string step_data = pImpl->serializeExecutionStep(step);
    step.hash = pImpl->computeHash(step_data);
    
    pImpl->current_trace_.push_back(step);
}

void ProofGenerator::recordSyscall(const std::string& syscall, 
                                  const std::vector<std::string>& args, 
                                  int result) {
    if (pImpl->trace_verbosity_ < 2) {
        return;  // Only record syscalls in detailed mode
    }
    
    recordStep("syscall:" + syscall, args, std::to_string(result));
}

void ProofGenerator::recordGPUOperation(const std::string& kernel, 
                                       const std::vector<std::string>& params) {
    if (!pImpl->gpu_tracing_enabled_) {
        return;
    }
    
    recordStep("gpu_kernel:" + kernel, params, "completed");
}

ExecutionStep ProofGenerator::finalizeStep() {
    if (pImpl->current_trace_.empty()) {
        throw std::runtime_error("No trace recorded");
    }
    
    ExecutionStep final_step;
    final_step.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    final_step.operation = "trace_end";
    final_step.args = {pImpl->current_job_id_};
    final_step.result = "finalized";
    
    return final_step;
}

ProofOfCompute ProofGenerator::generateProof(const std::string& node_id, const std::string& code) {
    ProofOfCompute proof;
    proof.job_id = pImpl->current_job_id_;
    proof.node_id = node_id;
    proof.code_hash = pImpl->computeHash(code);
    proof.trace = pImpl->current_trace_;
    proof.timestamp = std::chrono::system_clock::now();
    proof.trace_length = pImpl->current_trace_.size();
    
    // Compute final hash
    proof.final_hash = computeTraceHash(proof.trace);
    
    // Create metadata
    Json::Value metadata;
    metadata["node_id"] = node_id;
    metadata["trace_length"] = static_cast<Json::UInt64>(proof.trace_length);
    metadata["verbosity"] = pImpl->trace_verbosity_;
    metadata["gpu_enabled"] = pImpl->gpu_tracing_enabled_;
    metadata["deterministic"] = pImpl->deterministic_mode_;
    
    Json::StreamWriterBuilder builder;
    proof.metadata = Json::writeString(builder, metadata);
    
    return proof;
}

std::string ProofGenerator::computeTraceHash(const std::vector<ExecutionStep>& trace) {
    if (trace.empty()) {
        return pImpl->computeHash("");
    }
    
    std::stringstream trace_data;
    for (const auto& step : trace) {
        trace_data << step.hash;
    }
    
    return pImpl->computeHash(trace_data.str());
}

std::string ProofGenerator::signProof(const ProofOfCompute& proof, const std::string& private_key) {
    // Create signature data
    std::stringstream sig_data;
    sig_data << proof.job_id << proof.node_id << proof.code_hash << proof.final_hash;
    
    std::string data = sig_data.str();
    return pImpl->computeHash(data + private_key);  // Simplified signing
}

bool ProofGenerator::verifyProof(const ProofOfCompute& proof, const std::string& public_key) {
    // Verify trace integrity first
    if (!verifyTraceIntegrity(proof.trace)) {
        return false;
    }
    
    // Verify final hash
    std::string computed_hash = computeTraceHash(proof.trace);
    if (computed_hash != proof.final_hash) {
        return false;
    }
    
    // Verify timestamp sequence
    if (!verifyTimestampSequence(proof.trace)) {
        return false;
    }
    
    // Simplified signature verification
    std::stringstream sig_data;
    sig_data << proof.job_id << proof.node_id << proof.code_hash << proof.final_hash;
    std::string expected_signature = pImpl->computeHash(sig_data.str() + public_key);
    
    return expected_signature == proof.signature;
}

bool ProofGenerator::verifyTraceIntegrity(const std::vector<ExecutionStep>& trace) {
    for (const auto& step : trace) {
        std::string step_data = pImpl->serializeExecutionStep(step);
        std::string computed_hash = pImpl->computeHash(step_data);
        
        if (computed_hash != step.hash) {
            return false;
        }
    }
    return true;
}

bool ProofGenerator::verifyTimestampSequence(const std::vector<ExecutionStep>& trace) {
    for (size_t i = 1; i < trace.size(); ++i) {
        if (trace[i].timestamp < trace[i-1].timestamp) {
            return false;  // Timestamps should be non-decreasing
        }
    }
    return true;
}

void ProofGenerator::enableDeterministicMode() {
    pImpl->deterministic_mode_ = true;
}

void ProofGenerator::seedRandomNumberGenerator(uint64_t seed) {
    pImpl->rng_seed_ = seed;
}

void ProofGenerator::normalizeFloatingPointPrecision() {
    // This would set floating-point control registers in a real implementation
    pImpl->deterministic_mode_ = true;
}

void ProofGenerator::enforceStrictMemoryOrdering() {
    // This would set memory ordering constraints in a real implementation
    pImpl->deterministic_mode_ = true;
}

void ProofGenerator::setTraceVerbosity(int level) {
    pImpl->trace_verbosity_ = std::max(0, std::min(2, level));
}

void ProofGenerator::enableGPUTracing(bool enable) {
    pImpl->gpu_tracing_enabled_ = enable;
}

void ProofGenerator::setMaxTraceLength(size_t max_steps) {
    pImpl->max_trace_length_ = max_steps;
}

// ConsensusEngine implementation
ConsensusEngine::ConsensusEngine() : pImpl(std::make_unique<Impl>()) {}

ConsensusEngine::~ConsensusEngine() = default;

ConsensusResult ConsensusEngine::validateProofs(const std::vector<ProofOfCompute>& proofs) {
    ConsensusResult result;
    
    if (proofs.empty()) {
        result.error_message = "No proofs to validate";
        return result;
    }
    
    if (proofs.size() == 1) {
        result.is_valid = true;
        result.confidence_score = 0.5;  // Low confidence with single proof
        result.canonical_hash = proofs[0].final_hash;
        result.agreeing_nodes.push_back(proofs[0].node_id);
        return result;
    }
    
    // Group similar proofs
    auto groups = pImpl->groupSimilarTraces(proofs, 0.9);
    
    // Find the largest group
    auto max_group = *std::max_element(groups.begin(), groups.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    double consensus_ratio = static_cast<double>(max_group.second) / proofs.size();
    
    result.is_valid = consensus_ratio >= pImpl->consensus_threshold_;
    result.confidence_score = consensus_ratio;
    result.canonical_hash = max_group.first;
    
    // Classify nodes as agreeing or disagreeing
    for (const auto& proof : proofs) {
        double similarity = 0.0;
        for (const auto& other_proof : proofs) {
            if (other_proof.final_hash == max_group.first) {
                similarity = compareTraces({proof.trace, other_proof.trace}).confidence_score;
                break;
            }
        }
        
        if (similarity >= 0.9) {
            result.agreeing_nodes.push_back(proof.node_id);
        } else {
            result.disagreeing_nodes.push_back(proof.node_id);
        }
    }
    
    // Check for potential malicious behavior
    if (pImpl->detectMaliciousBehavior(proofs)) {
        result.error_message = "Potential Byzantine behavior detected";
        result.confidence_score *= 0.5;  // Reduce confidence
    }
    
    return result;
}

ConsensusResult ConsensusEngine::compareTraces(const std::vector<std::vector<ExecutionStep>>& traces) {
    ConsensusResult result;
    
    if (traces.size() < 2) {
        result.error_message = "Need at least 2 traces to compare";
        return result;
    }
    
    // Compare all pairs and find average similarity
    double total_similarity = 0.0;
    int comparisons = 0;
    
    for (size_t i = 0; i < traces.size(); ++i) {
        for (size_t j = i + 1; j < traces.size(); ++j) {
            double similarity = pImpl->calculateTraceSimilarity(traces[i], traces[j]);
            total_similarity += similarity;
            comparisons++;
        }
    }
    
    double average_similarity = total_similarity / comparisons;
    
    result.is_valid = average_similarity >= pImpl->consensus_threshold_;
    result.confidence_score = average_similarity;
    
    return result;
}

double ConsensusEngine::calculateSimilarity(const std::vector<ExecutionStep>& trace1,
                                          const std::vector<ExecutionStep>& trace2) {
    return pImpl->calculateTraceSimilarity(trace1, trace2);
}

std::string ConsensusEngine::findCanonicalTrace(const std::vector<ProofOfCompute>& proofs, 
                                               double threshold) {
    auto groups = pImpl->groupSimilarTraces(proofs, threshold);
    
    if (groups.empty()) {
        return "";
    }
    
    // Return hash of largest group
    auto max_group = *std::max_element(groups.begin(), groups.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    return max_group.first;
}

std::vector<std::string> ConsensusEngine::detectMaliciousNodes(const std::vector<ProofOfCompute>& proofs) {
    std::vector<std::string> malicious_nodes;
    
    if (!pImpl->byzantine_fault_tolerance_ || proofs.size() < 4) {
        return malicious_nodes;
    }
    
    std::string canonical_hash = findCanonicalTrace(proofs, 0.9);
    if (canonical_hash.empty()) {
        return malicious_nodes;
    }
    
    // Nodes with significantly different traces are potentially malicious
    for (const auto& proof : proofs) {
        if (proof.final_hash != canonical_hash) {
            // Check if this is an outlier
            bool is_outlier = true;
            for (const auto& other_proof : proofs) {
                if (other_proof.node_id != proof.node_id && 
                    proof.final_hash == other_proof.final_hash) {
                    is_outlier = false;
                    break;
                }
            }
            
            if (is_outlier) {
                malicious_nodes.push_back(proof.node_id);
            }
        }
    }
    
    return malicious_nodes;
}

bool ConsensusEngine::verifyWithMajorityConsensus(const std::vector<ProofOfCompute>& proofs) {
    ConsensusResult result = validateProofs(proofs);
    return result.is_valid && result.confidence_score >= 0.5;
}

bool ConsensusEngine::verifyWithStakeWeighting(const std::vector<ProofOfCompute>& proofs,
                                              const std::vector<double>& stakes) {
    if (proofs.size() != stakes.size()) {
        return false;
    }
    
    std::map<std::string, double> hash_stakes;
    double total_stake = 0.0;
    
    for (size_t i = 0; i < proofs.size(); ++i) {
        hash_stakes[proofs[i].final_hash] += stakes[i];
        total_stake += stakes[i];
    }
    
    // Find hash with highest stake
    auto max_it = std::max_element(hash_stakes.begin(), hash_stakes.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (max_it != hash_stakes.end()) {
        double stake_ratio = max_it->second / total_stake;
        return stake_ratio >= pImpl->consensus_threshold_;
    }
    
    return false;
}

bool ConsensusEngine::verifyWithReputationScoring(const std::vector<ProofOfCompute>& proofs,
                                                  const std::vector<double>& reputation) {
    if (proofs.size() != reputation.size()) {
        return false;
    }
    
    std::map<std::string, double> hash_reputation;
    double total_reputation = 0.0;
    
    for (size_t i = 0; i < proofs.size(); ++i) {
        hash_reputation[proofs[i].final_hash] += reputation[i];
        total_reputation += reputation[i];
    }
    
    if (total_reputation == 0.0) {
        return false;
    }
    
    // Find hash with highest reputation score
    auto max_it = std::max_element(hash_reputation.begin(), hash_reputation.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (max_it != hash_reputation.end()) {
        double reputation_ratio = max_it->second / total_reputation;
        return reputation_ratio >= pImpl->consensus_threshold_;
    }
    
    return false;
}

void ConsensusEngine::setConsensusThreshold(double threshold) {
    pImpl->consensus_threshold_ = std::max(0.5, std::min(1.0, threshold));
}

void ConsensusEngine::setMaxValidationTime(std::chrono::seconds timeout) {
    pImpl->max_validation_time_ = timeout;
}

void ConsensusEngine::enableByzantineFaultTolerance(bool enable) {
    pImpl->byzantine_fault_tolerance_ = enable;
}

// Utility functions
namespace proof_utils {
    std::string hashExecutionStep(const ExecutionStep& step) {
        ProofGenerator::Impl impl;
        std::string step_data = impl.serializeExecutionStep(step);
        return impl.computeHash(step_data);
    }
    
    std::string combineHashes(const std::vector<std::string>& hashes) {
        ProofGenerator::Impl impl;
        std::string combined;
        for (const auto& hash : hashes) {
            combined += hash;
        }
        return impl.computeHash(combined);
    }
    
    bool isValidSHA256(const std::string& hash) {
        if (hash.length() != 64) {
            return false;
        }
        
        return std::all_of(hash.begin(), hash.end(), [](char c) {
            return std::isxdigit(c);
        });
    }
    
    std::vector<uint8_t> serializeProof(const ProofOfCompute& proof) {
        Json::Value json;
        json["job_id"] = proof.job_id;
        json["node_id"] = proof.node_id;
        json["code_hash"] = proof.code_hash;
        json["final_hash"] = proof.final_hash;
        json["trace_length"] = static_cast<Json::UInt64>(proof.trace_length);
        json["signature"] = proof.signature;
        json["metadata"] = proof.metadata;
        
        // Serialize trace
        Json::Value trace_array(Json::arrayValue);
        for (const auto& step : proof.trace) {
            Json::Value step_json;
            step_json["timestamp"] = static_cast<Json::Int64>(step.timestamp.count());
            step_json["operation"] = step.operation;
            step_json["result"] = step.result;
            step_json["hash"] = step.hash;
            
            Json::Value args_array(Json::arrayValue);
            for (const auto& arg : step.args) {
                args_array.append(arg);
            }
            step_json["args"] = args_array;
            
            trace_array.append(step_json);
        }
        json["trace"] = trace_array;
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string json_str = Json::writeString(builder, json);
        
        return std::vector<uint8_t>(json_str.begin(), json_str.end());
    }
    
    ProofOfCompute deserializeProof(const std::vector<uint8_t>& data) {
        std::string json_str(data.begin(), data.end());
        
        Json::Value json;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(json_str);
        
        if (!Json::parseFromStream(builder, stream, &json, &errors)) {
            throw std::runtime_error("Failed to parse proof JSON: " + errors);
        }
        
        ProofOfCompute proof;
        proof.job_id = json["job_id"].asString();
        proof.node_id = json["node_id"].asString();
        proof.code_hash = json["code_hash"].asString();
        proof.final_hash = json["final_hash"].asString();
        proof.trace_length = json["trace_length"].asUInt64();
        proof.signature = json["signature"].asString();
        proof.metadata = json["metadata"].asString();
        
        // Deserialize trace
        const Json::Value& trace_array = json["trace"];
        for (const auto& step_json : trace_array) {
            ExecutionStep step;
            step.timestamp = std::chrono::microseconds(step_json["timestamp"].asInt64());
            step.operation = step_json["operation"].asString();
            step.result = step_json["result"].asString();
            step.hash = step_json["hash"].asString();
            
            const Json::Value& args_array = step_json["args"];
            for (const auto& arg : args_array) {
                step.args.push_back(arg.asString());
            }
            
            proof.trace.push_back(step);
        }
        
        return proof;
    }
}

} // namespace sandrun