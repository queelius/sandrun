#include <gtest/gtest.h>
#include "proof.h"
#include <thread>

namespace sandrun {
namespace {

class ProofTest : public ::testing::Test {
protected:
    void SetUp() override {
        generator = std::make_unique<ProofGenerator>();
    }

    std::unique_ptr<ProofGenerator> generator;
};

TEST(ExecutionTraceTest, RecordSyscalls) {
    ExecutionTrace trace;
    
    // Record some syscalls
    trace.record_syscall(1, 100, 200);  // SYS_write
    trace.record_syscall(0, 0, 1024);   // SYS_read
    trace.record_syscall(2, 3, 0);      // SYS_open
    
    EXPECT_EQ(trace.syscalls.size(), 3);
    EXPECT_EQ(trace.syscalls[0].number, 1);
    EXPECT_EQ(trace.syscalls[0].arg1, 100);
    EXPECT_EQ(trace.syscalls[0].arg2, 200);
}

TEST(ExecutionTraceTest, RecordFileOperations) {
    ExecutionTrace trace;
    
    trace.record_file_op("open", "/tmp/test.txt");
    trace.record_file_op("write", "/tmp/test.txt");
    trace.record_file_op("close", "/tmp/test.txt");
    
    EXPECT_EQ(trace.file_operations.size(), 3);
    EXPECT_EQ(trace.file_operations[0], "open:/tmp/test.txt");
}

TEST(ExecutionTraceTest, CreateCheckpoints) {
    ExecutionTrace trace;
    
    // Add some activity
    trace.record_syscall(1, 0, 0);
    trace.record_file_op("open", "file.txt");
    
    std::string checkpoint1 = trace.create_checkpoint();
    EXPECT_FALSE(checkpoint1.empty());
    EXPECT_EQ(checkpoint1.length(), 64); // SHA256 hex string
    
    // Add more activity
    trace.record_syscall(2, 0, 0);
    std::string checkpoint2 = trace.create_checkpoint();
    
    EXPECT_NE(checkpoint1, checkpoint2); // Different checkpoints
    EXPECT_EQ(trace.checkpoints.size(), 2);
}

TEST(ExecutionTraceTest, ClearTrace) {
    ExecutionTrace trace;
    
    trace.record_syscall(1, 0, 0);
    trace.record_file_op("open", "file.txt");
    trace.create_checkpoint();
    
    EXPECT_FALSE(trace.syscalls.empty());
    EXPECT_FALSE(trace.file_operations.empty());
    EXPECT_FALSE(trace.checkpoints.empty());
    
    trace.clear();
    
    EXPECT_TRUE(trace.syscalls.empty());
    EXPECT_TRUE(trace.file_operations.empty());
    EXPECT_TRUE(trace.checkpoints.empty());
}

TEST(ProofOfComputeTest, CalculateHash) {
    ProofOfCompute proof;
    proof.job_id = "test_job";
    proof.code_hash = "abc123";
    proof.input_hash = "def456";
    proof.output_hash = "ghi789";
    proof.execution_hash = "jkl012";
    proof.cpu_time = 1.5;
    proof.gpu_time = 0.0;
    proof.memory_peak = 1024 * 1024;
    proof.syscall_count = 100;
    
    std::string hash1 = proof.calculate_hash();
    EXPECT_FALSE(hash1.empty());
    EXPECT_EQ(hash1.length(), 64); // SHA256 hex
    
    // Same proof should generate same hash
    std::string hash2 = proof.calculate_hash();
    EXPECT_EQ(hash1, hash2);
    
    // Change something and hash should differ
    proof.cpu_time = 2.0;
    std::string hash3 = proof.calculate_hash();
    EXPECT_NE(hash1, hash3);
}

TEST(ProofOfComputeTest, ProofWithCheckpoints) {
    ProofOfCompute proof;
    proof.job_id = "long_job";
    proof.checkpoint_hashes.push_back("checkpoint1_hash");
    proof.checkpoint_hashes.push_back("checkpoint2_hash");
    proof.checkpoint_hashes.push_back("checkpoint3_hash");
    
    std::string hash1 = proof.calculate_hash();
    
    // Remove a checkpoint
    proof.checkpoint_hashes.pop_back();
    std::string hash2 = proof.calculate_hash();
    
    EXPECT_NE(hash1, hash2); // Different checkpoints = different hash
}

TEST(ProofOfComputeTest, JSONSerialization) {
    ProofOfCompute proof;
    proof.job_id = "test_job";
    proof.code_hash = "code123";
    proof.output_hash = "output456";
    proof.execution_hash = "exec789";
    proof.cpu_time = 3.14;
    proof.gpu_time = 1.23;
    proof.memory_peak = 2048576;
    proof.syscall_count = 250;
    proof.timestamp = std::chrono::system_clock::now();
    
    std::string json = proof.to_json();
    
    EXPECT_TRUE(json.find("\"job_id\": \"test_job\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"cpu_time\": 3.14") != std::string::npos);
    EXPECT_TRUE(json.find("\"gpu_time\": 1.23") != std::string::npos);
    EXPECT_TRUE(json.find("\"syscall_count\": 250") != std::string::npos);
    EXPECT_TRUE(json.find("\"proof_hash\":") != std::string::npos);
}

TEST(ProofOfComputeTest, TraceVerification) {
    ExecutionTrace trace;
    trace.record_syscall(1, 10, 20);
    trace.record_syscall(2, 30, 40);
    trace.record_syscall(3, 50, 60);
    trace.create_checkpoint();
    
    ProofOfCompute proof;
    proof.syscall_count = 3;
    proof.checkpoint_hashes = trace.checkpoints;
    
    EXPECT_TRUE(proof.verify(trace));
    
    // Modify trace
    trace.record_syscall(4, 70, 80);
    EXPECT_FALSE(proof.verify(trace)); // Count mismatch
}

TEST_F(ProofTest, GeneratorBasicFlow) {
    std::string job_id = "test_job";
    std::string code = "print('hello world')";
    
    generator->start_recording(job_id, code);
    
    // Simulate some syscalls
    generator->record_syscall(1, 0, 0);
    generator->record_syscall(2, 0, 0);
    generator->record_syscall(3, 0, 0);
    
    // Create checkpoint
    generator->checkpoint();
    
    // Generate proof
    std::string output = "hello world\n";
    ProofOfCompute proof = generator->generate_proof(output, 0.5, 1024000);
    
    EXPECT_EQ(proof.job_id, job_id);
    EXPECT_FALSE(proof.code_hash.empty());
    EXPECT_FALSE(proof.output_hash.empty());
    EXPECT_FALSE(proof.execution_hash.empty());
    EXPECT_DOUBLE_EQ(proof.cpu_time, 0.5);
    EXPECT_EQ(proof.memory_peak, 1024000);
    EXPECT_EQ(proof.syscall_count, 3);
    EXPECT_EQ(proof.checkpoint_hashes.size(), 1);
}

TEST_F(ProofTest, GeneratorMultipleCheckpoints) {
    generator->start_recording("long_job", "long running code");
    
    // Simulate long-running job with multiple checkpoints
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 10; j++) {
            generator->record_syscall(j, i * 10 + j, 0);
        }
        generator->checkpoint();
    }
    
    ProofOfCompute proof = generator->generate_proof("output", 10.0, 2048000);
    
    EXPECT_EQ(proof.checkpoint_hashes.size(), 3);
    EXPECT_EQ(proof.syscall_count, 30);
}

TEST_F(ProofTest, DeterministicHashing) {
    // Two identical executions should produce same proof
    std::string code = "deterministic code";
    std::string output = "deterministic output";
    
    // First execution
    generator->start_recording("job1", code);
    generator->record_syscall(1, 100, 200);
    generator->record_syscall(2, 300, 400);
    ProofOfCompute proof1 = generator->generate_proof(output, 1.0, 1000);
    
    // Second execution (identical)
    generator->start_recording("job1", code);
    generator->record_syscall(1, 100, 200);
    generator->record_syscall(2, 300, 400);
    ProofOfCompute proof2 = generator->generate_proof(output, 1.0, 1000);
    
    EXPECT_EQ(proof1.code_hash, proof2.code_hash);
    EXPECT_EQ(proof1.output_hash, proof2.output_hash);
    EXPECT_EQ(proof1.execution_hash, proof2.execution_hash);
}

TEST_F(ProofTest, ConcurrentProofGeneration) {
    std::vector<std::thread> threads;
    std::vector<ProofOfCompute> proofs(5);
    
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([i, &proofs]() {
            ProofGenerator gen;
            std::string job_id = "concurrent_" + std::to_string(i);
            gen.start_recording(job_id, "code");
            
            for (int j = 0; j < 100; j++) {
                gen.record_syscall(j % 10, j, j * 2);
            }
            
            proofs[i] = gen.generate_proof("output", 0.1 * i, 1024 * i);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify all proofs were generated
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(proofs[i].job_id, "concurrent_" + std::to_string(i));
        EXPECT_EQ(proofs[i].syscall_count, 100);
        EXPECT_FALSE(proofs[i].execution_hash.empty());
    }
}

} // namespace
} // namespace sandrun