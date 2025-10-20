# Testing Guide

Comprehensive guide to testing Sandrun.

## Test Structure

```
tests/
├── CMakeLists.txt
├── unit/                    Unit tests
│   ├── test_file_utils.cpp
│   ├── test_worker_identity.cpp
│   ├── test_sandbox.cpp
│   └── ...
└── integration/             Integration tests
    ├── test_job_verification.cpp
    ├── test_worker_signing.cpp
    └── ...
```

## Running Tests

### Quick Start

```bash
# Build and run all tests
./scripts/run_tests.sh
```

### Unit Tests

```bash
# Build tests
cmake --build build

# Run all unit tests
./build/tests/unit_tests

# Run specific test suite
./build/tests/unit_tests --gtest_filter=FileUtilsTest.*

# Run specific test
./build/tests/unit_tests --gtest_filter=FileUtilsTest.SHA256File
```

### Integration Tests

```bash
# Integration tests may require sudo
sudo ./build/tests/integration_tests

# Run specific integration test
sudo ./build/tests/integration_tests --gtest_filter=JobVerificationTest.*
```

### With Output

```bash
# Verbose output
./build/tests/unit_tests --gtest_color=yes

# XML output (for CI)
./build/tests/unit_tests --gtest_output=xml:test_results.xml
```

## Test Coverage

### Generate Coverage Report

```bash
# Build with coverage flags
cmake -B build-coverage \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="--coverage"

cmake --build build-coverage

# Run tests
./build-coverage/tests/unit_tests
./build-coverage/tests/integration_tests

# Generate report
lcov --directory build-coverage --capture --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report

# View report
firefox coverage_report/index.html
```

### Coverage Targets

- **Critical paths**: 100% (crypto, verification, sandbox)
- **Core features**: 90%+ (job execution, HTTP server)
- **Overall**: 80%+

## Writing Tests

### Unit Test Example

```cpp
#include <gtest/gtest.h>
#include "file_utils.h"

using namespace sandrun;

TEST(FileUtilsTest, SHA256File) {
    // Create temporary file
    std::string test_file = "/tmp/test_sha256.txt";
    std::ofstream f(test_file);
    f << "Hello, World!";
    f.close();

    // Calculate hash
    std::string hash = FileUtils::sha256_file(test_file);

    // Verify against known SHA256
    EXPECT_EQ(hash, "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f");

    // Cleanup
    std::remove(test_file.c_str());
}

TEST(FileUtilsTest, SHA256FileNotFound) {
    std::string hash = FileUtils::sha256_file("/nonexistent/file.txt");
    EXPECT_EQ(hash, "");
}
```

### Integration Test Example

```cpp
#include <gtest/gtest.h>
#include "sandbox.h"
#include "job_executor.h"

TEST(SandboxIntegrationTest, PythonExecution) {
    // Create test job
    Job job;
    job.job_id = "test-job-123";
    job.entrypoint = "test.py";
    job.interpreter = "python3";
    job.working_dir = "/tmp/test_sandbox";

    // Create job files
    std::filesystem::create_directories(job.working_dir);
    std::ofstream script(job.working_dir + "/test.py");
    script << "print('Test passed!')";
    script.close();

    // Execute in sandbox
    Sandbox sandbox;
    JobResult result = sandbox.execute(job);

    // Verify results
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Test passed!") != std::string::npos);

    // Cleanup
    std::filesystem::remove_all(job.working_dir);
}
```

## Test Categories

### 1. Unit Tests

**File Utils (`test_file_utils.cpp`)**
- SHA256 hashing (35 tests)
- File metadata extraction
- Directory hashing
- Glob pattern matching

**Worker Identity (`test_worker_identity.cpp`)**
- Keypair generation (30 tests)
- PEM file I/O
- Signing and verification
- Tampering detection

**Sandbox (`test_sandbox.cpp`)**
- Namespace creation
- Seccomp filtering
- Resource limits
- Cleanup

### 2. Integration Tests

**Job Verification (`test_job_verification.cpp`)**
- End-to-end job hash calculation (19 tests)
- Output file hashing
- JSON response format
- Verification workflows

**Worker Signing (`test_worker_signing.cpp`)**
- Job signature generation (20 tests)
- Signature verification
- Tampering detection
- Cross-worker verification

**Pool Integration (`integrations/trusted-pool/test_pool.sh`)**
- Worker health checks
- Job distribution
- Status proxying
- Output download

## Test Data

### Fixtures

```cpp
class SandboxTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test environment
        test_dir = "/tmp/sandrun_test_" + random_string();
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // Cleanup
        std::filesystem::remove_all(test_dir);
    }

    std::string test_dir;
};

TEST_F(SandboxTestFixture, IsolatedExecution) {
    // Test uses test_dir from fixture
}
```

### Mock Data

```cpp
// Mock job
Job create_test_job() {
    Job job;
    job.job_id = "test-abc123";
    job.entrypoint = "main.py";
    job.interpreter = "python3";
    job.timeout = 60;
    job.memory_mb = 256;
    return job;
}
```

## Continuous Integration

### GitHub Actions

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libseccomp-dev libcap-dev libssl-dev libgtest-dev

      - name: Build
        run: |
          cmake -B build
          cmake --build build

      - name: Run tests
        run: |
          ./build/tests/unit_tests --gtest_output=xml:test_results.xml

      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: test-results
          path: test_results.xml
```

## Performance Testing

### Benchmarks

```cpp
#include <benchmark/benchmark.h>

static void BM_SHA256(benchmark::State& state) {
    std::string data(state.range(0), 'x');

    for (auto _ : state) {
        FileUtils::sha256_string(data);
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

BENCHMARK(BM_SHA256)->Range(1<<10, 1<<20);  // 1KB to 1MB
```

### Load Testing

```bash
# Concurrent job submissions
for i in {1..100}; do
    curl -X POST http://localhost:8443/submit \
      -F "files=@test.tar.gz" \
      -F 'manifest={"entrypoint":"test.py"}' &
done

wait
```

## Debugging Failed Tests

### Verbose Output

```bash
# Run with verbose flag
./build/tests/unit_tests --gtest_color=yes --gtest_print_time=1
```

### Repeat Failed Test

```bash
# Run specific failed test
./build/tests/unit_tests --gtest_filter=FailedTest --gtest_repeat=10
```

### Valgrind Check

```bash
# Check for memory leaks
valgrind --leak-check=full ./build/tests/unit_tests --gtest_filter=FailedTest
```

## Test Best Practices

### 1. Test Behavior, Not Implementation

❌ **Bad:**
```cpp
TEST(JobExecutorTest, UsesCorrectDataStructure) {
    EXPECT_TRUE(executor.uses_vector());  // Testing implementation
}
```

✅ **Good:**
```cpp
TEST(JobExecutorTest, ExecutesJobsInOrder) {
    executor.submit(job1);
    executor.submit(job2);
    EXPECT_EQ(executor.next(), job1);  // Testing behavior
}
```

### 2. Isolated Tests

Each test should be independent:

```cpp
TEST(FileUtilsTest, Test1) {
    // Create own temp file
    // Test logic
    // Clean up
}

TEST(FileUtilsTest, Test2) {
    // Don't depend on Test1
}
```

### 3. Descriptive Names

```cpp
// Good test names describe what they test
TEST(SandboxTest, FailsWhenEntrypointMissing)
TEST(SandboxTest, EnforcesMemoryLimit)
TEST(SandboxTest, IsolatesNetworkAccess)
```

### 4. AAA Pattern

```cpp
TEST(Example, DoSomething) {
    // Arrange: Setup test data
    Job job = create_test_job();

    // Act: Perform action
    Result result = execute(job);

    // Assert: Verify outcome
    EXPECT_EQ(result.exit_code, 0);
}
```

## Next Steps

- [Building Guide](building.md)
- [Architecture](../architecture.md)
- [Contributing](https://github.com/yourusername/sandrun/blob/master/CONTRIBUTING.md)
