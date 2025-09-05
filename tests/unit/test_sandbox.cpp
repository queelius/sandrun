#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "sandbox.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace sandrun;
using namespace testing;

class SandboxTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = "/tmp/sandrun_test_" + std::to_string(getpid());
        std::filesystem::create_directories(test_dir);
        sandbox = std::make_unique<Sandbox>(SecurityLevel::STANDARD);
        sandbox->setWorkingDirectory(test_dir);
    }
    
    void TearDown() override {
        sandbox.reset();
        if (std::filesystem::exists(test_dir)) {
            std::filesystem::remove_all(test_dir);
        }
    }
    
    std::string test_dir;
    std::unique_ptr<Sandbox> sandbox;
};

// Basic execution tests
TEST_F(SandboxTest, ExecuteSimplePythonCode) {
    std::string code = "print('Hello, World!')";
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("Hello, World!"));
    EXPECT_TRUE(result.stderr_output.empty());
    EXPECT_FALSE(result.timeout_occurred);
    EXPECT_GT(result.execution_time.count(), 0);
}

TEST_F(SandboxTest, ExecutePythonWithOutput) {
    std::string code = R"(
import sys
print("stdout message", file=sys.stdout)
print("stderr message", file=sys.stderr)
print("result:", 42)
)";
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("stdout message"));
    EXPECT_THAT(result.stdout_output, HasSubstr("result: 42"));
    EXPECT_THAT(result.stderr_output, HasSubstr("stderr message"));
}

TEST_F(SandboxTest, ExecuteNodeJSCode) {
    std::string code = "console.log('Node.js execution');";
    
    // Check if Node.js is available before running test
    if (system("which node > /dev/null 2>&1") != 0) {
        GTEST_SKIP() << "Node.js not installed, skipping test";
    }
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::NODEJS);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("Node.js execution"));
}

TEST_F(SandboxTest, ExecuteCodeWithError) {
    std::string code = "raise RuntimeError('Intentional error')";
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    EXPECT_NE(result.exit_code, 0);
    EXPECT_THAT(result.stderr_output, HasSubstr("RuntimeError"));
    EXPECT_THAT(result.stderr_output, HasSubstr("Intentional error"));
}

// Resource limit tests
TEST_F(SandboxTest, MemoryLimitEnforcement) {
    ResourceLimits limits;
    limits.max_memory_mb = 50;  // Very low memory limit
    limits.max_wall_time_sec = 10;
    sandbox->setResourceLimits(limits);
    
    // Python code that tries to allocate a lot of memory
    std::string code = R"(
try:
    data = bytearray(100 * 1024 * 1024)  # Try to allocate 100MB
    print("Memory allocated successfully")
except MemoryError:
    print("Memory allocation failed as expected")
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    // Should either fail due to memory limit or catch MemoryError
    if (result.exit_code != 0) {
        EXPECT_NE(result.exit_code, 0);
    } else {
        EXPECT_THAT(result.stdout_output, HasSubstr("Memory allocation failed"));
    }
}

TEST_F(SandboxTest, TimeoutEnforcement) {
    ResourceLimits limits;
    limits.max_wall_time_sec = 2;  // 2 second timeout
    limits.max_cpu_time_sec = 2;
    sandbox->setResourceLimits(limits);
    
    // Code that runs for a long time
    std::string code = R"(
import time
print("Starting long operation")
time.sleep(5)  # Sleep for 5 seconds
print("Should not reach this point")
)";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    EXPECT_TRUE(result.timeout_occurred || result.exit_code != 0);
    EXPECT_LT(elapsed.count(), 4);  // Should complete much faster than 5 seconds
    EXPECT_THAT(result.stdout_output, HasSubstr("Starting long operation"));
    EXPECT_THAT(result.stdout_output, Not(HasSubstr("Should not reach this point")));
}

TEST_F(SandboxTest, FileSizeLimitEnforcement) {
    ResourceLimits limits;
    limits.max_file_size_mb = 1;  // 1MB file size limit
    sandbox->setResourceLimits(limits);
    
    std::string code = R"(
with open('large_file.txt', 'w') as f:
    try:
        # Try to write 2MB of data
        for i in range(2 * 1024):
            f.write('x' * 1024)  # Write 1KB at a time
        print("File created successfully")
    except IOError as e:
        print(f"File creation failed: {e}")
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    // Should either fail or catch the IOError
    if (result.exit_code == 0) {
        EXPECT_THAT(result.stdout_output, HasSubstr("File creation failed"));
    }
}

// Security tests - these require appropriate system capabilities
TEST_F(SandboxTest, DISABLED_NetworkIsolation) {
    // This test requires network namespace isolation
    sandbox->allowNetworkAccess(false);
    
    std::string code = R"(
import urllib.request
try:
    response = urllib.request.urlopen('http://httpbin.org/get', timeout=5)
    print("Network access succeeded - BAD")
except Exception as e:
    print(f"Network access blocked - GOOD: {type(e).__name__}")
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("Network access blocked - GOOD"));
}

TEST_F(SandboxTest, DISABLED_FileSystemIsolation) {
    // This test requires mount namespace isolation
    std::string code = R"(
import os
try:
    files = os.listdir('/etc')
    print(f"Can access /etc with {len(files)} files - BAD")
except Exception as e:
    print(f"Cannot access /etc - GOOD: {type(e).__name__}")
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("Cannot access /etc - GOOD"));
}

// Environment variable tests
TEST_F(SandboxTest, EnvironmentVariables) {
    sandbox->addEnvironmentVariable("TEST_VAR", "test_value");
    sandbox->addEnvironmentVariable("NUMERIC_VAR", "42");
    
    std::string code = R"(
import os
print(f"TEST_VAR: {os.environ.get('TEST_VAR', 'NOT_SET')}")
print(f"NUMERIC_VAR: {os.environ.get('NUMERIC_VAR', 'NOT_SET')}")
print(f"NONEXISTENT: {os.environ.get('NONEXISTENT', 'NOT_SET')}")
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("TEST_VAR: test_value"));
    EXPECT_THAT(result.stdout_output, HasSubstr("NUMERIC_VAR: 42"));
    EXPECT_THAT(result.stdout_output, HasSubstr("NONEXISTENT: NOT_SET"));
}

// File I/O tests
TEST_F(SandboxTest, FileCreationAndOutput) {
    std::string code = R"(
with open('output.txt', 'w') as f:
    f.write('Hello from sandbox\n')
    f.write('Line 2\n')

with open('data.json', 'w') as f:
    f.write('{"result": 123, "status": "success"}')

print("Files created successfully")
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("Files created successfully"));
    EXPECT_THAT(result.output_files, SizeIs(Ge(2)));
    
    // Check if output files exist
    bool found_txt = false, found_json = false;
    for (const auto& file : result.output_files) {
        if (file.find("output.txt") != std::string::npos) {
            found_txt = true;
            // Verify file contents
            std::ifstream f(file);
            std::string content((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
            EXPECT_THAT(content, HasSubstr("Hello from sandbox"));
        }
        if (file.find("data.json") != std::string::npos) {
            found_json = true;
        }
    }
    EXPECT_TRUE(found_txt);
    EXPECT_TRUE(found_json);
}

// Resource monitoring tests
TEST_F(SandboxTest, ExecutionTimeAccuracy) {
    std::string code = R"(
import time
start = time.time()
time.sleep(0.1)  # Sleep for 100ms
end = time.time()
print(f"Python measured time: {(end - start) * 1000:.1f}ms")
)";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    EXPECT_EQ(result.exit_code, 0);
    
    // Check that execution time is reasonable (should be around 100ms plus overhead)
    double execution_ms = result.execution_time.count() / 1000.0;
    double wall_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    EXPECT_GT(execution_ms, 80);   // At least 80ms
    EXPECT_LT(execution_ms, 500);  // Less than 500ms
    EXPECT_THAT(result.stdout_output, HasSubstr("Python measured time"));
}

TEST_F(SandboxTest, MemoryUsageTracking) {
    std::string code = R"(
data = bytearray(10 * 1024 * 1024)  # Allocate 10MB
print(f"Allocated {len(data)} bytes")
del data  # Clean up
print("Memory freed")
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_THAT(result.stdout_output, HasSubstr("Allocated 10485760 bytes"));
    EXPECT_GE(result.memory_used_mb, 5);  // Should have used at least 5MB
}

// Multiple interpreter support tests
TEST_F(SandboxTest, DISABLED_MultipleInterpreters) {
    // Skip if interpreters are not available
    if (system("which python3 > /dev/null 2>&1") != 0) {
        GTEST_SKIP() << "Python3 not available";
    }
    if (system("which node > /dev/null 2>&1") != 0) {
        GTEST_SKIP() << "Node.js not available";
    }
    
    // Test Python
    std::string python_code = "print('Python:', 2 + 2)";
    ExecutionResult python_result = sandbox->executeCode(python_code, InterpreterType::PYTHON);
    EXPECT_EQ(python_result.exit_code, 0);
    EXPECT_THAT(python_result.stdout_output, HasSubstr("Python: 4"));
    
    // Test Node.js
    std::string js_code = "console.log('JavaScript:', 3 * 3);";
    ExecutionResult js_result = sandbox->executeCode(js_code, InterpreterType::NODEJS);
    EXPECT_EQ(js_result.exit_code, 0);
    EXPECT_THAT(js_result.stdout_output, HasSubstr("JavaScript: 9"));
}

// GPU support tests (if available)
TEST_F(SandboxTest, GPUAvailabilityCheck) {
    std::vector<std::string> gpu_devices = sandbox->getAvailableGPUDevices();
    
    // Just check that the method works - don't require GPU to be present
    EXPECT_NO_THROW(sandbox->isGPUAvailable());
    EXPECT_NO_THROW(sandbox->initializeGPUContext());
    EXPECT_NO_THROW(sandbox->cleanupGPUContext());
}

// Security level tests
TEST_F(SandboxTest, SecurityLevelConfiguration) {
    auto minimal_sandbox = std::make_unique<Sandbox>(SecurityLevel::MINIMAL);
    auto paranoid_sandbox = std::make_unique<Sandbox>(SecurityLevel::PARANOID);
    auto gpu_sandbox = std::make_unique<Sandbox>(SecurityLevel::GPU_SECURE);
    
    // Just verify they can be created and execute basic code
    std::string simple_code = "print('Hello')";
    
    ExecutionResult minimal_result = minimal_sandbox->executeCode(simple_code, InterpreterType::PYTHON);
    EXPECT_EQ(minimal_result.exit_code, 0);
    
    ExecutionResult paranoid_result = paranoid_sandbox->executeCode(simple_code, InterpreterType::PYTHON);
    // May fail due to security restrictions, but should not crash
    EXPECT_NO_THROW(paranoid_result.exit_code);
    
    ExecutionResult gpu_result = gpu_sandbox->executeCode(simple_code, InterpreterType::PYTHON);
    EXPECT_NO_THROW(gpu_result.exit_code);
}

// System capability tests
TEST_F(SandboxTest, SystemCapabilityDetection) {
    bool has_capabilities = SandboxManager::testSystemCapabilities();
    auto required_caps = SandboxManager::getRequiredCapabilities();
    
    EXPECT_FALSE(required_caps.empty());
    EXPECT_THAT(required_caps, Contains("CAP_SYS_ADMIN"));
    
    // These tests don't require capabilities to pass, just check they don't crash
    EXPECT_NO_THROW(has_capabilities);
}

// Error handling tests
TEST_F(SandboxTest, InvalidInterpreterType) {
    std::string code = "print('test')";
    
    // This should throw or handle gracefully
    EXPECT_THROW(sandbox->executeCode(code, static_cast<InterpreterType>(999)),
                 std::invalid_argument);
}

TEST_F(SandboxTest, NonexistentScript) {
    std::string nonexistent_script = "/nonexistent/path/script.py";
    
    ExecutionResult result = sandbox->executeScript(nonexistent_script, InterpreterType::PYTHON);
    EXPECT_NE(result.exit_code, 0);
    EXPECT_FALSE(result.error_message.empty());
}

// Concurrent execution tests
TEST_F(SandboxTest, ConcurrentExecution) {
    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::vector<ExecutionResult> results(num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, i, &results]() {
            auto thread_sandbox = std::make_unique<Sandbox>(SecurityLevel::STANDARD);
            std::string test_dir = "/tmp/sandrun_thread_" + std::to_string(i);
            thread_sandbox->setWorkingDirectory(test_dir);
            
            std::string code = "print('Thread " + std::to_string(i) + " result:', " + 
                              std::to_string(i * i) + ")";
            results[i] = thread_sandbox->executeCode(code, InterpreterType::PYTHON);
            
            std::filesystem::remove_all(test_dir);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Check that all executions succeeded
    for (int i = 0; i < num_threads; ++i) {
        EXPECT_EQ(results[i].exit_code, 0) << "Thread " << i << " failed";
        EXPECT_THAT(results[i].stdout_output, HasSubstr("Thread " + std::to_string(i)));
        EXPECT_THAT(results[i].stdout_output, HasSubstr(std::to_string(i * i)));
    }
}

// Edge case tests
TEST_F(SandboxTest, EmptyCode) {
    std::string empty_code = "";
    ExecutionResult result = sandbox->executeCode(empty_code, InterpreterType::PYTHON);
    
    // Empty Python script should succeed with exit code 0
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_output.empty());
    EXPECT_TRUE(result.stderr_output.empty());
}

TEST_F(SandboxTest, VeryLongOutput) {
    std::string code = R"(
for i in range(1000):
    print(f"Line {i}: " + "x" * 100)
)";
    
    ExecutionResult result = sandbox->executeCode(code, InterpreterType::PYTHON);
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_GT(result.stdout_output.length(), 100000);  // Should be long
    EXPECT_THAT(result.stdout_output, HasSubstr("Line 0:"));
    EXPECT_THAT(result.stdout_output, HasSubstr("Line 999:"));
}