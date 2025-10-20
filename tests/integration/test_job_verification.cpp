#include <gtest/gtest.h>
#include "file_utils.h"
#include "job_executor.h"
#include "job_hash.h"
#include <fstream>
#include <filesystem>
#include <sstream>

namespace sandrun {
namespace {

class JobVerificationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir = std::filesystem::temp_directory_path() / "job_verification_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // Cleanup test directory
        std::filesystem::remove_all(test_dir);
    }

    // Helper to create a test file
    std::string create_file(const std::string& filename, const std::string& content) {
        std::string filepath = test_dir / filename;
        std::ofstream file(filepath);
        file << content;
        file.close();
        return filepath;
    }

    std::filesystem::path test_dir;
};

// ============================================================================
// Job Hash Calculation Tests
// ============================================================================

TEST_F(JobVerificationTest, JobHash_BasicCalculation) {
    // Given: A simple job with entrypoint
    JobDefinition job{"main.py", "python3", "", {}, "print('hello')"};

    // When: Calculating job hash
    std::string hash1 = job.calculate_hash();
    std::string hash2 = job.calculate_hash();

    // Then: Should produce consistent hash
    EXPECT_EQ(hash1, hash2) << "Job hash should be deterministic";
    EXPECT_EQ(hash1.length(), 64) << "Should be valid SHA256 hash";
}

TEST_F(JobVerificationTest, JobHash_IdenticalJobsSameHash) {
    // Given: Two identical jobs
    JobDefinition job1{"main.py", "python3", "", {}, "print('test')"};
    JobDefinition job2{"main.py", "python3", "", {}, "print('test')"};

    // When: Calculating hashes for both
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should produce identical hashes
    EXPECT_EQ(hash1, hash2) << "Identical jobs should have identical hashes";
}

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

TEST_F(JobVerificationTest, JobHash_DifferentArgsDifferentHash) {
    // Given: Same code but different arguments
    std::string code = "import sys; print(sys.argv)";
    JobDefinition job1{"main.py", "python3", "", {"--input", "data1.csv"}, code};
    JobDefinition job2{"main.py", "python3", "", {"--input", "data2.csv"}, code};
    JobDefinition job3{"main.py", "python3", "", {}, code};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();
    std::string hash3 = job3.calculate_hash();

    // Then: Should produce different hashes
    EXPECT_NE(hash1, hash2) << "Different args should produce different hashes";
    EXPECT_NE(hash1, hash3) << "Args vs no args should produce different hashes";
    EXPECT_NE(hash2, hash3);
}

TEST_F(JobVerificationTest, JobHash_DifferentInterpreterDifferentHash) {
    // Given: Same code but different interpreters
    std::string code = "console.log('test')";
    JobDefinition job_node{"main.js", "node", "", {}, code};
    JobDefinition job_python{"main.js", "python3", "", {}, code};

    // When: Calculating hashes
    std::string hash_node = job_node.calculate_hash();
    std::string hash_python = job_python.calculate_hash();

    // Then: Should produce different hashes
    EXPECT_NE(hash_node, hash_python)
        << "Different interpreters should produce different hashes";
}

TEST_F(JobVerificationTest, JobHash_DifferentEnvironmentDifferentHash) {
    // Given: Same code but different environments
    std::string code = "import torch";
    JobDefinition job1{"main.py", "python3", "", {}, code};
    JobDefinition job2{"main.py", "python3", "pytorch", {}, code};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should produce different hashes
    EXPECT_NE(hash1, hash2)
        << "Different environments should produce different hashes";
}

TEST_F(JobVerificationTest, JobHash_DifferentEntrypointDifferentHash) {
    // Given: Different entrypoint names
    std::string code = "print('test')";
    JobDefinition job1{"main.py", "python3", "", {}, code};
    JobDefinition job2{"script.py", "python3", "", {}, code};

    // When: Calculating hashes
    std::string hash1 = job1.calculate_hash();
    std::string hash2 = job2.calculate_hash();

    // Then: Should produce different hashes
    EXPECT_NE(hash1, hash2)
        << "Different entrypoints should produce different hashes";
}

// ============================================================================
// Output File Hashing Tests
// ============================================================================

TEST_F(JobVerificationTest, OutputHashing_AllFiles) {
    // Given: A job that produces multiple output files
    // When: Hashing all output files
    // Then: Should hash all files with correct metadata

    create_file("output1.txt", "result 1");
    create_file("output2.csv", "a,b,c");
    create_file("plot.png", "fake image data");

    auto output_files = FileUtils::hash_directory(test_dir.string());

    EXPECT_EQ(output_files.size(), 3) << "Should hash all output files";
    EXPECT_TRUE(output_files.count("output1.txt") > 0);
    EXPECT_TRUE(output_files.count("output2.csv") > 0);
    EXPECT_TRUE(output_files.count("plot.png") > 0);

    // Verify metadata is correct
    EXPECT_EQ(output_files["output1.txt"].size_bytes, 8);
    EXPECT_EQ(output_files["output1.txt"].type, FileType::TEXT);
    EXPECT_EQ(output_files["output2.csv"].type, FileType::DATA);
    EXPECT_EQ(output_files["plot.png"].type, FileType::IMAGE);
}

TEST_F(JobVerificationTest, OutputHashing_WithGlobPatterns) {
    // Given: Output files with specific patterns to hash
    // When: Using glob patterns like in job manifest
    // Then: Should only hash matching files

    create_file("result.txt", "text");
    create_file("plot1.png", "image1");
    create_file("plot2.png", "image2");
    create_file("debug.log", "debug info");

    // Simulate job.outputs = ["*.png", "result.txt"]
    std::vector<std::string> patterns = {"*.png", "result.txt"};
    auto output_files = FileUtils::hash_directory(test_dir.string(), patterns);

    EXPECT_EQ(output_files.size(), 3) << "Should only hash matching files";
    EXPECT_TRUE(output_files.count("result.txt") > 0);
    EXPECT_TRUE(output_files.count("plot1.png") > 0);
    EXPECT_TRUE(output_files.count("plot2.png") > 0);
    EXPECT_FALSE(output_files.count("debug.log") > 0) << "debug.log should not be hashed";
}

TEST_F(JobVerificationTest, OutputHashing_EmptyOutput) {
    // Given: A job that produces no output files
    // When: Hashing output directory
    // Then: Should return empty map

    auto output_files = FileUtils::hash_directory(test_dir.string());

    EXPECT_TRUE(output_files.empty()) << "No output files should result in empty map";
}

TEST_F(JobVerificationTest, OutputHashing_VerifyHashesMatchContent) {
    // Given: Output files with known content
    // When: Hashing the outputs
    // Then: Hashes should match expected values

    std::string content1 = "output data 1";
    std::string content2 = "output data 2";

    create_file("out1.txt", content1);
    create_file("out2.txt", content2);

    auto output_files = FileUtils::hash_directory(test_dir.string());

    std::string expected_hash1 = FileUtils::sha256_string(content1);
    std::string expected_hash2 = FileUtils::sha256_string(content2);

    EXPECT_EQ(output_files["out1.txt"].sha256_hash, expected_hash1);
    EXPECT_EQ(output_files["out2.txt"].sha256_hash, expected_hash2);
}

// ============================================================================
// End-to-End Job Execution with Verification
// ============================================================================

TEST_F(JobVerificationTest, EndToEnd_JobExecutionWithHashing) {
    // Given: A complete job that produces output
    // When: Executing the job and hashing outputs
    // Then: Should have valid job hash and output hashes

    // Create a simple Python script that generates output
    std::string script = R"(
# Simple job that creates output files
with open('result.txt', 'w') as f:
    f.write('computation result')

with open('data.csv', 'w') as f:
    f.write('a,b,c\n1,2,3\n')

print('Job completed')
)";

    create_file("main.py", script);

    // Calculate job hash
    JobDefinition job{"main.py", "python3", "", {}, script};
    std::string job_hash = job.calculate_hash();

    EXPECT_EQ(job_hash.length(), 64) << "Should have valid job hash";

    // Execute the job
    auto result = JobExecutor::execute(
        test_dir.string(),
        "python3",
        "main.py",
        std::vector<std::string>(),
        ""
    );

    EXPECT_EQ(result.exit_code, 0) << "Job should execute successfully";
    EXPECT_TRUE(result.stdout_log.find("Job completed") != std::string::npos);

    // Hash output files
    auto output_files = FileUtils::hash_directory(test_dir.string(), {"*.txt", "*.csv"});

    EXPECT_EQ(output_files.size(), 2) << "Should have hashed 2 output files";
    EXPECT_TRUE(output_files.count("result.txt") > 0);
    EXPECT_TRUE(output_files.count("data.csv") > 0);

    // Verify output hashes match content
    EXPECT_EQ(output_files["result.txt"].sha256_hash,
              FileUtils::sha256_string("computation result"));
    EXPECT_EQ(output_files["data.csv"].sha256_hash,
              FileUtils::sha256_string("a,b,c\n1,2,3\n"));
}

TEST_F(JobVerificationTest, EndToEnd_FailedJobStillHashesOutputs) {
    // Given: A job that fails but produces partial output
    // When: Job executes with error
    // Then: Should still hash any outputs produced

    std::string script = R"(
with open('partial.txt', 'w') as f:
    f.write('partial output')

raise ValueError('Intentional error')
)";

    create_file("main.py", script);

    auto result = JobExecutor::execute(
        test_dir.string(),
        "python3",
        "main.py",
        std::vector<std::string>(),
        ""
    );

    EXPECT_NE(result.exit_code, 0) << "Job should fail";

    // Still hash outputs
    auto output_files = FileUtils::hash_directory(test_dir.string(), {"*.txt"});

    EXPECT_EQ(output_files.size(), 1) << "Should hash partial output";
    EXPECT_TRUE(output_files.count("partial.txt") > 0);
}

TEST_F(JobVerificationTest, EndToEnd_JobWithSubdirectoryOutputs) {
    // Given: A job that creates outputs in subdirectories
    // When: Hashing outputs
    // Then: Should recursively hash all outputs

    std::string script = R"(
import os
os.makedirs('results', exist_ok=True)
os.makedirs('results/plots', exist_ok=True)

with open('results/summary.txt', 'w') as f:
    f.write('summary')

with open('results/plots/plot1.png', 'w') as f:
    f.write('fake png')
)";

    create_file("main.py", script);

    auto result = JobExecutor::execute(
        test_dir.string(),
        "python3",
        "main.py",
        std::vector<std::string>(),
        ""
    );

    EXPECT_EQ(result.exit_code, 0);

    // Hash all outputs
    auto output_files = FileUtils::hash_directory(test_dir.string());

    EXPECT_GE(output_files.size(), 2) << "Should hash files in subdirectories";
    EXPECT_TRUE(output_files.count("results/summary.txt") > 0);
    EXPECT_TRUE(output_files.count("results/plots/plot1.png") > 0);
}

// ============================================================================
// Verification Scenario Tests
// ============================================================================

TEST_F(JobVerificationTest, Verification_ReproducibleComputation) {
    // Given: The same job executed twice
    // When: Comparing job hashes and output hashes
    // Then: Should be identical (reproducibility)

    std::string script = "print('deterministic output')";
    create_file("main.py", script);

    // First execution
    JobDefinition job{"main.py", "python3", "", {}, script};
    std::string job_hash1 = job.calculate_hash();
    auto result1 = JobExecutor::execute(test_dir.string(), "python3", "main.py", {}, "");

    EXPECT_EQ(result1.exit_code, 0);

    // Clean outputs for second run
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir);
    create_file("main.py", script);

    // Second execution
    std::string job_hash2 = job.calculate_hash();
    auto result2 = JobExecutor::execute(test_dir.string(), "python3", "main.py", {}, "");

    EXPECT_EQ(result2.exit_code, 0);

    // Compare
    EXPECT_EQ(job_hash1, job_hash2) << "Same job should produce same hash";
    EXPECT_EQ(result1.stdout_log, result2.stdout_log) << "Output should be identical";
}

TEST_F(JobVerificationTest, Verification_DetectCodeTampering) {
    // Given: Two jobs with slightly different code
    std::string code_original = "result = 2 + 2\nprint(result)";
    std::string code_tampered = "result = 2 + 3\nprint(result)";  // Changed calculation
    JobDefinition job_original{"main.py", "python3", "", {}, code_original};
    JobDefinition job_tampered{"main.py", "python3", "", {}, code_tampered};

    // When: Calculating job hashes
    std::string hash_original = job_original.calculate_hash();
    std::string hash_tampered = job_tampered.calculate_hash();

    // Then: Should detect the difference
    EXPECT_NE(hash_original, hash_tampered)
        << "Code tampering should be detected via different hash";
}

TEST_F(JobVerificationTest, Verification_OutputIntegrity) {
    // Given: A job that produces output
    // When: Verifying output integrity via hash
    // Then: Hash should match actual file content

    std::string script = R"(
with open('output.txt', 'w') as f:
    f.write('verified output')
)";

    create_file("main.py", script);

    auto result = JobExecutor::execute(test_dir.string(), "python3", "main.py", {}, "");
    EXPECT_EQ(result.exit_code, 0);

    auto output_files = FileUtils::hash_directory(test_dir.string(), {"*.txt"});

    // Verify hash matches
    std::string expected_hash = FileUtils::sha256_string("verified output");
    EXPECT_EQ(output_files["output.txt"].sha256_hash, expected_hash)
        << "Output hash should verify integrity";

    // Simulate tampering - modify file after execution
    std::ofstream tampered_file(test_dir / "output.txt");
    tampered_file << "tampered output";
    tampered_file.close();

    // Re-hash
    auto tampered_hashes = FileUtils::hash_directory(test_dir.string(), {"*.txt"});
    EXPECT_NE(tampered_hashes["output.txt"].sha256_hash, expected_hash)
        << "Tampered output should have different hash";
}

// ============================================================================
// JSON Output Format Tests (Simulated)
// ============================================================================

TEST_F(JobVerificationTest, JSONOutput_HasAllRequiredFields) {
    // Given: A completed job with outputs
    // When: Building JSON status response (simulated)
    // Then: Should include job_hash, output_files, execution_metadata

    std::string script = "print('test')";
    create_file("result.txt", "output");

    JobDefinition job{"main.py", "python3", "", {}, script};
    std::string job_hash = job.calculate_hash();
    auto output_files = FileUtils::hash_directory(test_dir.string(), {"*.txt"});

    // Verify we have the data needed for JSON response
    EXPECT_FALSE(job_hash.empty()) << "Should have job_hash";
    EXPECT_FALSE(output_files.empty()) << "Should have output_files";
    EXPECT_TRUE(output_files.count("result.txt") > 0);

    // Verify output file metadata has required fields
    auto& metadata = output_files["result.txt"];
    EXPECT_GT(metadata.size_bytes, 0) << "Should have size_bytes";
    EXPECT_FALSE(metadata.sha256_hash.empty()) << "Should have sha256 hash";
    EXPECT_NE(FileUtils::file_type_to_string(metadata.type), "")
        << "Should have type string";
}

TEST_F(JobVerificationTest, JSONOutput_MultipleOutputFiles) {
    // Given: Job with multiple output files of different types
    // When: Preparing JSON output
    // Then: All files should be included with correct metadata

    create_file("result.txt", "text result");
    create_file("data.csv", "a,b,c");
    create_file("plot.png", "image data");

    auto output_files = FileUtils::hash_directory(test_dir.string());

    EXPECT_EQ(output_files.size(), 3);

    // Each file should have complete metadata
    for (const auto& [path, metadata] : output_files) {
        EXPECT_FALSE(metadata.sha256_hash.empty()) << "File " << path << " should have hash";
        EXPECT_GT(metadata.size_bytes, 0) << "File " << path << " should have size";
        EXPECT_NE(metadata.type, FileType::OTHER) << "File " << path << " should have type";
    }
}

// ============================================================================
// Output File Ordering Tests
// ============================================================================

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

} // namespace
} // namespace sandrun
