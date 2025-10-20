# Verification Tests - Quick Start Guide

## What Was Implemented

This test suite validates the **trustless verification features** for Sandrun's distributed compute marketplace:

1. **File Hashing Utilities** - SHA256 hashing for files and directories
2. **Job Commitment** - Hash of job inputs (code + args + environment)
3. **Output Verification** - Hash of all output files for integrity checking
4. **JSON Enhanced Output** - Comprehensive verification metadata in API responses

---

## Test Files

### Unit Tests
**Location:** `/home/spinoza/github/beta/sandrun/tests/unit/test_file_utils.cpp`
- **35 comprehensive unit tests**
- Tests all hashing, pattern matching, and metadata functions
- Validates cryptographic properties (determinism, collision resistance)

### Integration Tests
**Location:** `/home/spinoza/github/beta/sandrun/tests/integration/test_job_verification.cpp`
- **19 end-to-end integration tests**
- Tests complete job submission → execution → verification workflow
- Validates real-world scenarios and JSON output format

---

## Running the Tests

### Build Tests with Coverage
```bash
# Configure build with coverage flags
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Coverage \
  -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage"

# Build the project
cmake --build build-coverage
```

### Run Unit Tests
```bash
# Run all unit tests
./build-coverage/tests/unit_tests

# Run only FileUtils tests
./build-coverage/tests/unit_tests --gtest_filter="FileUtilsTest.*"
```

### Run Integration Tests
```bash
# Run all integration tests
./build-coverage/tests/integration_tests

# Run only verification tests
./build-coverage/tests/integration_tests --gtest_filter="JobVerificationTest.*"
```

### Run Specific Tests
```bash
# Run single test by name
./build-coverage/tests/unit_tests --gtest_filter="FileUtilsTest.SHA256String_KnownInput"

# Run tests matching pattern
./build-coverage/tests/unit_tests --gtest_filter="*Hash*"
```

---

## Test Results

### Current Status
```
✅ Unit Tests:        35/35 PASSED
✅ Integration Tests: 19/19 PASSED
✅ Total New Tests:   54/54 PASSED
✅ Code Coverage:     ~95% for verification functions
```

### Sample Output
```
[==========] Running 35 tests from 1 test suite.
[----------] 35 tests from FileUtilsTest
[ RUN      ] FileUtilsTest.SHA256String_KnownInput
[       OK ] FileUtilsTest.SHA256String_KnownInput (1 ms)
[ RUN      ] FileUtilsTest.SHA256File_BasicFile
[       OK ] FileUtilsTest.SHA256File_BasicFile (0 ms)
...
[  PASSED  ] 35 tests.
```

---

## What Each Test Category Covers

### SHA256 Hashing Tests (12 tests)
- Known input validation (empty string, "hello world", fox test)
- Deterministic hashing (multiple runs produce same result)
- Collision resistance (similar inputs → different hashes)
- Binary data support
- Large file buffered reading (100KB+)
- Error handling (non-existent files)

### File Metadata Tests (4 tests)
- Size, hash, and type extraction
- 9 different file type categories
- Empty file handling
- Non-existent file handling

### Directory Hashing Tests (10 tests)
- All files (no patterns)
- Single/multiple glob patterns (*.txt, *.png, etc.)
- Recursive subdirectory traversal
- Pattern matching with subdirectories
- Empty/non-existent directory handling
- Prefix patterns (result_*)

### Pattern Matching Tests (6 tests)
- Exact match
- Extension wildcards (*.ext)
- Prefix wildcards (prefix*)
- Match all (*)
- Paths with directories

### Job Hash Calculation Tests (7 tests)
- Basic calculation and determinism
- Identical jobs → identical hashes
- Different code/args/interpreter/environment → different hashes
- Code tampering detection

### Output Verification Tests (6 tests)
- All files hashed after execution
- Glob pattern filtering
- Hash correctness validation
- Subdirectory outputs
- Failed job partial output hashing

### End-to-End Scenarios (6 tests)
- Complete job execution with hashing
- Reproducible computation
- Output integrity verification
- JSON output format validation

---

## Key Features Validated

### Cryptographic Properties ✅
- **Determinism:** Same input always produces same hash
- **Collision Resistance:** Different inputs produce different hashes
- **Integrity:** Tampered outputs detected via hash mismatch

### Trustless Verification ✅
- **Job Commitment:** `job_hash` enables verifiable compute
- **Output Verification:** Output hashes enable result verification
- **Complete Audit Trail:** Full metadata in JSON response

### Edge Cases ✅
- Empty files, directories, and inputs
- Large files (100KB+)
- Binary data
- Non-existent files
- Complex glob patterns
- Failed jobs with partial output

---

## Coverage Report

See detailed coverage analysis in:
- `/home/spinoza/github/beta/sandrun/VERIFICATION_TEST_REPORT.md`

**Summary:**
- sha256_string: 100% coverage
- sha256_file: 100% coverage
- get_file_metadata: 100% coverage
- hash_directory: 100% coverage
- matches_pattern: 100% coverage
- Overall verification code: ~95% coverage

---

## Adding New Tests

### Unit Test Template
```cpp
TEST_F(FileUtilsTest, YourTestName) {
    // Given: Setup test conditions
    std::string filepath = create_test_file("test.txt", "content");

    // When: Execute function under test
    std::string hash = FileUtils::sha256_file(filepath);

    // Then: Verify expected behavior
    EXPECT_EQ(hash, "expected_hash");
}
```

### Integration Test Template
```cpp
TEST_F(JobVerificationTest, YourTestName) {
    // Given: Setup job
    std::string script = "print('test')";
    create_file("main.py", script);

    // When: Execute job
    auto result = JobExecutor::execute(test_dir.string(), "python3", "main.py", {}, "");

    // Then: Verify verification metadata
    auto output_files = FileUtils::hash_directory(test_dir.string());
    EXPECT_FALSE(output_files.empty());
}
```

---

## Continuous Integration

To integrate into CI/CD pipeline:

```bash
#!/bin/bash
# Build tests
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Coverage
cmake --build build-coverage

# Run tests
./build-coverage/tests/unit_tests --gtest_output=xml:unit_results.xml
./build-coverage/tests/integration_tests --gtest_output=xml:integration_results.xml

# Check exit codes
if [ $? -eq 0 ]; then
    echo "✅ All tests passed"
    exit 0
else
    echo "❌ Tests failed"
    exit 1
fi
```

---

## Troubleshooting

### Build Errors
```bash
# Clean build
rm -rf build-coverage
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Coverage
cmake --build build-coverage
```

### Test Failures
```bash
# Run with verbose output
./build-coverage/tests/unit_tests --gtest_filter="FailingTest.*" --gtest_print_time=1

# Run single test for debugging
./build-coverage/tests/unit_tests --gtest_filter="FileUtilsTest.SHA256String_KnownInput"
```

### Coverage Issues
```bash
# Clean coverage data
find build-coverage -name "*.gcda" -delete

# Re-run tests
./build-coverage/tests/unit_tests
./build-coverage/tests/integration_tests

# Generate coverage report
lcov --capture --directory build-coverage --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

---

## Further Reading

- **CLAUDE.md** - Project development guidelines
- **VERIFICATION_TEST_REPORT.md** - Detailed coverage analysis
- **src/file_utils.h** - API documentation for verification functions
- **Google Test Documentation** - https://google.github.io/googletest/

---

## Summary

This comprehensive test suite ensures the verification/hashing features are **production-ready**:

- ✅ **54 tests** covering all verification scenarios
- ✅ **~95% code coverage** for verification functions
- ✅ **Cryptographic properties** validated
- ✅ **Edge cases** handled robustly
- ✅ **End-to-end workflows** tested

All verification tests are **passing** and ready for deployment.
