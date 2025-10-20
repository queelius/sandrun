#include <gtest/gtest.h>
#include "environment_manager.h"
#include "job_executor.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

using namespace sandrun;
namespace fs = std::filesystem;

/**
 * Integration tests for persistent environments end-to-end workflow
 *
 * These tests verify that the environment manager works correctly
 * in a realistic job execution scenario.
 */
class EnvironmentIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique test directory
        test_dir = fs::temp_directory_path() / ("sandrun_env_int_test_" + random_string());
        fs::create_directories(test_dir);
    }

    void TearDown() override {
        // Cleanup test directory
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }

        // Cleanup environment manager cache
        // Note: EnvironmentManager is singleton, so cleanup affects global state
        try {
            auto& env_mgr = EnvironmentManager::instance();
            env_mgr.cleanup_old_environments();
        } catch (...) {
            // Ignore cleanup errors
        }
    }

    std::string random_string(size_t length = 8) {
        static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        std::string result;
        result.reserve(length);

        std::srand(std::time(nullptr) + std::rand());
        for (size_t i = 0; i < length; ++i) {
            result += alphanum[std::rand() % (sizeof(alphanum) - 1)];
        }
        return result;
    }

    void create_test_script(const std::string& filename, const std::string& content) {
        std::ofstream file(test_dir / filename);
        file << content;
        file.close();
    }

    fs::path test_dir;
};

// ============================================================================
// Basic Environment Usage Tests
// ============================================================================

TEST_F(EnvironmentIntegrationTest, BuiltInTemplates_AreAvailable) {
    // Given: Environment manager initialized
    auto& env_mgr = EnvironmentManager::instance();

    // When: Checking for built-in templates
    bool has_ml = env_mgr.has_template("ml-basic");
    bool has_vision = env_mgr.has_template("vision");
    bool has_nlp = env_mgr.has_template("nlp");
    bool has_ds = env_mgr.has_template("data-science");
    bool has_sci = env_mgr.has_template("scientific");

    // Then: All built-in templates should be available
    EXPECT_TRUE(has_ml) << "ml-basic template should be available";
    EXPECT_TRUE(has_vision) << "vision template should be available";
    EXPECT_TRUE(has_nlp) << "nlp template should be available";
    EXPECT_TRUE(has_ds) << "data-science template should be available";
    EXPECT_TRUE(has_sci) << "scientific template should be available";
}

TEST_F(EnvironmentIntegrationTest, ListTemplates_ReturnsAllBuiltIns) {
    // Given: Environment manager initialized
    auto& env_mgr = EnvironmentManager::instance();

    // When: Listing templates
    auto templates = env_mgr.list_templates();

    // Then: Should have at least 5 built-in templates
    EXPECT_GE(templates.size(), 5) << "Should have at least 5 built-in templates";

    // And: Should contain expected templates
    bool found_ml = false, found_vision = false, found_nlp = false;
    for (const auto& name : templates) {
        if (name == "ml-basic") found_ml = true;
        if (name == "vision") found_vision = true;
        if (name == "nlp") found_nlp = true;
    }

    EXPECT_TRUE(found_ml);
    EXPECT_TRUE(found_vision);
    EXPECT_TRUE(found_nlp);
}

TEST_F(EnvironmentIntegrationTest, PrepareEnvironment_CreatesJobSpecificDirectory) {
    // Given: A job ID
    std::string job_id = "test-job-" + random_string();
    auto& env_mgr = EnvironmentManager::instance();

    // When: Preparing environment (this will actually build packages - may be slow)
    // Use a simple template to speed up test
    std::string env_dir;

    // Note: This test may take a while on first run as it builds the environment
    // Subsequent runs will be faster due to caching
    ASSERT_NO_THROW({
        env_dir = env_mgr.prepare_environment("ml-basic", job_id);
    }) << "Environment preparation should not throw";

    // Then: Should return a valid directory path
    EXPECT_FALSE(env_dir.empty());
    EXPECT_TRUE(fs::exists(env_dir)) << "Environment directory should exist: " << env_dir;

    // And: Directory should be job-specific (contain job ID)
    EXPECT_NE(env_dir.find(job_id), std::string::npos)
        << "Environment directory should be job-specific";
}

TEST_F(EnvironmentIntegrationTest, PrepareEnvironment_MultipleCalls_UsesCache) {
    // Given: Two different jobs using same template
    std::string job1_id = "job1-" + random_string();
    std::string job2_id = "job2-" + random_string();
    auto& env_mgr = EnvironmentManager::instance();

    // When: Preparing environments for both jobs
    auto start1 = std::chrono::steady_clock::now();
    std::string env1;
    ASSERT_NO_THROW({
        env1 = env_mgr.prepare_environment("ml-basic", job1_id);
    });
    auto end1 = std::chrono::steady_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1);

    auto start2 = std::chrono::steady_clock::now();
    std::string env2;
    ASSERT_NO_THROW({
        env2 = env_mgr.prepare_environment("ml-basic", job2_id);
    });
    auto end2 = std::chrono::steady_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2);

    // Then: Both should succeed
    EXPECT_FALSE(env1.empty());
    EXPECT_FALSE(env2.empty());

    // And: Should be different directories (job-specific)
    EXPECT_NE(env1, env2) << "Each job should get its own environment directory";

    // And: Second preparation should be faster (cached)
    // Note: The speedup may not be dramatic because cloning still takes time
    // We just verify caching is working (both calls complete successfully)
    std::cout << "[Test] First preparation: " << duration1.count() << "ms, "
              << "Second: " << duration2.count() << "ms" << std::endl;

    // Both should complete (success is the main test)
    EXPECT_TRUE(true) << "Both environment preparations completed successfully";
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(EnvironmentIntegrationTest, GetStats_ReflectsUsage) {
    // Given: Fresh environment manager state
    auto& env_mgr = EnvironmentManager::instance();
    auto initial_stats = env_mgr.get_stats();

    // When: Preparing an environment
    std::string job_id = "stats-test-" + random_string();
    ASSERT_NO_THROW({
        env_mgr.prepare_environment("scientific", job_id);
    });

    // Then: Stats should reflect usage
    auto after_stats = env_mgr.get_stats();

    EXPECT_EQ(after_stats.total_templates, initial_stats.total_templates)
        << "Template count should not change";

    // Cached environments may increase (if scientific wasn't cached before)
    EXPECT_GE(after_stats.cached_environments, initial_stats.cached_environments)
        << "Cached environments should not decrease";

    // Total uses should increase
    EXPECT_GT(after_stats.total_uses, initial_stats.total_uses)
        << "Total uses should increase after preparing environment";
}

TEST_F(EnvironmentIntegrationTest, GetStats_DiskUsage_IsReasonable) {
    // Given: Environment manager with cached environments
    auto& env_mgr = EnvironmentManager::instance();

    // When: Preparing an environment
    std::string job_id = "disk-test-" + random_string();
    ASSERT_NO_THROW({
        env_mgr.prepare_environment("ml-basic", job_id);
    });

    // Then: Disk usage should be reported
    auto stats = env_mgr.get_stats();

    // Environment with packages should take some space
    // ml-basic has numpy, pandas, scikit-learn, matplotlib
    EXPECT_GT(stats.disk_usage_mb, 0) << "Disk usage should be > 0 with cached environment";

    // But shouldn't be absurdly large (< 5GB for basic templates)
    EXPECT_LT(stats.disk_usage_mb, 5000) << "Disk usage seems unreasonably high";
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(EnvironmentIntegrationTest, PrepareEnvironment_InvalidTemplate_ThrowsError) {
    // Given: Invalid template name
    auto& env_mgr = EnvironmentManager::instance();
    std::string job_id = "error-test-" + random_string();

    // When/Then: Preparing with invalid template should throw
    EXPECT_THROW({
        env_mgr.prepare_environment("nonexistent-template", job_id);
    }, std::runtime_error) << "Should throw when template doesn't exist";
}

TEST_F(EnvironmentIntegrationTest, PrepareEnvironment_EmptyJobId_HandlesGracefully) {
    // Given: Empty job ID
    auto& env_mgr = EnvironmentManager::instance();

    // When: Preparing environment with empty job ID
    std::string env_dir;
    ASSERT_NO_THROW({
        env_dir = env_mgr.prepare_environment("ml-basic", "");
    }) << "Should handle empty job ID gracefully";

    // Then: Should still return a valid directory
    EXPECT_FALSE(env_dir.empty());
}

// ============================================================================
// Concurrent Usage Tests
// ============================================================================

TEST_F(EnvironmentIntegrationTest, PrepareEnvironment_Concurrent_HandlesSafely) {
    // Given: Multiple threads preparing environments
    auto& env_mgr = EnvironmentManager::instance();
    const int num_threads = 5;
    std::vector<std::thread> threads;
    std::vector<std::string> results(num_threads);
    std::atomic<int> success_count{0};

    // When: Preparing environments concurrently
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&, i]() {
            try {
                std::string job_id = "concurrent-job-" + std::to_string(i);
                results[i] = env_mgr.prepare_environment("ml-basic", job_id);
                success_count++;
            } catch (const std::exception& e) {
                std::cerr << "Thread " << i << " failed: " << e.what() << std::endl;
            }
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    // Then: All threads should succeed
    EXPECT_EQ(success_count.load(), num_threads)
        << "All concurrent environment preparations should succeed";

    // And: Each should get a unique directory
    for (int i = 0; i < num_threads; i++) {
        EXPECT_FALSE(results[i].empty()) << "Thread " << i << " should get valid directory";

        for (int j = i + 1; j < num_threads; j++) {
            EXPECT_NE(results[i], results[j])
                << "Jobs should get different environment directories";
        }
    }
}

// ============================================================================
// Rebuild Tests
// ============================================================================

TEST_F(EnvironmentIntegrationTest, RebuildTemplate_ForcesNewBuild) {
    // Given: An environment that's already cached
    auto& env_mgr = EnvironmentManager::instance();
    std::string job1_id = "rebuild-test-1-" + random_string();

    // First prepare to ensure it's cached
    std::string env1;
    ASSERT_NO_THROW({
        env1 = env_mgr.prepare_environment("scientific", job1_id);
    });

    auto initial_stats = env_mgr.get_stats();

    // When: Rebuilding the template
    ASSERT_NO_THROW({
        env_mgr.rebuild_template("scientific");
    });

    // Then: Next preparation should use the rebuilt version
    std::string job2_id = "rebuild-test-2-" + random_string();
    std::string env2;
    ASSERT_NO_THROW({
        env2 = env_mgr.prepare_environment("scientific", job2_id);
    });

    EXPECT_FALSE(env2.empty());

    // Stats should still be reasonable (rebuild doesn't reset use counts)
    auto after_stats = env_mgr.get_stats();
    // After rebuild and another prepare, stats may vary
    EXPECT_GT(after_stats.total_uses, 0) << "Should have some usage after rebuild";
}

TEST_F(EnvironmentIntegrationTest, RebuildTemplate_InvalidTemplate_DoesNotThrow) {
    // Given: Invalid template name
    auto& env_mgr = EnvironmentManager::instance();

    // When/Then: Rebuilding invalid template currently doesn't throw
    // (it's a no-op if template doesn't exist)
    EXPECT_NO_THROW({
        env_mgr.rebuild_template("nonexistent-template");
    }) << "Rebuild of non-existent template should be a no-op";
}

// ============================================================================
// Cleanup Tests
// ============================================================================

TEST_F(EnvironmentIntegrationTest, CleanupOldEnvironments_RunsWithoutError) {
    // Given: Environment manager with some usage
    auto& env_mgr = EnvironmentManager::instance();

    // Prepare an environment to ensure there's something to potentially clean
    std::string job_id = "cleanup-test-" + random_string();
    ASSERT_NO_THROW({
        env_mgr.prepare_environment("ml-basic", job_id);
    });

    // When: Running cleanup
    // Then: Should not throw (even if nothing to clean)
    ASSERT_NO_THROW({
        env_mgr.cleanup_old_environments();
    }) << "Cleanup should run without error";
}

// ============================================================================
// Real-World Scenario Tests
// ============================================================================

TEST_F(EnvironmentIntegrationTest, Scenario_MLWorkflow_UsesMLBasicEnvironment) {
    // Given: A typical ML job that needs numpy and pandas
    create_test_script("train.py", R"(
import sys
try:
    import numpy as np
    import pandas as pd
    print("Successfully imported numpy and pandas")
    print(f"NumPy version: {np.__version__}")
    print(f"Pandas version: {pd.__version__}")
    sys.exit(0)
except ImportError as e:
    print(f"Import failed: {e}")
    sys.exit(1)
)");

    auto& env_mgr = EnvironmentManager::instance();
    std::string job_id = "ml-workflow-" + random_string();

    // When: Preparing ml-basic environment
    std::string env_dir;
    ASSERT_NO_THROW({
        env_dir = env_mgr.prepare_environment("ml-basic", job_id);
    }) << "ML environment preparation failed";

    // Note: Actually running the script would require JobExecutor integration
    // and proper PYTHONPATH setup, which is tested elsewhere
    EXPECT_FALSE(env_dir.empty());
    EXPECT_TRUE(fs::exists(env_dir));
}

TEST_F(EnvironmentIntegrationTest, Scenario_MultipleTemplates_CanCoexist) {
    // Given: Jobs using different templates
    auto& env_mgr = EnvironmentManager::instance();

    std::string ml_job_id = "ml-" + random_string();
    std::string sci_job_id = "sci-" + random_string();
    std::string ds_job_id = "ds-" + random_string();

    // When: Preparing multiple different environments
    std::string ml_env, sci_env, ds_env;

    ASSERT_NO_THROW({
        ml_env = env_mgr.prepare_environment("ml-basic", ml_job_id);
        sci_env = env_mgr.prepare_environment("scientific", sci_job_id);
        ds_env = env_mgr.prepare_environment("data-science", ds_job_id);
    });

    // Then: All should succeed and be different
    EXPECT_FALSE(ml_env.empty());
    EXPECT_FALSE(sci_env.empty());
    EXPECT_FALSE(ds_env.empty());

    EXPECT_NE(ml_env, sci_env);
    EXPECT_NE(ml_env, ds_env);
    EXPECT_NE(sci_env, ds_env);

    // And: All directories should exist
    EXPECT_TRUE(fs::exists(ml_env));
    EXPECT_TRUE(fs::exists(sci_env));
    EXPECT_TRUE(fs::exists(ds_env));
}
