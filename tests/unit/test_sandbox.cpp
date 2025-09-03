#include <gtest/gtest.h>
#include "sandbox.h"
#include "constants.h"
#include <fstream>
#include <filesystem>

namespace sandrun {
namespace {

class SandboxTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir = std::filesystem::temp_directory_path() / "sandrun_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // Cleanup test directory
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::path test_dir;
};

TEST_F(SandboxTest, ExecuteSimplePythonScript) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(5);
    
    Sandbox sandbox(config);
    
    std::string code = "print('Hello from sandbox')";
    JobResult result = sandbox.execute(code, "test_job_1");
    
    EXPECT_EQ(result.job_id, "test_job_1");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Hello from sandbox") != std::string::npos);
    EXPECT_TRUE(result.error.empty());
    EXPECT_GT(result.cpu_seconds, 0.0);
    EXPECT_GT(result.memory_bytes, 0);
}

TEST_F(SandboxTest, ExecuteWithError) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(5);
    
    Sandbox sandbox(config);
    
    std::string code = "print('Before error')\nraise ValueError('Test error')\nprint('After error')";
    JobResult result = sandbox.execute(code, "test_job_2");
    
    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Before error") != std::string::npos);
    EXPECT_TRUE(result.output.find("After error") == std::string::npos);
    EXPECT_TRUE(result.error.find("ValueError: Test error") != std::string::npos);
}

TEST_F(SandboxTest, MemoryLimit) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.memory_limit_bytes = 50 * 1024 * 1024; // 50MB
    config.timeout = std::chrono::seconds(10);
    
    Sandbox sandbox(config);
    
    // Try to allocate more than limit
    std::string code = R"(
import numpy as np
# Try to allocate 100MB (more than 50MB limit)
data = np.zeros((100 * 1024 * 1024,), dtype=np.uint8)
print('Should not reach here')
)";
    
    JobResult result = sandbox.execute(code, "test_job_3");
    
    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Should not reach here") == std::string::npos);
    // Memory allocation should fail
}

TEST_F(SandboxTest, TimeoutEnforcement) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(1);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import time
print('Starting long operation')
time.sleep(10)  # Sleep longer than timeout
print('Should not reach here')
)";
    
    JobResult result = sandbox.execute(code, "test_job_4");
    
    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Starting long operation") != std::string::npos);
    EXPECT_TRUE(result.output.find("Should not reach here") == std::string::npos);
    EXPECT_TRUE(result.error.find("timeout") != std::string::npos);
}

TEST_F(SandboxTest, NetworkIsolation) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.allow_network = false; // Should be isolated by default
    config.timeout = std::chrono::seconds(5);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import socket
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('google.com', 80))
    print('Network access successful')
except Exception as e:
    print(f'Network blocked: {e}')
)";
    
    JobResult result = sandbox.execute(code, "test_job_5");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Network blocked") != std::string::npos);
    EXPECT_TRUE(result.output.find("Network access successful") == std::string::npos);
}

TEST_F(SandboxTest, FileSystemIsolation) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(5);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import os
# Try to access parent directories
try:
    files = os.listdir('/')
    print(f'Root access: {len(files)} files')
except Exception as e:
    print(f'Access denied: {e}')

# Should be able to write to working directory
with open('test.txt', 'w') as f:
    f.write('test data')
print('Write to working dir successful')
)";
    
    JobResult result = sandbox.execute(code, "test_job_6");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Write to working dir successful") != std::string::npos);
}

TEST_F(SandboxTest, GPUConfiguration) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.gpu_device_id = 0;
    config.gpu_memory_limit_bytes = 2ULL * 1024 * 1024 * 1024; // 2GB
    config.timeout = std::chrono::seconds(5);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import os
# Check if CUDA environment is set
cuda_device = os.environ.get('CUDA_VISIBLE_DEVICES', 'not set')
print(f'CUDA_VISIBLE_DEVICES: {cuda_device}')
)";
    
    JobResult result = sandbox.execute(code, "test_job_7");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("CUDA_VISIBLE_DEVICES: 0") != std::string::npos);
}

TEST_F(SandboxTest, MultipleInterpreters) {
    // Test Python
    {
        SandboxConfig config;
        config.interpreter = "python3";
        Sandbox sandbox(config);
        
        JobResult result = sandbox.execute("print(2+2)", "test_py");
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("4") != std::string::npos);
    }
    
    // Test Bash
    {
        SandboxConfig config;
        config.interpreter = "bash";
        Sandbox sandbox(config);
        
        JobResult result = sandbox.execute("echo $((2+2))", "test_bash");
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("4") != std::string::npos);
    }
    
    // Test Node.js (if available)
    if (std::filesystem::exists("/usr/bin/node")) {
        SandboxConfig config;
        config.interpreter = "node";
        Sandbox sandbox(config);
        
        JobResult result = sandbox.execute("console.log(2+2)", "test_node");
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("4") != std::string::npos);
    }
}

TEST_F(SandboxTest, OutputSizeLimit) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(10);
    
    Sandbox sandbox(config);
    
    // Generate more output than MAX_OUTPUT_SIZE (10MB)
    std::string code = R"(
# Generate 11MB of output
for i in range(11 * 1024):
    print('x' * 1024)  # 1KB per line
print('END_MARKER')
)";
    
    JobResult result = sandbox.execute(code, "test_job_8");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_LE(result.output.size(), MAX_OUTPUT_SIZE + 1024); // Allow some buffer
    EXPECT_TRUE(result.output.find("Output truncated") != std::string::npos);
    EXPECT_TRUE(result.output.find("END_MARKER") == std::string::npos);
}

TEST_F(SandboxTest, ConcurrentExecution) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(5);
    
    Sandbox sandbox1(config);
    Sandbox sandbox2(config);
    
    // Execute jobs in parallel
    std::thread t1([&sandbox1]() {
        std::string code = "import time; time.sleep(0.5); print('Job 1 complete')";
        JobResult result = sandbox1.execute(code, "concurrent_1");
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("Job 1 complete") != std::string::npos);
    });
    
    std::thread t2([&sandbox2]() {
        std::string code = "import time; time.sleep(0.5); print('Job 2 complete')";
        JobResult result = sandbox2.execute(code, "concurrent_2");
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("Job 2 complete") != std::string::npos);
    });
    
    t1.join();
    t2.join();
}

} // namespace
} // namespace sandrun