#include <gtest/gtest.h>
#include "file_utils.h"
#include "job_executor.h"
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

    // Helper to calculate job hash (mimics main.cpp logic)
    std::string calculate_job_hash(
        const std::string& entrypoint,
        const std::string& interpreter,
        const std::string& environment,
        const std::vector<std::string>& args,
        const std::string& entrypoint_content
    ) {
        std::ostringstream job_data;
        job_data << entrypoint << "|"
                 << interpreter << "|"
                 << environment << "|";
        for (const auto& arg : args) {
            job_data << arg << "|";
        }
        job_data << entrypoint_content;
        return FileUtils::sha256_string(job_data.str());
    }

    std::filesystem::path test_dir;
};

// ============================================================================
// Job Hash Calculation Tests
// ============================================================================

TEST_F(JobVerificationTest, JobHash_BasicCalculation) {
    // Given: A simple job with entrypoint
    // When: Calculating job hash
    // Then: Should produce consistent hash

    std::string entrypoint = "main.py";
    std::string interpreter = "python3";
    std::string environment = "";
    std::vector<std::string> args;
    std::string code = "print('hello')";

    std::string hash1 = calculate_job_hash(entrypoint, interpreter, environment, args, code);
    std::string hash2 = calculate_job_hash(entrypoint, interpreter, environment, args, code);

    EXPECT_EQ(hash1, hash2) << "Job hash should be deterministic";
    EXPECT_EQ(hash1.length(), 64) << "Should be valid SHA256 hash";
}

TEST_F(JobVerificationTest, JobHash_IdenticalJobsSameHash) {
    // Given: Two identical jobs
    // When: Calculating hashes for both
    // Then: Should produce identical hashes

    std::string code = "print('test')";
    std::vector<std::string> args;

    std::string hash1 = calculate_job_hash("main.py", "python3", "", args, code);
    std::string hash2 = calculate_job_hash("main.py", "python3", "", args, code);

    EXPECT_EQ(hash1, hash2) << "Identical jobs should have identical hashes";
}

TEST_F(JobVerificationTest, JobHash_DifferentCodeDifferentHash) {
    // Given: Jobs with different code
    // When: Calculating hashes
    // Then: Should produce different hashes

    std::vector<std::string> args;

    std::string hash1 = calculate_job_hash("main.py", "python3", "", args, "print('test1')");
    std::string hash2 = calculate_job_hash("main.py", "python3", "", args, "print('test2')");

    EXPECT_NE(hash1, hash2) << "Different code should produce different hashes";
}

TEST_F(JobVerificationTest, JobHash_DifferentArgsDifferentHash) {
    // Given: Same code but different arguments
    // When: Calculating hashes
    // Then: Should produce different hashes

    std::string code = "import sys; print(sys.argv)";

    std::vector<std::string> args1 = {"--input", "data1.csv"};
    std::vector<std::string> args2 = {"--input", "data2.csv"};
    std::vector<std::string> args3;

    std::string hash1 = calculate_job_hash("main.py", "python3", "", args1, code);
    std::string hash2 = calculate_job_hash("main.py", "python3", "", args2, code);
    std::string hash3 = calculate_job_hash("main.py", "python3", "", args3, code);

    EXPECT_NE(hash1, hash2) << "Different args should produce different hashes";
    EXPECT_NE(hash1, hash3) << "Args vs no args should produce different hashes";
    EXPECT_NE(hash2, hash3);
}

TEST_F(JobVerificationTest, JobHash_DifferentInterpreterDifferentHash) {
    // Given: Same code but different interpreters
    // When: Calculating hashes
    // Then: Should produce different hashes

    std::string code = "console.log('test')";
    std::vector<std::string> args;

    std::string hash_node = calculate_job_hash("main.js", "node", "", args, code);
    std::string hash_python = calculate_job_hash("main.js", "python3", "", args, code);

    EXPECT_NE(hash_node, hash_python)
        << "Different interpreters should produce different hashes";
}

TEST_F(JobVerificationTest, JobHash_DifferentEnvironmentDifferentHash) {
    // Given: Same code but different environments
    // When: Calculating hashes
    // Then: Should produce different hashes

    std::string code = "import torch";
    std::vector<std::string> args;

    std::string hash1 = calculate_job_hash("main.py", "python3", "", args, code);
    std::string hash2 = calculate_job_hash("main.py", "python3", "pytorch", args, code);

    EXPECT_NE(hash1, hash2)
        << "Different environments should produce different hashes";
}

TEST_F(JobVerificationTest, JobHash_DifferentEntrypointDifferentHash) {
    // Given: Different entrypoint names
    // When: Calculating hashes
    // Then: Should produce different hashes

    std::string code = "print('test')";
    std::vector<std::string> args;

    std::string hash1 = calculate_job_hash("main.py", "python3", "", args, code);
    std::string hash2 = calculate_job_hash("script.py", "python3", "", args, code);

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
    std::string job_hash = calculate_job_hash(
        "main.py",
        "python3",
        "",
        std::vector<std::string>(),
        script
    );

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
    std::string job_hash1 = calculate_job_hash("main.py", "python3", "", {}, script);
    auto result1 = JobExecutor::execute(test_dir.string(), "python3", "main.py", {}, "");

    EXPECT_EQ(result1.exit_code, 0);

    // Clean outputs for second run
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir);
    create_file("main.py", script);

    // Second execution
    std::string job_hash2 = calculate_job_hash("main.py", "python3", "", {}, script);
    auto result2 = JobExecutor::execute(test_dir.string(), "python3", "main.py", {}, "");

    EXPECT_EQ(result2.exit_code, 0);

    // Compare
    EXPECT_EQ(job_hash1, job_hash2) << "Same job should produce same hash";
    EXPECT_EQ(result1.stdout_log, result2.stdout_log) << "Output should be identical";
}

TEST_F(JobVerificationTest, Verification_DetectCodeTampering) {
    // Given: Two jobs with slightly different code
    // When: Calculating job hashes
    // Then: Should detect the difference

    std::string code_original = "result = 2 + 2\nprint(result)";
    std::string code_tampered = "result = 2 + 3\nprint(result)";  // Changed calculation

    std::string hash_original = calculate_job_hash("main.py", "python3", "", {}, code_original);
    std::string hash_tampered = calculate_job_hash("main.py", "python3", "", {}, code_tampered);

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

    std::string job_hash = calculate_job_hash("main.py", "python3", "", {}, script);
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

} // namespace
} // namespace sandrun
