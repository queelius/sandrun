#include <gtest/gtest.h>
#include "sandbox.h"
#include "job_executor.h"
#include "rate_limiter.h"
#include "proof.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <sstream>

namespace sandrun {
namespace {

class JobExecutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
        test_dir = std::filesystem::temp_directory_path() / "sandrun_integration_test";
        std::filesystem::create_directories(test_dir);
        
        // Initialize components
        rate_limiter = std::make_unique<RateLimiter>();
        executor = std::make_unique<JobExecutor>();
        proof_gen = std::make_unique<ProofGenerator>();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::path test_dir;
    std::unique_ptr<RateLimiter> rate_limiter;
    std::unique_ptr<JobExecutor> executor;
    std::unique_ptr<ProofGenerator> proof_gen;
};

TEST_F(JobExecutionTest, EndToEndPythonJob) {
    // Create a Python script
    std::string script_content = R"(
import json
import sys

def process_data(data):
    result = {
        "input_length": len(data),
        "processed": True,
        "output": data.upper()
    }
    return result

if __name__ == "__main__":
    input_data = "hello world"
    result = process_data(input_data)
    print(json.dumps(result))
)";

    // Create job with manifest
    TestJob job;
    job.id = "integration_test_1";
    job.manifest.entrypoint = "process.py";
    job.manifest.interpreter = "python3";
    job.manifest.timeout = 10;
    job.manifest.memory_mb = 128;
    job.files["process.py"] = script_content;
    
    // Check rate limit
    auto quota = rate_limiter->check_quota("127.0.0.1");
    ASSERT_TRUE(quota.can_submit);
    
    // Register job
    ASSERT_TRUE(rate_limiter->register_job_start("127.0.0.1", job.id));
    
    // Start proof recording
    proof_gen->start_recording(job.id, script_content);
    
    // Execute job
    SandboxConfig config;
    config.interpreter = job.manifest.interpreter;
    config.timeout = std::chrono::seconds(job.manifest.timeout);
    config.memory_limit_bytes = job.manifest.memory_mb * 1024 * 1024;
    
    Sandbox sandbox(config);
    JobResult result = sandbox.execute(script_content, job.id);
    
    // Verify execution
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("\"processed\": true") != std::string::npos);
    EXPECT_TRUE(result.output.find("\"output\": \"HELLO WORLD\"") != std::string::npos);
    
    // Generate proof
    ProofOfCompute proof = proof_gen->generate_proof(
        result.output, 
        result.cpu_seconds, 
        result.memory_bytes
    );
    
    // Verify proof
    EXPECT_EQ(proof.job_id, job.id);
    EXPECT_FALSE(proof.execution_hash.empty());
    EXPECT_FALSE(proof.output_hash.empty());
    EXPECT_GT(proof.cpu_time, 0.0);
    
    // Complete job in rate limiter
    rate_limiter->register_job_end("127.0.0.1", job.id, result.cpu_seconds);
    
    // Check updated quota
    quota = rate_limiter->check_quota("127.0.0.1");
    EXPECT_LT(quota.cpu_seconds_available, 10.0);
}

TEST_F(JobExecutionTest, JobWithDependencies) {
    // Create a Python script that imports a library
    std::string requirements = "numpy==1.24.0\npandas==2.0.0\n";
    std::string script = R"(
import numpy as np
import pandas as pd

# Create sample data
data = np.random.randn(100)
df = pd.DataFrame(data, columns=['value'])

# Calculate statistics
stats = {
    'mean': df['value'].mean(),
    'std': df['value'].std(),
    'min': df['value'].min(),
    'max': df['value'].max()
}

print(f"Statistics: {stats}")
)";

    TestJob job;
    job.id = "test_deps";
    job.manifest.entrypoint = "analyze.py";
    job.manifest.requirements = "requirements.txt";
    job.files["analyze.py"] = script;
    job.files["requirements.txt"] = requirements;
    
    // Note: This test would need pip available in sandbox
    // and would take longer due to dependency installation
}

TEST_F(JobExecutionTest, ConcurrentJobExecution) {
    std::vector<std::thread> threads;
    std::vector<JobResult> results(5);
    
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([i, &results]() {
            std::string job_id = "concurrent_" + std::to_string(i);
            std::string code = "print('Job " + std::to_string(i) + " executing')";
            
            SandboxConfig config;
            config.interpreter = "python3";
            Sandbox sandbox(config);
            
            results[i] = sandbox.execute(code, job_id);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify all jobs completed
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(results[i].exit_code, 0);
        std::string expected = "Job " + std::to_string(i) + " executing";
        EXPECT_TRUE(results[i].output.find(expected) != std::string::npos);
    }
}

TEST_F(JobExecutionTest, JobWithFileOutput) {
    std::string script = R"(
# Generate output files
with open('result.txt', 'w') as f:
    f.write('Processing complete\n')
    
with open('data.json', 'w') as f:
    import json
    json.dump({'status': 'success', 'value': 42}, f)
    
print('Files created')
)";

    TestJob job;
    job.id = "file_output_test";
    job.manifest.entrypoint = "generate.py";
    job.manifest.outputs = {"result.txt", "data.json"};
    job.files["generate.py"] = script;
    
    SandboxConfig config;
    config.interpreter = "python3";
    Sandbox sandbox(config);
    
    JobResult result = sandbox.execute(script, job.id);
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Files created") != std::string::npos);
    
    // In real implementation, would verify output files are captured
}

TEST_F(JobExecutionTest, JobChainWithCheckpoints) {
    // Simulate a long job with checkpoints
    std::string script = R"(
import time
import json

def checkpoint(step, data):
    print(f"CHECKPOINT:{step}:{json.dumps(data)}")

# Step 1
checkpoint(1, {"status": "starting", "progress": 0})
time.sleep(0.1)

# Step 2
checkpoint(2, {"status": "processing", "progress": 50})
time.sleep(0.1)

# Step 3
checkpoint(3, {"status": "complete", "progress": 100})
print("Job complete")
)";

    ProofGenerator proof_gen;
    proof_gen.start_recording("checkpoint_job", script);
    
    SandboxConfig config;
    config.interpreter = "python3";
    Sandbox sandbox(config);
    
    // Capture checkpoints during execution
    std::vector<std::string> checkpoints;
    
    JobResult result = sandbox.execute(script, "checkpoint_job");
    
    // Parse checkpoints from output
    std::stringstream ss(result.output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("CHECKPOINT:") == 0) {
            checkpoints.push_back(line);
            proof_gen.checkpoint(); // Create proof checkpoint
        }
    }
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(checkpoints.size(), 3);
    
    ProofOfCompute proof = proof_gen.generate_proof(result.output, result.cpu_seconds, result.memory_bytes);
    EXPECT_EQ(proof.checkpoint_hashes.size(), 3);
}

TEST_F(JobExecutionTest, ErrorHandlingAndRecovery) {
    // Test job that fails initially then succeeds on retry
    std::string script = R"(
import os
import sys

retry_file = '/tmp/retry_marker'

if not os.path.exists(retry_file):
    # First run - fail
    with open(retry_file, 'w') as f:
        f.write('failed')
    print("First attempt failed")
    sys.exit(1)
else:
    # Retry - succeed
    print("Retry successful")
    sys.exit(0)
)";

    SandboxConfig config;
    config.interpreter = "python3";
    
    // First attempt
    Sandbox sandbox1(config);
    JobResult result1 = sandbox1.execute(script, "retry_job_1");
    EXPECT_NE(result1.exit_code, 0);
    EXPECT_TRUE(result1.output.find("First attempt failed") != std::string::npos);
    
    // Note: In sandbox, /tmp is isolated, so this wouldn't actually work
    // This is more to demonstrate the pattern
}

TEST_F(JobExecutionTest, ResourceMetrics) {
    std::string script = R"(
import numpy as np

# Allocate some memory
data = np.zeros((10000, 1000))  # ~80MB

# Do some computation
result = np.sum(data * 2 + 1)
print(f"Result: {result}")
)";

    SandboxConfig config;
    config.interpreter = "python3";
    config.memory_limit_bytes = 200 * 1024 * 1024; // 200MB
    
    Sandbox sandbox(config);
    JobResult result = sandbox.execute(script, "metrics_job");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_GT(result.cpu_seconds, 0.0);
    EXPECT_GT(result.memory_bytes, 50 * 1024 * 1024); // Should use at least 50MB
    EXPECT_LT(result.memory_bytes, 200 * 1024 * 1024); // Should be under limit
}

} // namespace
} // namespace sandrun