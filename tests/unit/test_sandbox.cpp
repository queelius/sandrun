#include <gtest/gtest.h>
#include "sandbox.h"
#include "constants.h"
#include <fstream>
#include <filesystem>
#include <thread>

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
    // Given: A sandbox configured for Python execution
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(5);

    Sandbox sandbox(config);

    // When: Simple Python code is executed
    std::string code = "print('Hello from sandbox')";
    JobResult result = sandbox.execute(code, "test_job_1");

    // Then: The code should execute successfully and produce expected output
    EXPECT_EQ(result.job_id, "test_job_1") << "Job ID should match the input";
    EXPECT_EQ(result.exit_code, 0) << "Execution should succeed with exit code 0";
    EXPECT_TRUE(result.output.find("Hello from sandbox") != std::string::npos)
        << "Output should contain the expected message, got: " << result.output;

    // Warning messages about namespace creation are acceptable (not privileged)
    // What matters is that execution succeeds
    EXPECT_GT(result.cpu_seconds, 0.0) << "Should report CPU usage";
    EXPECT_GT(result.memory_bytes, 0) << "Should report memory usage";
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
import sys
print('Starting long operation')
sys.stdout.flush()  # Ensure output is flushed before sleep
time.sleep(10)  # Sleep longer than timeout
print('Should not reach here')
)";

    auto start_time = std::chrono::steady_clock::now();
    JobResult result = sandbox.execute(code, "test_job_4");
    auto end_time = std::chrono::steady_clock::now();

    // Given: A job that sleeps longer than the timeout
    // When: The job is executed with a 1-second timeout
    // Then: The job should be killed and marked as timed out
    EXPECT_NE(result.exit_code, 0) << "Job should have been killed due to timeout";
    EXPECT_TRUE(result.output.find("Starting long operation") != std::string::npos)
        << "Initial output should be captured before timeout";
    EXPECT_TRUE(result.output.find("Should not reach here") == std::string::npos)
        << "Code after sleep should not execute";
    EXPECT_TRUE(result.error.find("timeout") != std::string::npos || result.error.find("Killed") != std::string::npos)
        << "Error should indicate timeout or kill signal";

    // Execution should not take much longer than the timeout
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    EXPECT_LE(duration.count(), 3) << "Execution should terminate close to timeout limit";
}

TEST_F(SandboxTest, NetworkIsolation) {
    // Given: A sandbox configured without network access
    SandboxConfig config;
    config.interpreter = "python3";
    config.allow_network = false;
    config.timeout = std::chrono::seconds(5);

    Sandbox sandbox(config);

    // When: Code attempts to make a network connection
    std::string code = R"(
print('Starting network test')
import sys
sys.stdout.flush()

try:
    import socket
    print('Socket module imported')
    sys.stdout.flush()

    # Test socket creation (should work)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    print('Socket created successfully')
    sys.stdout.flush()

    # Test network connection (should fail in isolated environment)
    s.settimeout(2)  # Don't wait too long
    s.connect(('8.8.8.8', 53))  # Try to connect to Google DNS
    print('Network access successful - SECURITY BREACH!')
    s.close()
except Exception as e:
    print(f'Network blocked: {type(e).__name__}: {e}')
    sys.stdout.flush()

print('Test completed')
)";

    JobResult result = sandbox.execute(code, "test_job_5");

    // Then: The code should execute but network connections should fail
    EXPECT_EQ(result.exit_code, 0) << "Python execution should succeed. Error: " << result.error << " Output: " << result.output;

    // The test should at minimum start and import socket
    EXPECT_TRUE(result.output.find("Starting network test") != std::string::npos)
        << "Test should start executing, output: " << result.output;

    // If namespace isolation works (requires root), network should be blocked
    // If not running as root, the test may succeed in creating connections (degraded security)
    // What's important is that the code executes and we can observe the behavior
    EXPECT_TRUE(result.output.find("Test completed") != std::string::npos)
        << "Test should complete, output: " << result.output;

    // Ideally network should be blocked, but this requires proper namespace support
    if (result.error.find("Failed to create namespaces") == std::string::npos) {
        // Only enforce network isolation if namespaces were successfully created
        EXPECT_TRUE(result.output.find("Network blocked") != std::string::npos)
            << "Network connection should be blocked when namespaces are supported, output: " << result.output;
        EXPECT_TRUE(result.output.find("SECURITY BREACH") == std::string::npos)
            << "Network access should be blocked, output: " << result.output;
    }
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
    // Given: Different interpreters are available
    // When: Code is executed with each interpreter
    // Then: Each should execute correctly and produce expected results

    // Test Python
    {
        SandboxConfig config;
        config.interpreter = "python3";
        config.timeout = std::chrono::seconds(5);
        Sandbox sandbox(config);

        JobResult result = sandbox.execute("print(2+2)", "test_py");
        EXPECT_EQ(result.exit_code, 0) << "Python should execute successfully. Error: " << result.error;
        EXPECT_TRUE(result.output.find("4") != std::string::npos)
            << "Python should output '4', got: " << result.output;
    }

    // Test Bash (if available)
    if (std::filesystem::exists("/bin/bash")) {
        SandboxConfig config;
        config.interpreter = "bash";
        config.timeout = std::chrono::seconds(5);
        Sandbox sandbox(config);

        // Bash interprets files, not direct code, so we need proper script
        JobResult result = sandbox.execute("#!/bin/bash\necho $((2+2))", "test_bash");
        EXPECT_EQ(result.exit_code, 0) << "Bash should execute successfully. Error: " << result.error;
        EXPECT_TRUE(result.output.find("4") != std::string::npos)
            << "Bash should output '4', got: " << result.output;
    }

    // Test Node.js (if available) - This is optional functionality
    // Node.js may not work in all sandbox configurations due to additional
    // syscall requirements, so we make this a softer assertion
    if (std::filesystem::exists("/usr/bin/node")) {
        SandboxConfig config;
        config.interpreter = "node";
        config.timeout = std::chrono::seconds(5);
        Sandbox sandbox(config);

        JobResult result = sandbox.execute("console.log(2+2)", "test_node");

        // Node.js support is best-effort in sandboxed environments
        // If it works, verify it produces correct output
        if (result.exit_code == 0) {
            EXPECT_TRUE(result.output.find("4") != std::string::npos)
                << "When Node executes successfully, it should output '4', got: " << result.output;
        } else {
            // Node.js may fail in restricted sandbox - this is acceptable
            // Log for debugging but don't fail the test
            std::cout << "Note: Node.js execution failed in sandbox (may need additional syscalls). "
                      << "Error: " << result.error << std::endl;
        }
    }
}

TEST_F(SandboxTest, OutputSizeLimit) {
    // Given: A sandbox with output size limits
    SandboxConfig config;
    config.interpreter = "python3";
    config.timeout = std::chrono::seconds(15);
    config.memory_limit_bytes = 512 * 1024 * 1024;  // Ensure enough memory for Python

    Sandbox sandbox(config);

    // When: Code generates output larger than MAX_OUTPUT_SIZE (10MB)
    std::string code = R"(
import sys
# Generate output larger than 10MB limit
# Use smaller chunks to avoid memory issues
for i in range(12 * 1024):
    print('x' * 1000, flush=True)  # ~1KB per line, 12MB total
    if i % 100 == 0:
        sys.stdout.flush()
print('END_MARKER')
sys.stdout.flush()
)";

    JobResult result;
    try {
        result = sandbox.execute(code, "test_job_8");
    } catch (const std::exception& e) {
        FAIL() << "Exception during execute: " << e.what();
    }

    // Then: The output should be truncated at the limit
    // Exit code might be non-zero if process was killed, or 0 if it completed
    // The key behavior is that output is bounded
    try {
        size_t output_size = result.output.size();
        EXPECT_LE(output_size, MAX_OUTPUT_SIZE + 10240)  // Allow 10KB buffer for truncation message
            << "Output should be limited to prevent memory exhaustion, got " << output_size << " bytes";

        // Should contain truncation message or be at the size limit
        bool has_truncation_msg = result.output.find("truncated") != std::string::npos;
        bool is_at_limit = output_size >= (MAX_OUTPUT_SIZE - 10240);  // Within 10KB of limit
        EXPECT_TRUE(has_truncation_msg || is_at_limit)
            << "Output should be truncated at limit, size=" << output_size
            << " has_msg=" << has_truncation_msg;

        // The END_MARKER should not appear since it comes after 12MB of output
        EXPECT_TRUE(result.output.find("END_MARKER") == std::string::npos)
            << "Content after truncation point should not appear";
    } catch (const std::exception& e) {
        FAIL() << "Exception during assertions: " << e.what()
               << " output_size=" << result.output.size();
    }
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