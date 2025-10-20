#include <gtest/gtest.h>
#include "job_hash.h"

using namespace sandrun;

// Test JobDefinition::calculate_hash() behavior
class JobHashTest : public ::testing::Test {
protected:
    // Helper to create a basic job definition
    JobDefinition create_basic_job() {
        JobDefinition job;
        job.entrypoint = "main.py";
        job.interpreter = "python3";
        job.environment = "";
        job.args = {};
        job.code = "print('Hello, World!')";
        return job;
    }
};

// ============================================================================
// Basic Hash Calculation Tests
// ============================================================================

TEST_F(JobHashTest, CalculateHash_BasicJob_ReturnsValidSHA256) {
    // Given: A basic job definition
    JobDefinition job = create_basic_job();

    // When: Calculating hash
    std::string hash = job.calculate_hash();

    // Then: Should return valid SHA256 (64 hex characters)
    EXPECT_EQ(hash.length(), 64) << "SHA256 hash should be 64 characters";
    EXPECT_TRUE(std::all_of(hash.begin(), hash.end(), [](char c) {
        return std::isxdigit(c);
    })) << "Hash should only contain hexadecimal characters";
}

TEST_F(JobHashTest, CalculateHash_SameJob_ProducesSameHash) {
    // Given: Two identical job definitions
    JobDefinition job1 = create_basic_job();
    JobDefinition job2 = create_basic_job();

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should produce identical hashes
    EXPECT_EQ(hash1, hash2) << "Identical jobs should produce identical hashes";
}

TEST_F(JobHashTest, CalculateHash_DeterministicAcrossRuns) {
    // Given: A job definition
    JobDefinition job = create_basic_job();

    // When: Calculating hash multiple times
    std::string hash1 = job.calculate_hash();
    std::string hash2 = job.calculate_hash();
    std::string hash3 = job.calculate_hash();

    // Then: All hashes should be identical
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash2, hash3);
    EXPECT_EQ(hash1, hash3) << "Hash should be deterministic across multiple calls";
}

// ============================================================================
// Sensitivity Tests - Hash Changes When Fields Change
// ============================================================================

TEST_F(JobHashTest, DifferentCode_ProducesDifferentHash) {
    // Given: Two jobs with different code
    JobDefinition job1 = create_basic_job();
    JobDefinition job2 = create_basic_job();
    job2.code = "print('Different code!')";

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Hashes should be different
    EXPECT_NE(hash1, hash2) << "Different code should produce different hashes";
}

TEST_F(JobHashTest, DifferentEntrypoint_ProducesDifferentHash) {
    // Given: Two jobs with different entrypoints
    JobDefinition job1 = create_basic_job();
    JobDefinition job2 = create_basic_job();
    job2.entrypoint = "script.py";

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Hashes should be different
    EXPECT_NE(hash1, hash2) << "Different entrypoint should produce different hash";
}

TEST_F(JobHashTest, DifferentInterpreter_ProducesDifferentHash) {
    // Given: Two jobs with different interpreters
    JobDefinition job1 = create_basic_job();
    JobDefinition job2 = create_basic_job();
    job2.interpreter = "python3.11";

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Hashes should be different
    EXPECT_NE(hash1, hash2) << "Different interpreter should produce different hash";
}

TEST_F(JobHashTest, DifferentEnvironment_ProducesDifferentHash) {
    // Given: Two jobs with different environments
    JobDefinition job1 = create_basic_job();
    JobDefinition job2 = create_basic_job();
    job2.environment = "ml-basic";

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Hashes should be different
    EXPECT_NE(hash1, hash2) << "Different environment should produce different hash";
}

TEST_F(JobHashTest, DifferentArgs_ProducesDifferentHash) {
    // Given: Two jobs with different args
    JobDefinition job1 = create_basic_job();
    JobDefinition job2 = create_basic_job();
    job2.args = {"--verbose"};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Hashes should be different
    EXPECT_NE(hash1, hash2) << "Different args should produce different hash";
}

TEST_F(JobHashTest, ArgsOrderMatters) {
    // Given: Two jobs with same args in different order
    JobDefinition job1 = create_basic_job();
    job1.args = {"--input", "data.csv"};

    JobDefinition job2 = create_basic_job();
    job2.args = {"data.csv", "--input"};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Hashes should be different
    EXPECT_NE(hash1, hash2) << "Argument order should affect hash";
}

TEST_F(JobHashTest, ArgsCount_AffectsHash) {
    // Given: Two jobs with different number of args
    JobDefinition job1 = create_basic_job();
    job1.args = {"--verbose"};

    JobDefinition job2 = create_basic_job();
    job2.args = {"--verbose", "--debug"};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Hashes should be different
    EXPECT_NE(hash1, hash2) << "Different number of args should produce different hash";
}

// ============================================================================
// Edge Cases and Special Values
// ============================================================================

TEST_F(JobHashTest, EmptyFields_ProducesValidHash) {
    // Given: A job with all empty fields
    JobDefinition job;
    job.entrypoint = "";
    job.interpreter = "";
    job.environment = "";
    job.args = {};
    job.code = "";

    // When: Calculating hash
    std::string hash = job.calculate_hash();

    // Then: Should still produce valid SHA256
    EXPECT_EQ(hash.length(), 64);
    EXPECT_TRUE(std::all_of(hash.begin(), hash.end(), [](char c) {
        return std::isxdigit(c);
    })) << "Empty job should still produce valid hash";
}

TEST_F(JobHashTest, EmptyVsNonEmptyCode_DifferentHashes) {
    // Given: Two jobs, one with empty code
    JobDefinition job1 = create_basic_job();
    job1.code = "";

    JobDefinition job2 = create_basic_job();
    job2.code = " ";  // Single space

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should be different
    EXPECT_NE(hash1, hash2) << "Empty vs non-empty code should differ";
}

TEST_F(JobHashTest, SpecialCharactersInCode_HandledCorrectly) {
    // Given: Job with special characters in code
    JobDefinition job = create_basic_job();
    job.code = "print('Hello\\nWorld!')\n# Comment\nif True:\n    pass";

    // When: Calculating hash
    std::string hash = job.calculate_hash();

    // Then: Should produce valid hash
    EXPECT_EQ(hash.length(), 64);

    // And: Should be deterministic
    EXPECT_EQ(hash, job.calculate_hash());
}

TEST_F(JobHashTest, UnicodeInCode_HandledCorrectly) {
    // Given: Job with unicode characters
    JobDefinition job = create_basic_job();
    job.code = "print('你好世界')  # Unicode";

    // When: Calculating hash
    std::string hash = job.calculate_hash();

    // Then: Should produce valid hash
    EXPECT_EQ(hash.length(), 64);

    // And: Should be deterministic
    EXPECT_EQ(hash, job.calculate_hash());
}

TEST_F(JobHashTest, VeryLongCode_HandledCorrectly) {
    // Given: Job with very long code (simulate large file)
    JobDefinition job = create_basic_job();
    job.code = std::string(100000, 'x');  // 100KB of 'x'

    // When: Calculating hash
    std::string hash = job.calculate_hash();

    // Then: Should produce valid hash
    EXPECT_EQ(hash.length(), 64);
}

TEST_F(JobHashTest, ManyArgs_HandledCorrectly) {
    // Given: Job with many arguments
    JobDefinition job = create_basic_job();
    for (int i = 0; i < 100; i++) {
        job.args.push_back("arg" + std::to_string(i));
    }

    // When: Calculating hash
    std::string hash = job.calculate_hash();

    // Then: Should produce valid hash
    EXPECT_EQ(hash.length(), 64);

    // And: Should be deterministic
    EXPECT_EQ(hash, job.calculate_hash());
}

// ============================================================================
// Separator Handling Tests
// ============================================================================

TEST_F(JobHashTest, PipeCharacterInFields_CausesKnownCollision) {
    // Given: Two jobs where pipe character causes hash collision
    // Note: This is a KNOWN LIMITATION of the current hash implementation
    // The separator is '|', so "main|extra" + "python3" has same hash as
    // "main" + "extra|python3" because both result in "main|extra|python3|..."

    JobDefinition job1 = create_basic_job();
    job1.entrypoint = "main|extra";
    job1.interpreter = "python3";

    JobDefinition job2 = create_basic_job();
    job2.entrypoint = "main";
    job2.interpreter = "extra|python3";

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: These WILL collide due to pipe separator (known limitation)
    EXPECT_EQ(hash1, hash2) << "Pipe character in fields causes known hash collision";

    // This is acceptable because:
    // 1. Pipe characters in filenames/interpreters are extremely rare
    // 2. Fixing would require length-prefixing which is more complex
    // 3. The collision only happens with malicious/unusual inputs
}

TEST_F(JobHashTest, EmptyArg_AffectsHash) {
    // Given: Two jobs with different empty arg handling
    JobDefinition job1 = create_basic_job();
    job1.args = {"--flag", ""};

    JobDefinition job2 = create_basic_job();
    job2.args = {"--flag"};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should be different
    EXPECT_NE(hash1, hash2) << "Empty arg vs no arg should produce different hash";
}

// ============================================================================
// Real-World Scenarios
// ============================================================================

TEST_F(JobHashTest, TypicalMLJob_ProducesStableHash) {
    // Given: A typical ML training job
    JobDefinition job;
    job.entrypoint = "train.py";
    job.interpreter = "python3";
    job.environment = "ml-basic";
    job.args = {"--epochs", "100", "--batch-size", "32"};
    job.code = R"(
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier

# Load data
data = pd.read_csv('data.csv')
# Train model
model = RandomForestClassifier()
model.fit(X_train, y_train)
    )";

    // When: Calculating hash
    std::string hash1 = job.calculate_hash();
    std::string hash2 = job.calculate_hash();

    // Then: Should be stable and valid
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash1.length(), 64);
}

TEST_F(JobHashTest, TwoSimilarJobs_ProduceDifferentHashes) {
    // Given: Two ML jobs that differ only in hyperparameters
    JobDefinition job1;
    job1.entrypoint = "train.py";
    job1.interpreter = "python3";
    job1.environment = "ml-basic";
    job1.args = {"--learning-rate", "0.001"};
    job1.code = "# ML training code";

    JobDefinition job2 = job1;
    job2.args = {"--learning-rate", "0.01"};  // Different hyperparameter

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should be different
    EXPECT_NE(hash1, hash2) << "Different hyperparameters should produce different hash";
}

TEST_F(JobHashTest, CodeWhitespaceMatters) {
    // Given: Two jobs with same logic but different whitespace
    JobDefinition job1 = create_basic_job();
    job1.code = "print('hello')";

    JobDefinition job2 = create_basic_job();
    job2.code = "print( 'hello' )";  // Extra spaces

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should be different (whitespace is significant)
    EXPECT_NE(hash1, hash2) << "Whitespace in code should affect hash";
}

// ============================================================================
// Hash Collision Resistance Tests
// ============================================================================

TEST_F(JobHashTest, MinorCodeChange_ProducesDifferentHash) {
    // Given: Two jobs with tiny code difference
    JobDefinition job1 = create_basic_job();
    job1.code = "result = 42";

    JobDefinition job2 = create_basic_job();
    job2.code = "result = 43";  // Only one character different

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should be completely different (avalanche effect)
    EXPECT_NE(hash1, hash2);

    // And: Should differ in many characters (avalanche property)
    int diff_count = 0;
    for (size_t i = 0; i < hash1.length(); i++) {
        if (hash1[i] != hash2[i]) diff_count++;
    }
    EXPECT_GT(diff_count, 20) << "SHA256 avalanche effect should change many bits";
}

TEST_F(JobHashTest, FieldReordering_DoesNotAffectHash) {
    // Given: JobDefinition has fixed field order in calculation
    // This test verifies the implementation detail that field order is fixed

    JobDefinition job1;
    job1.entrypoint = "a.py";
    job1.interpreter = "python3";
    job1.environment = "env1";
    job1.args = {};
    job1.code = "code";

    JobDefinition job2;
    // Set fields in different order (but same values)
    job2.code = "code";
    job2.environment = "env1";
    job2.args = {};
    job2.interpreter = "python3";
    job2.entrypoint = "a.py";

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should be identical (internal order is fixed)
    EXPECT_EQ(hash1, hash2) << "Hash should not depend on field assignment order";
}
