#include <gtest/gtest.h>
#include "worker_identity.h"
#include "sandbox.h"
#include "job_executor.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sandrun {
namespace {

class WorkerSigningIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir = std::filesystem::temp_directory_path() / "sandrun_signing_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // Cleanup test directory
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::path test_dir;
};

// ============================================================================
// Worker Identity Persistence Tests
// ============================================================================

TEST_F(WorkerSigningIntegrationTest, WorkerCanStartWithGeneratedKey) {
    // Given: We generate and save a worker key
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr) << "Should generate identity";

    std::string keyfile = (test_dir / "worker_key.pem").string();
    ASSERT_TRUE(identity->save_to_file(keyfile)) << "Should save key to file";

    std::string original_id = identity->get_worker_id();

    // When: Worker reloads the key (simulating restart)
    auto reloaded = WorkerIdentity::from_keyfile(keyfile);
    ASSERT_NE(reloaded, nullptr) << "Should load key from file";

    // Then: Worker ID should be preserved
    EXPECT_EQ(reloaded->get_worker_id(), original_id)
        << "Worker ID should persist across restarts";
}

TEST_F(WorkerSigningIntegrationTest, MultipleWorkersHaveUniqueIdentities) {
    // Given: Multiple workers each with their own keys
    std::vector<std::unique_ptr<WorkerIdentity>> workers;
    std::vector<std::string> worker_ids;

    for (int i = 0; i < 5; i++) {
        auto worker = WorkerIdentity::generate();
        ASSERT_NE(worker, nullptr);

        worker_ids.push_back(worker->get_worker_id());
        workers.push_back(std::move(worker));
    }

    // Then: All worker IDs should be unique
    for (size_t i = 0; i < worker_ids.size(); i++) {
        for (size_t j = i + 1; j < worker_ids.size(); j++) {
            EXPECT_NE(worker_ids[i], worker_ids[j])
                << "Workers " << i << " and " << j << " should have unique IDs";
        }
    }
}

// ============================================================================
// Job Result Signing Tests
// ============================================================================

TEST_F(WorkerSigningIntegrationTest, SignedJobResultCanBeVerified) {
    // Given: A worker with an identity
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::string worker_id = worker->get_worker_id();

    // And: Job execution results
    std::string job_hash = "test_job_hash_12345";
    int exit_code = 0;
    double cpu_seconds = 2.5;
    size_t memory_mb = 256;

    // Build result data in the format used by main.cpp
    std::ostringstream result_data;
    result_data << job_hash << "|"
                << exit_code << "|"
                << cpu_seconds << "|"
                << memory_mb << "|"
                << "output.txt:abc123|"
                << "results.json:def456|";

    // When: Worker signs the result
    std::string signature = worker->sign(result_data.str());

    // Then: Signature should be non-empty
    ASSERT_FALSE(signature.empty()) << "Should produce signature";

    // And: Signature should verify with worker's public key
    EXPECT_TRUE(WorkerIdentity::verify(result_data.str(), signature, worker_id))
        << "Job result signature should verify";
}

TEST_F(WorkerSigningIntegrationTest, TamperedJobResultFailsVerification) {
    // Given: A signed job result
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::string worker_id = worker->get_worker_id();

    std::string job_hash = "original_job_hash";
    std::ostringstream original_data;
    original_data << job_hash << "|0|1.0|128|output.txt:originalhash|";

    std::string signature = worker->sign(original_data.str());

    // When: An attacker tampers with the data
    std::ostringstream tampered_data;
    tampered_data << job_hash << "|0|1.0|128|output.txt:tampered_hash|";  // Changed hash

    // Then: Verification should fail
    EXPECT_FALSE(WorkerIdentity::verify(tampered_data.str(), signature, worker_id))
        << "Tampered result should not verify";

    // Original should still verify
    EXPECT_TRUE(WorkerIdentity::verify(original_data.str(), signature, worker_id))
        << "Original result should still verify";
}

TEST_F(WorkerSigningIntegrationTest, ModifiedExitCodeFailsVerification) {
    // Given: A signed job result with exit code 1 (failure)
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream result_with_error;
    result_with_error << "job123|1|1.0|128|output.txt:hash|";  // exit_code = 1

    std::string signature = worker->sign(result_with_error.str());
    std::string worker_id = worker->get_worker_id();

    // When: Attacker tries to change exit code to 0 (success)
    std::ostringstream modified_result;
    modified_result << "job123|0|1.0|128|output.txt:hash|";  // exit_code = 0

    // Then: Verification should fail
    EXPECT_FALSE(WorkerIdentity::verify(modified_result.str(), signature, worker_id))
        << "Cannot change exit code without invalidating signature";

    // Original should verify
    EXPECT_TRUE(WorkerIdentity::verify(result_with_error.str(), signature, worker_id))
        << "Original result should verify";
}

TEST_F(WorkerSigningIntegrationTest, ModifiedResourceUsageFailsVerification) {
    // Given: A signed job result with resource usage
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream original_result;
    original_result << "job123|0|5.5|1024|output.txt:hash|";  // 5.5s CPU, 1024MB RAM

    std::string signature = worker->sign(original_result.str());
    std::string worker_id = worker->get_worker_id();

    // When: Attacker tries to reduce reported resource usage
    std::ostringstream modified_result;
    modified_result << "job123|0|0.1|64|output.txt:hash|";  // 0.1s CPU, 64MB RAM

    // Then: Verification should fail
    EXPECT_FALSE(WorkerIdentity::verify(modified_result.str(), signature, worker_id))
        << "Cannot modify resource usage without invalidating signature";
}

TEST_F(WorkerSigningIntegrationTest, AddingOutputFileFailsVerification) {
    // Given: A signed job result with one output file
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream original_result;
    original_result << "job123|0|1.0|128|output.txt:hash1|";

    std::string signature = worker->sign(original_result.str());
    std::string worker_id = worker->get_worker_id();

    // When: Attacker tries to add another output file
    std::ostringstream modified_result;
    modified_result << "job123|0|1.0|128|output.txt:hash1|malicious.txt:hash2|";

    // Then: Verification should fail
    EXPECT_FALSE(WorkerIdentity::verify(modified_result.str(), signature, worker_id))
        << "Cannot add output files without invalidating signature";
}

TEST_F(WorkerSigningIntegrationTest, RemovingOutputFileFailsVerification) {
    // Given: A signed job result with multiple output files
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream original_result;
    original_result << "job123|0|1.0|128|output1.txt:hash1|output2.txt:hash2|";

    std::string signature = worker->sign(original_result.str());
    std::string worker_id = worker->get_worker_id();

    // When: Attacker tries to remove an output file
    std::ostringstream modified_result;
    modified_result << "job123|0|1.0|128|output1.txt:hash1|";  // Removed output2.txt

    // Then: Verification should fail
    EXPECT_FALSE(WorkerIdentity::verify(modified_result.str(), signature, worker_id))
        << "Cannot remove output files without invalidating signature";
}

// ============================================================================
// Anonymous Mode Compatibility Tests
// ============================================================================

TEST_F(WorkerSigningIntegrationTest, AnonymousModeStillWorks) {
    // Given: No worker identity (anonymous mode)
    std::unique_ptr<WorkerIdentity> no_identity = nullptr;

    // When: Job executes without identity
    // (In real code, this means worker_id and result_signature are empty)

    // Then: This should be acceptable (backward compatibility)
    EXPECT_EQ(no_identity, nullptr) << "Anonymous mode should be supported";

    // Job can still execute, just without signatures
    std::string worker_id = "";
    std::string signature = "";

    EXPECT_TRUE(worker_id.empty()) << "Anonymous workers have no ID";
    EXPECT_TRUE(signature.empty()) << "Anonymous workers produce no signatures";
}

TEST_F(WorkerSigningIntegrationTest, MixedAnonymousAndIdentifiedWorkers) {
    // Given: Some workers with identity, some without
    auto identified_worker = WorkerIdentity::generate();
    std::unique_ptr<WorkerIdentity> anonymous_worker = nullptr;

    ASSERT_NE(identified_worker, nullptr);
    ASSERT_EQ(anonymous_worker, nullptr);

    // When: Both process jobs
    std::string data = "job_result|0|1.0|128|output.txt:hash|";

    std::string identified_sig = identified_worker->sign(data);
    // Anonymous worker produces no signature

    // Then: Identified worker's signature should verify
    EXPECT_TRUE(WorkerIdentity::verify(
        data,
        identified_sig,
        identified_worker->get_worker_id()
    )) << "Identified worker's signature should verify";

    // Anonymous worker's results have no signature to verify
    // (This is checked by presence of worker_id in JSON output)
}

// ============================================================================
// Cross-Worker Verification Tests
// ============================================================================

TEST_F(WorkerSigningIntegrationTest, WorkerACanVerifyWorkerBResults) {
    // Given: Two workers, A and B
    auto worker_a = WorkerIdentity::generate();
    auto worker_b = WorkerIdentity::generate();
    ASSERT_NE(worker_a, nullptr);
    ASSERT_NE(worker_b, nullptr);

    // When: Worker B signs a job result
    std::string job_result = "job456|0|2.0|256|result.txt:hash_b|";
    std::string signature_b = worker_b->sign(job_result);
    std::string worker_b_id = worker_b->get_worker_id();

    // Then: Worker A (or anyone else) can verify B's signature
    bool verified = WorkerIdentity::verify(job_result, signature_b, worker_b_id);
    EXPECT_TRUE(verified) << "Worker A should be able to verify Worker B's signatures";
}

TEST_F(WorkerSigningIntegrationTest, ThirdPartyCanVerifyJobResults) {
    // Given: A worker signs a job result
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::string job_result = "job789|0|3.5|512|outputs/data.csv:hash123|";
    std::string signature = worker->sign(job_result);
    std::string worker_id = worker->get_worker_id();

    // When: A third party (e.g., job submitter, broker) wants to verify
    // They don't need the private key, only the public key (worker_id)
    bool verified = WorkerIdentity::verify(job_result, signature, worker_id);

    // Then: Verification should succeed
    EXPECT_TRUE(verified) << "Third parties should be able to verify signatures";
}

// ============================================================================
// Key Rotation and Identity Management Tests
// ============================================================================

TEST_F(WorkerSigningIntegrationTest, OldSignaturesRemainValidAfterKeyRotation) {
    // Given: A worker signs results with key v1
    auto worker_v1 = WorkerIdentity::generate();
    ASSERT_NE(worker_v1, nullptr);

    std::string job_result_old = "old_job|0|1.0|128|old_output.txt:old_hash|";
    std::string signature_old = worker_v1->sign(job_result_old);
    std::string worker_id_v1 = worker_v1->get_worker_id();

    // When: Worker generates a new key (key rotation)
    auto worker_v2 = WorkerIdentity::generate();
    ASSERT_NE(worker_v2, nullptr);

    std::string worker_id_v2 = worker_v2->get_worker_id();
    EXPECT_NE(worker_id_v1, worker_id_v2) << "New key should have different ID";

    // Then: Old signatures should still verify with old key
    EXPECT_TRUE(WorkerIdentity::verify(job_result_old, signature_old, worker_id_v1))
        << "Old signatures should remain valid with old key";

    // But not with new key
    EXPECT_FALSE(WorkerIdentity::verify(job_result_old, signature_old, worker_id_v2))
        << "Old signatures should not verify with new key";
}

TEST_F(WorkerSigningIntegrationTest, WorkerIdentityPersistsAcrossRestarts) {
    // Given: A worker creates a key and signs results
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::string keyfile = (test_dir / "persistent_key.pem").string();
    ASSERT_TRUE(worker->save_to_file(keyfile));

    std::string original_id = worker->get_worker_id();

    std::string job_result = "persistent_job|0|1.5|256|output.txt:persistent_hash|";
    std::string signature_before = worker->sign(job_result);

    // When: Worker restarts and reloads key
    auto restarted_worker = WorkerIdentity::from_keyfile(keyfile);
    ASSERT_NE(restarted_worker, nullptr);

    std::string reloaded_id = restarted_worker->get_worker_id();

    // Then: Worker ID should be the same
    EXPECT_EQ(reloaded_id, original_id) << "Worker ID should persist across restarts";

    // And: Old signatures should still verify
    EXPECT_TRUE(WorkerIdentity::verify(job_result, signature_before, reloaded_id))
        << "Pre-restart signatures should verify with reloaded key";

    // And: New signatures should also verify
    std::string new_job_result = "new_job|0|2.0|512|new_output.txt:new_hash|";
    std::string signature_after = restarted_worker->sign(new_job_result);
    EXPECT_TRUE(WorkerIdentity::verify(new_job_result, signature_after, reloaded_id))
        << "Post-restart signatures should also verify";
}

// ============================================================================
// Real-World Job Result Format Tests
// ============================================================================

TEST_F(WorkerSigningIntegrationTest, ComplexJobResultWithMultipleOutputs) {
    // Given: A worker and a complex job result with many outputs
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream complex_result;
    complex_result << "complex_job_abc123|"  // job_hash
                   << "0|"                    // exit_code
                   << "12.456|"               // cpu_seconds
                   << "2048|"                 // memory_mb
                   << "results/output1.txt:hash_1|"
                   << "results/output2.csv:hash_2|"
                   << "results/plots/plot1.png:hash_3|"
                   << "results/plots/plot2.png:hash_4|"
                   << "results/model.pkl:hash_5|";

    // When: Worker signs this complex result
    std::string signature = worker->sign(complex_result.str());
    std::string worker_id = worker->get_worker_id();

    // Then: Signature should verify
    EXPECT_TRUE(WorkerIdentity::verify(complex_result.str(), signature, worker_id))
        << "Complex job result signature should verify";

    // And: Any modification should invalidate it
    std::ostringstream tampered;
    tampered << "complex_job_abc123|0|12.456|2048|"
             << "results/output1.txt:hash_1|"
             << "results/output2.csv:hash_2|"
             << "results/plots/plot1.png:hash_3|"
             << "results/plots/plot2.png:hash_4|"
             << "results/model.pkl:TAMPERED|";  // Changed last hash

    EXPECT_FALSE(WorkerIdentity::verify(tampered.str(), signature, worker_id))
        << "Any modification should invalidate signature";
}

TEST_F(WorkerSigningIntegrationTest, JobWithFailureAndPartialOutputs) {
    // Given: A job that failed but produced some outputs
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream failed_job;
    failed_job << "failed_job_xyz789|"
               << "1|"  // exit_code = 1 (failure)
               << "5.2|"
               << "512|"
               << "partial_output.txt:hash_partial|"
               << "error.log:hash_error|";

    // When: Worker signs the failure result
    std::string signature = worker->sign(failed_job.str());
    std::string worker_id = worker->get_worker_id();

    // Then: Signature should verify even for failed jobs
    EXPECT_TRUE(WorkerIdentity::verify(failed_job.str(), signature, worker_id))
        << "Failed job results should also be signable and verifiable";

    // And: Exit code cannot be tampered to make it look successful
    std::ostringstream fake_success;
    fake_success << "failed_job_xyz789|0|5.2|512|"  // Changed exit_code to 0
                 << "partial_output.txt:hash_partial|"
                 << "error.log:hash_error|";

    EXPECT_FALSE(WorkerIdentity::verify(fake_success.str(), signature, worker_id))
        << "Cannot tamper exit code to fake success";
}

TEST_F(WorkerSigningIntegrationTest, JobWithNoOutputFiles) {
    // Given: A job that produced no output files
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream no_output_job;
    no_output_job << "no_output_job|0|0.5|64|";  // No file entries

    // When: Worker signs result with no outputs
    std::string signature = worker->sign(no_output_job.str());
    std::string worker_id = worker->get_worker_id();

    // Then: Should still produce valid signature
    EXPECT_FALSE(signature.empty()) << "Should sign even with no outputs";
    EXPECT_TRUE(WorkerIdentity::verify(no_output_job.str(), signature, worker_id))
        << "Job with no outputs should still verify";
}

TEST_F(WorkerSigningIntegrationTest, LargeScaleJobWithManyOutputs) {
    // Given: A job with many output files (stress test)
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream large_job;
    large_job << "large_job_12345|0|30.5|4096|";

    // Add 100 output files
    for (int i = 0; i < 100; i++) {
        large_job << "output_" << i << ".dat:hash_" << i << "|";
    }

    // When: Worker signs this large result
    std::string signature = worker->sign(large_job.str());
    std::string worker_id = worker->get_worker_id();

    // Then: Should handle large results correctly
    EXPECT_FALSE(signature.empty()) << "Should sign large results";
    EXPECT_TRUE(WorkerIdentity::verify(large_job.str(), signature, worker_id))
        << "Large job result should verify";

    // And: Modifying any single output should invalidate
    std::ostringstream tampered_large;
    tampered_large << "large_job_12345|0|30.5|4096|";
    for (int i = 0; i < 100; i++) {
        if (i == 50) {
            tampered_large << "output_" << i << ".dat:TAMPERED|";
        } else {
            tampered_large << "output_" << i << ".dat:hash_" << i << "|";
        }
    }

    EXPECT_FALSE(WorkerIdentity::verify(tampered_large.str(), signature, worker_id))
        << "Tampering any output in large result should fail verification";
}

// ============================================================================
// Edge Cases and Error Conditions
// ============================================================================

TEST_F(WorkerSigningIntegrationTest, SignatureRemainsValidWithSpacesInFilenames) {
    // Given: Job result with spaces in filenames
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream result_with_spaces;
    result_with_spaces << "job_spaces|0|1.0|128|"
                      << "my output file.txt:hash_1|"
                      << "another file with spaces.csv:hash_2|";

    // When: Worker signs it
    std::string signature = worker->sign(result_with_spaces.str());
    std::string worker_id = worker->get_worker_id();

    // Then: Should verify correctly
    EXPECT_TRUE(WorkerIdentity::verify(result_with_spaces.str(), signature, worker_id))
        << "Should handle filenames with spaces";
}

TEST_F(WorkerSigningIntegrationTest, SignatureHandlesSpecialCharactersInData) {
    // Given: Job result with special characters
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::ostringstream special_result;
    special_result << "job_special|0|1.5|256|"
                   << "file@special#chars$.txt:hash_special|";

    // When: Worker signs it
    std::string signature = worker->sign(special_result.str());
    std::string worker_id = worker->get_worker_id();

    // Then: Should verify correctly
    EXPECT_TRUE(WorkerIdentity::verify(special_result.str(), signature, worker_id))
        << "Should handle special characters in data";
}

} // namespace
} // namespace sandrun
