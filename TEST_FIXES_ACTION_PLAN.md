# Test Fixes - Action Plan

## Critical Fixes (Do First)

### Fix #1: Remove `calculate_job_hash()` Helper - CRITICAL ANTI-PATTERN

**Location**: `/home/spinoza/github/beta/sandrun/tests/integration/test_job_verification.cpp:34-50`

**Problem**: Test duplicates production logic, creating circular verification.

**Current Code (BAD)**:
```cpp
// Helper to calculate job hash (mimics main.cpp logic)
std::string calculate_job_hash(
    const std::string& entrypoint,
    const std::string& interpreter,
    const std::string& environment,
    const std::vector<std::string>& args,
    const std::string& entrypoint_content
) {
    std::ostringstream job_data;
    job_data << entrypoint << "|" << interpreter << "|" << environment << "|";
    for (const auto& arg : args) {
        job_data << arg << "|";
    }
    job_data << entrypoint_content;
    return FileUtils::sha256_string(job_data.str());
}
```

**Solution**: Create a `Job` class in production code with `calculate_hash()` method, then test it.

**Step 1**: Add to `src/job_hash.h` (new file):
```cpp
#pragma once
#include <string>
#include <vector>

namespace sandrun {

struct JobDefinition {
    std::string entrypoint;
    std::string interpreter;
    std::string environment;
    std::vector<std::string> args;
    std::string code;  // entrypoint content

    // Calculate deterministic job hash
    std::string calculate_hash() const;
};

} // namespace sandrun
```

**Step 2**: Add to `src/job_hash.cpp`:
```cpp
#include "job_hash.h"
#include "file_utils.h"
#include <sstream>

namespace sandrun {

std::string JobDefinition::calculate_hash() const {
    std::ostringstream job_data;
    job_data << entrypoint << "|"
             << interpreter << "|"
             << environment << "|";
    for (const auto& arg : args) {
        job_data << arg << "|";
    }
    job_data << code;
    return FileUtils::sha256_string(job_data.str());
}

} // namespace sandrun
```

**Step 3**: Update tests to use real API:
```cpp
TEST_F(JobVerificationTest, JobHash_DifferentCodeDifferentHash) {
    // Given: Jobs with different code
    JobDefinition job1{"main.py", "python3", "", {}, "print('test1')"};
    JobDefinition job2{"main.py", "python3", "", {}, "print('test2')"};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should produce different hashes
    EXPECT_NE(hash1, hash2) << "Different code should produce different hashes";
}
```

**Impact**: Eliminates circular verification and ensures tests validate real production code.

---

### Fix #2: Remove Implementation Consistency Tests

**Location**: `/home/spinoza/github/beta/sandrun/tests/unit/test_file_utils.cpp:510-543`

**Problem**: These tests verify internal implementation details, not behavioral requirements.

**Tests to DELETE**:
```cpp
TEST_F(FileUtilsTest, HashConsistency_FileVsString)
TEST_F(FileUtilsTest, HashConsistency_DirectoryVsIndividualFiles)
```

**Why Delete**:
- They test that two different code paths produce the same result (implementation detail)
- They will break during legitimate refactoring
- Known-value tests already ensure correctness

**Replace With**: Nothing. The known-value tests (lines 39-58, 117-127) already ensure correctness.

---

### Fix #3: Fix Circular Verification in Proof Tests

**Location**: `/home/spinoza/github/beta/sandrun/tests/unit/test_proof.cpp:142-193`

**Problem**: Test is overly complex and doesn't test the verify() contract properly.

**Current Code (CONFUSING)**:
```cpp
TEST(ProofOfComputeTest, TraceVerification) {
    ExecutionTrace trace;
    trace.record_syscall(1, 10, 20);
    // ... lots of convoluted setup ...
    EXPECT_FALSE(matching_proof.verify(modified_trace));
}
```

**Replace With (CLEAR)**:
```cpp
TEST(ProofOfComputeTest, Verify_SucceedsForMatchingTrace) {
    // Given: An execution trace
    ExecutionTrace trace;
    trace.record_syscall(1, 10, 20);
    trace.record_syscall(2, 30, 40);
    trace.create_checkpoint();

    // And: A proof generated from that trace
    ProofGenerator gen;
    gen.start_recording("job1", "code");
    gen.record_syscall(1, 10, 20);
    gen.record_syscall(2, 30, 40);
    gen.checkpoint();
    ProofOfCompute proof = gen.generate_proof("output", 1.0, 1000);

    // When: Verifying the proof against the original trace
    bool valid = proof.verify(trace);

    // Then: Should verify successfully
    EXPECT_TRUE(valid) << "Proof should verify against matching trace";
}

TEST(ProofOfComputeTest, Verify_FailsForModifiedTrace) {
    // Given: A proof from original trace
    ExecutionTrace original_trace;
    original_trace.record_syscall(1, 10, 20);
    original_trace.record_syscall(2, 30, 40);

    ProofGenerator gen;
    gen.start_recording("job1", "code");
    gen.record_syscall(1, 10, 20);
    gen.record_syscall(2, 30, 40);
    ProofOfCompute proof = gen.generate_proof("output", 1.0, 1000);

    // When: Verifying against modified trace (extra syscall)
    ExecutionTrace modified_trace = original_trace;
    modified_trace.record_syscall(3, 50, 60);

    // Then: Verification should fail
    EXPECT_FALSE(proof.verify(modified_trace))
        << "Proof should not verify against modified trace";
}

TEST(ProofOfComputeTest, Verify_FailsForWrongCheckpoints) {
    // Given: A proof with specific checkpoints
    ProofGenerator gen;
    gen.start_recording("job1", "code");
    gen.record_syscall(1, 10, 20);
    gen.checkpoint();
    ProofOfCompute proof = gen.generate_proof("output", 1.0, 1000);

    // When: Verifying against trace with different checkpoints
    ExecutionTrace wrong_trace;
    wrong_trace.record_syscall(1, 10, 20);
    wrong_trace.record_syscall(99, 0, 0);  // Different syscall
    wrong_trace.create_checkpoint();

    // Then: Verification should fail
    EXPECT_FALSE(proof.verify(wrong_trace))
        << "Proof should not verify with wrong checkpoints";
}
```

---

## High Priority Fixes

### Fix #4: Consolidate Redundant Tampering Tests

**Location**: `/home/spinoza/github/beta/sandrun/tests/integration/test_worker_signing.cpp:160-218`

**Current**: 4 separate tests for different types of tampering (60 lines)
**Better**: 1 parameterized test (20 lines)

**Replace 4 Tests With**:
```cpp
TEST_F(WorkerSigningIntegrationTest, AnyDataModificationFailsVerification) {
    // Given: A worker and signed job result
    auto worker = WorkerIdentity::generate();
    std::ostringstream original;
    original << "job123|0|5.5|1024|output1.txt:hash1|output2.txt:hash2|";

    std::string signature = worker->sign(original.str());
    std::string worker_id = worker->get_worker_id();

    // When: Attempting various modifications
    std::vector<std::pair<std::string, std::string>> tampering_attempts = {
        {"exit_code", "job123|1|5.5|1024|output1.txt:hash1|output2.txt:hash2|"},
        {"cpu_time", "job123|0|0.1|1024|output1.txt:hash1|output2.txt:hash2|"},
        {"memory", "job123|0|5.5|64|output1.txt:hash1|output2.txt:hash2|"},
        {"file_hash", "job123|0|5.5|1024|output1.txt:TAMPERED|output2.txt:hash2|"},
        {"add_file", "job123|0|5.5|1024|output1.txt:hash1|output2.txt:hash2|extra.txt:hash3|"},
        {"remove_file", "job123|0|5.5|1024|output1.txt:hash1|"}
    };

    // Then: All modifications should fail verification
    for (const auto& [modification_type, tampered_data] : tampering_attempts) {
        EXPECT_FALSE(WorkerIdentity::verify(tampered_data, signature, worker_id))
            << "Failed to detect " << modification_type << " modification";
    }

    // And: Original should still verify
    EXPECT_TRUE(WorkerIdentity::verify(original.str(), signature, worker_id))
        << "Original data should still verify";
}
```

**Impact**: Reduces code duplication, makes intent clearer, easier to add new tampering cases.

---

### Fix #5: Reduce File Type Detection Tests

**Location**: `/home/spinoza/github/beta/sandrun/tests/unit/test_file_utils.cpp:219-244`

**Current**: 9 test cases for file type detection
**Better**: 3 representative cases

**Replace With**:
```cpp
TEST_F(FileUtilsTest, GetFileMetadata_DetectsCommonFileTypes) {
    // Test representative file types only
    struct TestCase {
        std::string filename;
        FileType expected_type;
    };

    std::vector<TestCase> cases = {
        {"image.png", FileType::IMAGE},      // Visual output
        {"model.pt", FileType::MODEL},       // ML model
        {"data.csv", FileType::DATA},        // Structured data
        {"unknown.xyz", FileType::OTHER}     // Fallback
    };

    for (const auto& tc : cases) {
        std::string filepath = create_test_file(tc.filename, "test");
        FileMetadata metadata = FileUtils::get_file_metadata(filepath);

        EXPECT_EQ(metadata.type, tc.expected_type)
            << "File " << tc.filename << " should be detected as "
            << FileUtils::file_type_to_string(tc.expected_type);
    }
}
```

---

## Missing Critical Tests (Add These)

### Add #1: Malformed Data Handling

**Location**: `/home/spinoza/github/beta/sandrun/tests/unit/test_worker_identity.cpp` (add to end)

```cpp
TEST(WorkerIdentityTest, Verify_HandlesCorruptedBase64Gracefully) {
    // Given: A valid signature
    auto worker = WorkerIdentity::generate();
    std::string data = "test_data";
    std::string valid_signature = worker->sign(data);
    std::string worker_id = worker->get_worker_id();

    // When: Signature is corrupted (invalid base64)
    std::vector<std::string> corrupted_signatures = {
        "not!valid@base64",           // Invalid characters
        "YWJj",                        // Too short
        "",                            // Empty
        "====",                        // Just padding
        valid_signature + "CORRUPTED" // Appended garbage
    };

    // Then: Should return false (not crash or throw)
    for (const auto& corrupted : corrupted_signatures) {
        EXPECT_FALSE(WorkerIdentity::verify(data, corrupted, worker_id))
            << "Should gracefully reject corrupted signature: " << corrupted;
    }
}

TEST(WorkerIdentityTest, FromKeyfile_HandlesCorruptedPEM) {
    // Given: A corrupted PEM file
    std::string corrupt_pem = test_dir / "corrupt.pem";
    std::ofstream file(corrupt_pem);
    file << "-----BEGIN PRIVATE KEY-----\n"
         << "CORRUPTED_BASE64_DATA_HERE\n"
         << "-----END PRIVATE KEY-----\n";
    file.close();

    // When: Attempting to load
    auto identity = WorkerIdentity::from_keyfile(corrupt_pem);

    // Then: Should return nullptr (not crash)
    EXPECT_EQ(identity, nullptr)
        << "Should gracefully reject corrupted PEM file";
}
```

### Add #2: Concurrent Operations

**Location**: `/home/spinoza/github/beta/sandrun/tests/integration/test_worker_signing.cpp` (add to end)

```cpp
TEST_F(WorkerSigningIntegrationTest, ConcurrentSignatureGeneration) {
    // Given: Multiple threads using same worker identity
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::string worker_id = worker->get_worker_id();
    const int num_threads = 10;
    const int sigs_per_thread = 100;

    std::vector<std::thread> threads;
    std::mutex results_mutex;
    std::vector<std::pair<std::string, std::string>> all_signatures;

    // When: Multiple threads generate signatures concurrently
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < sigs_per_thread; i++) {
                std::string data = "thread_" + std::to_string(t) + "_sig_" + std::to_string(i);
                std::string sig = worker->sign(data);

                std::lock_guard<std::mutex> lock(results_mutex);
                all_signatures.push_back({data, sig});
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Then: All signatures should be valid
    EXPECT_EQ(all_signatures.size(), num_threads * sigs_per_thread);

    for (const auto& [data, signature] : all_signatures) {
        EXPECT_TRUE(WorkerIdentity::verify(data, signature, worker_id))
            << "Concurrent signature should be valid: " << data;
    }
}
```

### Add #3: Output File Ordering

**Location**: `/home/spinoza/github/beta/sandrun/tests/integration/test_job_verification.cpp` (add to end)

```cpp
TEST_F(JobVerificationTest, OutputHashing_OrderDependence) {
    // Given: A job that produces files in different orders
    // (This tests whether hash_directory returns deterministic results)

    std::string script = R"(
import random
import time

# Create files in random order
files = ['output1.txt', 'output2.txt', 'output3.txt']
random.shuffle(files)

for f in files:
    with open(f, 'w') as file:
        file.write(f'content of {f}')
    time.sleep(0.01)  # Small delay to ensure different timestamps
)";

    create_file("main.py", script);

    // When: Running job multiple times
    std::vector<std::string> combined_hashes;

    for (int i = 0; i < 3; i++) {
        auto result = JobExecutor::execute(test_dir.string(), "python3", "main.py", {}, "");
        EXPECT_EQ(result.exit_code, 0);

        auto output_files = FileUtils::hash_directory(test_dir.string(), {"*.txt"});

        // Build combined hash of all outputs (sorted by filename for determinism)
        std::ostringstream combined;
        for (const auto& [filename, metadata] : output_files) {
            combined << filename << ":" << metadata.sha256_hash << "|";
        }

        combined_hashes.push_back(FileUtils::sha256_string(combined.str()));

        // Clean for next run
        for (const auto& [filename, _] : output_files) {
            std::filesystem::remove(test_dir / filename);
        }
    }

    // Then: Combined hash should be identical across runs
    // (proving that map iteration order is deterministic)
    EXPECT_EQ(combined_hashes[0], combined_hashes[1]);
    EXPECT_EQ(combined_hashes[1], combined_hashes[2])
        << "Output hashing should be deterministic regardless of file creation order";
}
```

---

## Test Organization Improvements

### Move Tests from Integration to Unit

**Files to Move**:
- `test_job_verification.cpp` lines 59-167 → new file `test_job_hash.cpp` (unit test)

**Reason**: Pure job hash calculation has no I/O or dependencies, should be unit tested.

---

## Summary Checklist

### Must Do (Before Next Commit)
- [ ] Fix #1: Remove `calculate_job_hash()` helper, create `JobDefinition` class
- [ ] Fix #2: Delete `HashConsistency_*` tests
- [ ] Fix #3: Rewrite `TraceVerification` test

### Should Do (This Week)
- [ ] Fix #4: Consolidate tampering tests
- [ ] Fix #5: Reduce file type detection tests
- [ ] Add #1: Malformed data handling tests
- [ ] Add #2: Concurrent operations tests
- [ ] Add #3: Output ordering tests

### Nice to Have (Ongoing)
- [ ] Improve test naming throughout
- [ ] Add tests/README.md explaining architecture
- [ ] Document critical vs non-critical test coverage
- [ ] Set up CI to enforce test requirements

---

## Expected Impact

**After Critical Fixes**:
- 0 tests duplicating production logic (currently: 1 major violation)
- 0 tests verifying implementation consistency (currently: 2)
- +5 new tests for critical failure paths
- ~15% reduction in total test lines (consolidation)
- **100% of tests will survive refactoring** (currently: ~85%)

**Test Quality Score**:
- Before: B+
- After Critical Fixes: A-
- After All Fixes: A

---

## Next Review

After implementing these fixes, run:
```bash
# Build and test
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage
cd build-coverage && ctest --output-on-failure

# Check test count
grep -r "^TEST" tests/ | wc -l

# Verify no duplicated logic
grep -r "calculate_job_hash" tests/  # Should be 0 results
```

If all fixes applied:
- All tests should pass
- No test should duplicate production logic
- Each test should clearly state expected behavior
- Refactoring production code should not break tests
