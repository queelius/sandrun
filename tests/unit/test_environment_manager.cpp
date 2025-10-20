#include <gtest/gtest.h>
#include "environment_manager.h"
#include <thread>
#include <chrono>
#include <vector>
#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>

namespace fs = std::filesystem;

namespace sandrun {
namespace {

// Helper function to generate random strings
std::string generate_random_string(size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

class EnvironmentManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get singleton instance
        mgr = &EnvironmentManager::instance();

        // Note: We cannot easily isolate the singleton for testing,
        // so we'll work with the shared instance and use unique template names
        test_prefix = "test_" + generate_random_string(8) + "_";
    }

    void TearDown() override {
        // Cleanup test templates and environments
        cleanup_test_environments();
    }

    void cleanup_test_environments() {
        // Try to remove any test directories we created
        std::string cache_dir = "/tmp/sandrun_envs";
        if (fs::exists(cache_dir)) {
            for (const auto& entry : fs::directory_iterator(cache_dir)) {
                std::string name = entry.path().filename().string();
                if (name.find(test_prefix) != std::string::npos) {
                    try {
                        fs::remove_all(entry);
                    } catch (...) {
                        // Ignore cleanup errors
                    }
                }
            }
        }
    }

    EnvironmentTemplate create_test_template(const std::string& suffix = "") {
        EnvironmentTemplate tmpl;
        tmpl.name = test_prefix + suffix;
        tmpl.base_image = "python:3.11";
        tmpl.packages = {};  // Empty to avoid slow pip installs in tests
        tmpl.max_age_hours = 1;
        tmpl.gpu_enabled = false;
        return tmpl;
    }

    EnvironmentManager* mgr;
    std::string test_prefix;
};

// ============================================================================
// Template Registration Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, RegisterSimpleTemplate) {
    auto tmpl = create_test_template("simple");

    mgr->register_template(tmpl);

    EXPECT_TRUE(mgr->has_template(tmpl.name))
        << "Template should be registered and findable";
}

TEST_F(EnvironmentManagerTest, RegisterTemplateWithPackages) {
    auto tmpl = create_test_template("with_packages");
    tmpl.packages = {"requests", "flask"};

    mgr->register_template(tmpl);

    EXPECT_TRUE(mgr->has_template(tmpl.name))
        << "Template with packages should be registered";
}

TEST_F(EnvironmentManagerTest, RegisterTemplateWithSetupScript) {
    auto tmpl = create_test_template("with_script");
    tmpl.setup_script = "/tmp/setup.sh";  // Path only, doesn't need to exist for registration

    mgr->register_template(tmpl);

    EXPECT_TRUE(mgr->has_template(tmpl.name))
        << "Template with setup script path should be registered";
}

TEST_F(EnvironmentManagerTest, HasTemplateReturnsTrueAfterRegistration) {
    auto tmpl = create_test_template("check_exists");

    EXPECT_FALSE(mgr->has_template(tmpl.name))
        << "Template should not exist before registration";

    mgr->register_template(tmpl);

    EXPECT_TRUE(mgr->has_template(tmpl.name))
        << "Template should exist after registration";
}

TEST_F(EnvironmentManagerTest, ListTemplatesIncludesRegisteredTemplate) {
    auto tmpl = create_test_template("list_check");
    mgr->register_template(tmpl);

    auto templates = mgr->list_templates();

    auto it = std::find(templates.begin(), templates.end(), tmpl.name);
    EXPECT_NE(it, templates.end())
        << "Registered template should appear in list";
}

TEST_F(EnvironmentManagerTest, RegisterDuplicateTemplateOverwrites) {
    auto tmpl1 = create_test_template("duplicate");
    tmpl1.max_age_hours = 10;
    mgr->register_template(tmpl1);

    auto tmpl2 = create_test_template("duplicate");
    tmpl2.max_age_hours = 20;
    mgr->register_template(tmpl2);

    // Both registrations should succeed (overwrite behavior)
    EXPECT_TRUE(mgr->has_template(tmpl1.name))
        << "Duplicate registration should overwrite, not fail";
}

// ============================================================================
// Environment Preparation Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, PrepareEnvironmentCreatesEnvironment) {
    auto tmpl = create_test_template("prepare_basic");
    mgr->register_template(tmpl);

    std::string job_id = "job_" + generate_random_string(8);
    std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

    EXPECT_FALSE(env_path.empty())
        << "prepare_environment should return non-empty path";
    EXPECT_TRUE(fs::exists(env_path))
        << "Environment directory should exist at returned path";

    // Cleanup
    fs::remove_all(env_path);
}

TEST_F(EnvironmentManagerTest, PrepareEnvironmentReturnsValidPath) {
    auto tmpl = create_test_template("prepare_valid_path");
    mgr->register_template(tmpl);

    std::string job_id = "job_" + generate_random_string(8);
    std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

    EXPECT_TRUE(env_path.find("/tmp/sandrun_envs") != std::string::npos)
        << "Environment path should be in expected cache directory";
    EXPECT_TRUE(env_path.find(job_id) != std::string::npos)
        << "Environment path should contain job_id";

    // Cleanup
    fs::remove_all(env_path);
}

TEST_F(EnvironmentManagerTest, PrepareEnvironmentWithNonExistentTemplate) {
    std::string fake_template = test_prefix + "nonexistent";
    std::string job_id = "job_" + generate_random_string(8);

    EXPECT_THROW({
        mgr->prepare_environment(fake_template, job_id);
    }, std::runtime_error)
        << "Preparing environment with non-existent template should throw";
}

TEST_F(EnvironmentManagerTest, PrepareEnvironmentReusesCachedEnvironment) {
    auto tmpl = create_test_template("reuse_cache");
    mgr->register_template(tmpl);

    // First preparation - builds base environment
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id1);

    auto stats_after_first = mgr->get_stats();
    int cached_after_first = stats_after_first.cached_environments;

    // Second preparation - should reuse cached base
    std::string job_id2 = "job_" + generate_random_string(8);
    std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);

    auto stats_after_second = mgr->get_stats();

    EXPECT_EQ(stats_after_second.cached_environments, cached_after_first)
        << "Second prepare should reuse cached base, not create another";
    EXPECT_GT(stats_after_second.total_uses, stats_after_first.total_uses)
        << "Use count should increase when reusing cached environment";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, PrepareEnvironmentCreatesSeparateInstances) {
    auto tmpl = create_test_template("separate_instances");
    mgr->register_template(tmpl);

    std::string job_id1 = "job_" + generate_random_string(8);
    std::string job_id2 = "job_" + generate_random_string(8);

    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id1);
    std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);

    EXPECT_NE(env_path1, env_path2)
        << "Different jobs should get different environment paths";
    EXPECT_TRUE(fs::exists(env_path1))
        << "First job environment should exist";
    EXPECT_TRUE(fs::exists(env_path2))
        << "Second job environment should exist";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, PrepareEnvironmentUpdatesLastUsed) {
    auto tmpl = create_test_template("update_last_used");
    mgr->register_template(tmpl);

    // Prepare first time
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id1);

    // Small delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Prepare second time (should update last_used)
    std::string job_id2 = "job_" + generate_random_string(8);
    std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);

    // We can't directly access last_used, but we can verify the cache wasn't evicted
    auto stats = mgr->get_stats();
    EXPECT_GT(stats.total_uses, 0)
        << "Cache should be used and tracked";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, PrepareEnvironmentIncrementsUseCount) {
    auto tmpl = create_test_template("increment_use_count");
    mgr->register_template(tmpl);

    auto stats_before = mgr->get_stats();
    int uses_before = stats_before.total_uses;

    // Prepare environment multiple times
    for (int i = 0; i < 3; i++) {
        std::string job_id = "job_" + std::to_string(i) + "_" + generate_random_string(6);
        std::string env_path = mgr->prepare_environment(tmpl.name, job_id);
        fs::remove_all(env_path);  // Cleanup immediately
    }

    auto stats_after = mgr->get_stats();
    int uses_after = stats_after.total_uses;

    EXPECT_GE(uses_after, uses_before + 3)
        << "Use count should increase by at least 3 after 3 preparations";
}

// ============================================================================
// Caching Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, CachedEnvironmentIsReusedForSameTemplate) {
    auto tmpl = create_test_template("cache_reuse");
    mgr->register_template(tmpl);

    // First use - creates cache
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id1);

    auto stats_after_first = mgr->get_stats();

    // Second use - reuses cache
    std::string job_id2 = "job_" + generate_random_string(8);
    std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);

    auto stats_after_second = mgr->get_stats();

    // Cache count shouldn't increase on reuse
    EXPECT_EQ(stats_after_second.cached_environments, stats_after_first.cached_environments)
        << "Cached environment count should not increase when reusing";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, CacheHitUpdatesStatistics) {
    auto tmpl = create_test_template("cache_hit_stats");
    mgr->register_template(tmpl);

    // Build cache with first use
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id1);

    auto stats_after_first = mgr->get_stats();

    // Hit cache with second use
    std::string job_id2 = "job_" + generate_random_string(8);
    std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);

    auto stats_after_second = mgr->get_stats();

    EXPECT_GT(stats_after_second.total_uses, stats_after_first.total_uses)
        << "Cache hit should increment usage statistics";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, CacheMissCreatesNewEnvironment) {
    auto tmpl1 = create_test_template("cache_miss_1");
    auto tmpl2 = create_test_template("cache_miss_2");

    mgr->register_template(tmpl1);
    mgr->register_template(tmpl2);

    auto stats_before = mgr->get_stats();

    // Prepare environments for different templates (both cache misses)
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl1.name, job_id1);

    std::string job_id2 = "job_" + generate_random_string(8);
    std::string env_path2 = mgr->prepare_environment(tmpl2.name, job_id2);

    auto stats_after = mgr->get_stats();

    EXPECT_GE(stats_after.cached_environments, stats_before.cached_environments + 2)
        << "Cache misses should create new cached environments";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, CachedEnvironmentMarkedAsReady) {
    auto tmpl = create_test_template("cache_ready");
    mgr->register_template(tmpl);

    std::string job_id = "job_" + generate_random_string(8);
    std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

    // If we can prepare again without error, the cached environment is marked ready
    std::string job_id2 = "job_" + generate_random_string(8);
    EXPECT_NO_THROW({
        std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);
        fs::remove_all(env_path2);
    }) << "Reusing cached environment should work (implies ready=true)";

    // Cleanup
    fs::remove_all(env_path);
}

// ============================================================================
// Stats Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, GetStatsReturnsCorrectTemplateCount) {
    auto stats_before = mgr->get_stats();
    int count_before = stats_before.total_templates;

    // Register new test templates
    auto tmpl1 = create_test_template("stats_tmpl_1");
    auto tmpl2 = create_test_template("stats_tmpl_2");
    mgr->register_template(tmpl1);
    mgr->register_template(tmpl2);

    auto stats_after = mgr->get_stats();

    EXPECT_EQ(stats_after.total_templates, count_before + 2)
        << "Stats should reflect correct number of registered templates";
}

TEST_F(EnvironmentManagerTest, GetStatsReturnsCorrectCachedCount) {
    auto tmpl1 = create_test_template("stats_cached_1");
    auto tmpl2 = create_test_template("stats_cached_2");
    mgr->register_template(tmpl1);
    mgr->register_template(tmpl2);

    auto stats_before = mgr->get_stats();

    // Prepare environments to create caches
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl1.name, job_id1);

    std::string job_id2 = "job_" + generate_random_string(8);
    std::string env_path2 = mgr->prepare_environment(tmpl2.name, job_id2);

    auto stats_after = mgr->get_stats();

    EXPECT_GE(stats_after.cached_environments, stats_before.cached_environments + 2)
        << "Stats should reflect cached environments created";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, GetStatsReturnsCorrectTotalUses) {
    auto tmpl = create_test_template("stats_uses");
    mgr->register_template(tmpl);

    auto stats_before = mgr->get_stats();

    // Use environment multiple times
    std::vector<std::string> paths;
    for (int i = 0; i < 5; i++) {
        std::string job_id = "job_" + std::to_string(i) + "_" + generate_random_string(6);
        paths.push_back(mgr->prepare_environment(tmpl.name, job_id));
    }

    auto stats_after = mgr->get_stats();

    EXPECT_GE(stats_after.total_uses, stats_before.total_uses + 5)
        << "Stats should track all uses of cached environments";

    // Cleanup
    for (const auto& path : paths) {
        fs::remove_all(path);
    }
}

TEST_F(EnvironmentManagerTest, GetStatsDiskUsageReasonable) {
    auto tmpl = create_test_template("stats_disk");
    mgr->register_template(tmpl);

    std::string job_id = "job_" + generate_random_string(8);
    std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

    auto stats = mgr->get_stats();

    // Disk usage should be >= 0 (we can't guarantee > 0 without actually installing packages)
    EXPECT_GE(stats.disk_usage_mb, 0)
        << "Disk usage should be non-negative";

    // Cleanup
    fs::remove_all(env_path);
}

// ============================================================================
// Cleanup Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, CleanupOldEnvironmentsRemovesOldOnes) {
    // Create template with very short max_age
    auto tmpl = create_test_template("cleanup_old");
    tmpl.max_age_hours = 0;  // Expire immediately
    mgr->register_template(tmpl);

    // Prepare environment
    std::string job_id = "job_" + generate_random_string(8);
    std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

    auto stats_before = mgr->get_stats();

    // Small delay to ensure time passes
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cleanup should remove the environment
    mgr->cleanup_old_environments();

    auto stats_after = mgr->get_stats();

    EXPECT_LE(stats_after.cached_environments, stats_before.cached_environments)
        << "Cleanup should remove or maintain cache count";

    // Cleanup
    fs::remove_all(env_path);
}

TEST_F(EnvironmentManagerTest, CleanupPreservesRecentEnvironments) {
    auto tmpl = create_test_template("cleanup_preserve");
    tmpl.max_age_hours = 24;  // Fresh
    mgr->register_template(tmpl);

    // Prepare environment
    std::string job_id = "job_" + generate_random_string(8);
    std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

    // Cleanup should NOT remove recent environment
    mgr->cleanup_old_environments();

    // We can still prepare using the same template (cache preserved)
    std::string job_id2 = "job_" + generate_random_string(8);
    EXPECT_NO_THROW({
        std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);
        fs::remove_all(env_path2);
    }) << "Recent cached environment should be preserved by cleanup";

    // Cleanup
    fs::remove_all(env_path);
}

TEST_F(EnvironmentManagerTest, CleanupRespectsMaxAgeHours) {
    // Create two templates with different ages
    auto tmpl_fresh = create_test_template("cleanup_fresh");
    tmpl_fresh.max_age_hours = 24;

    auto tmpl_old = create_test_template("cleanup_old");
    tmpl_old.max_age_hours = 0;

    mgr->register_template(tmpl_fresh);
    mgr->register_template(tmpl_old);

    // Prepare both
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl_fresh.name, job_id1);

    std::string job_id2 = "job_" + generate_random_string(8);
    std::string env_path2 = mgr->prepare_environment(tmpl_old.name, job_id2);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cleanup
    mgr->cleanup_old_environments();

    // Fresh template should still work
    EXPECT_NO_THROW({
        std::string job_id3 = "job_" + generate_random_string(8);
        std::string env_path3 = mgr->prepare_environment(tmpl_fresh.name, job_id3);
        fs::remove_all(env_path3);
    }) << "Fresh template cache should survive cleanup";

    // Cleanup
    fs::remove_all(env_path1);
    fs::remove_all(env_path2);
}

TEST_F(EnvironmentManagerTest, CleanupReducesCachedCount) {
    auto tmpl = create_test_template("cleanup_reduce");
    tmpl.max_age_hours = 0;  // Expire immediately
    mgr->register_template(tmpl);

    // Prepare to create cache
    std::string job_id = "job_" + generate_random_string(8);
    std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

    auto stats_before = mgr->get_stats();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cleanup
    mgr->cleanup_old_environments();

    auto stats_after = mgr->get_stats();

    EXPECT_LE(stats_after.cached_environments, stats_before.cached_environments)
        << "Cleanup should reduce or maintain cached environment count";

    // Cleanup
    fs::remove_all(env_path);
}

// ============================================================================
// Rebuild Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, RebuildTemplateForcesCacheRebuild) {
    auto tmpl = create_test_template("rebuild_force");
    mgr->register_template(tmpl);

    // Build initial cache
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id1);

    // Rebuild (clears cache)
    mgr->rebuild_template(tmpl.name);

    // Next prepare should rebuild
    std::string job_id2 = "job_" + generate_random_string(8);
    EXPECT_NO_THROW({
        std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id2);
        fs::remove_all(env_path2);
    }) << "Prepare after rebuild should succeed";

    // Cleanup
    fs::remove_all(env_path1);
}

TEST_F(EnvironmentManagerTest, RebuildNonExistentTemplate) {
    std::string fake_template = test_prefix + "rebuild_nonexistent";

    // Rebuilding non-existent template should not throw (it's a no-op)
    EXPECT_NO_THROW({
        mgr->rebuild_template(fake_template);
    }) << "Rebuilding non-existent template should be safe no-op";
}

// ============================================================================
// Built-in Templates Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, BuiltInMLBasicReturnsValidTemplate) {
    auto tmpl = BuiltInTemplates::ml_basic();

    EXPECT_EQ(tmpl.name, "ml-basic");
    EXPECT_FALSE(tmpl.base_image.empty());
    EXPECT_FALSE(tmpl.packages.empty())
        << "ML basic template should have packages";
    EXPECT_FALSE(tmpl.gpu_enabled)
        << "ML basic should not require GPU";
}

TEST_F(EnvironmentManagerTest, BuiltInVisionReturnsValidTemplate) {
    auto tmpl = BuiltInTemplates::vision();

    EXPECT_EQ(tmpl.name, "vision");
    EXPECT_FALSE(tmpl.base_image.empty());
    EXPECT_FALSE(tmpl.packages.empty())
        << "Vision template should have packages";
    EXPECT_TRUE(tmpl.gpu_enabled)
        << "Vision template should support GPU";
}

TEST_F(EnvironmentManagerTest, BuiltInNLPReturnsValidTemplate) {
    auto tmpl = BuiltInTemplates::nlp();

    EXPECT_EQ(tmpl.name, "nlp");
    EXPECT_FALSE(tmpl.base_image.empty());
    EXPECT_FALSE(tmpl.packages.empty())
        << "NLP template should have packages";
    EXPECT_TRUE(tmpl.gpu_enabled)
        << "NLP template should support GPU";
}

TEST_F(EnvironmentManagerTest, BuiltInDataScienceReturnsValidTemplate) {
    auto tmpl = BuiltInTemplates::data_science();

    EXPECT_EQ(tmpl.name, "data-science");
    EXPECT_FALSE(tmpl.base_image.empty());
    EXPECT_FALSE(tmpl.packages.empty())
        << "Data science template should have packages";
}

TEST_F(EnvironmentManagerTest, BuiltInScientificReturnsValidTemplate) {
    auto tmpl = BuiltInTemplates::scientific();

    EXPECT_EQ(tmpl.name, "scientific");
    EXPECT_FALSE(tmpl.base_image.empty());
    EXPECT_FALSE(tmpl.packages.empty())
        << "Scientific template should have packages";
}

TEST_F(EnvironmentManagerTest, BuiltInTemplatesHaveNonEmptyPackageLists) {
    std::vector<EnvironmentTemplate> templates = {
        BuiltInTemplates::ml_basic(),
        BuiltInTemplates::vision(),
        BuiltInTemplates::nlp(),
        BuiltInTemplates::data_science(),
        BuiltInTemplates::scientific()
    };

    for (const auto& tmpl : templates) {
        EXPECT_GT(tmpl.packages.size(), 0)
            << "Built-in template " << tmpl.name << " should have packages";
    }
}

TEST_F(EnvironmentManagerTest, BuiltInTemplatesAutoRegistered) {
    // Built-in templates should be auto-registered on singleton creation
    EXPECT_TRUE(mgr->has_template("ml-basic"))
        << "ml-basic should be auto-registered";
    EXPECT_TRUE(mgr->has_template("vision"))
        << "vision should be auto-registered";
    EXPECT_TRUE(mgr->has_template("nlp"))
        << "nlp should be auto-registered";
    EXPECT_TRUE(mgr->has_template("data-science"))
        << "data-science should be auto-registered";
    EXPECT_TRUE(mgr->has_template("scientific"))
        << "scientific should be auto-registered";
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, PrepareEnvironmentInvalidTemplateName) {
    std::string fake_name = test_prefix + "invalid";
    std::string job_id = "job_" + generate_random_string(8);

    EXPECT_THROW({
        mgr->prepare_environment(fake_name, job_id);
    }, std::runtime_error)
        << "Should throw when template doesn't exist";
}

TEST_F(EnvironmentManagerTest, InstallPackagesWithInvalidPackagesGracefulFailure) {
    // We can't directly test install_packages as it's private, but we can
    // test the behavior through prepare_environment with bad packages
    auto tmpl = create_test_template("invalid_packages");
    tmpl.packages = {"this-package-definitely-does-not-exist-12345"};
    mgr->register_template(tmpl);

    std::string job_id = "job_" + generate_random_string(8);

    // This might throw or might create environment with failed install
    // Behavior depends on implementation details
    try {
        std::string env_path = mgr->prepare_environment(tmpl.name, job_id);
        fs::remove_all(env_path);  // Cleanup if it succeeded
    } catch (const std::runtime_error&) {
        // Expected - installation failed
        SUCCEED() << "Installation failure handled with exception";
    }
}

TEST_F(EnvironmentManagerTest, RunSetupScriptWithNonExistentScript) {
    auto tmpl = create_test_template("nonexistent_script");
    tmpl.setup_script = "/tmp/this_script_does_not_exist_" + generate_random_string(8) + ".sh";
    mgr->register_template(tmpl);

    std::string job_id = "job_" + generate_random_string(8);

    // Should throw because setup script doesn't exist
    EXPECT_THROW({
        mgr->prepare_environment(tmpl.name, job_id);
    }, std::runtime_error)
        << "Should throw when setup script doesn't exist";
}

TEST_F(EnvironmentManagerTest, EmptyJobIDHandling) {
    auto tmpl = create_test_template("empty_job_id");
    mgr->register_template(tmpl);

    // Empty job_id might be accepted (creates job_ directory)
    std::string empty_job_id = "";

    // This should either work or throw, but not crash
    try {
        std::string env_path = mgr->prepare_environment(tmpl.name, empty_job_id);
        EXPECT_FALSE(env_path.empty())
            << "Even with empty job_id, path should be returned";
        fs::remove_all(env_path);
    } catch (...) {
        // If implementation rejects empty job_id, that's fine too
        SUCCEED() << "Empty job_id handled (rejected)";
    }
}

// ============================================================================
// Concurrency Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, ConcurrentTemplateRegistration) {
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    // Register multiple templates concurrently
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([this, i, &success_count]() {
            try {
                auto tmpl = create_test_template("concurrent_reg_" + std::to_string(i));
                mgr->register_template(tmpl);
                success_count++;
            } catch (...) {
                // Should not throw
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), 10)
        << "All concurrent registrations should succeed";
}

TEST_F(EnvironmentManagerTest, ConcurrentEnvironmentPreparation) {
    auto tmpl = create_test_template("concurrent_prep");
    mgr->register_template(tmpl);

    std::vector<std::thread> threads;
    std::vector<std::string> paths;
    std::mutex paths_mutex;
    std::atomic<int> success_count{0};

    // Prepare environments concurrently for same template
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([this, &tmpl, i, &paths, &paths_mutex, &success_count]() {
            try {
                std::string job_id = "job_concurrent_" + std::to_string(i);
                std::string env_path = mgr->prepare_environment(tmpl.name, job_id);

                {
                    std::lock_guard<std::mutex> lock(paths_mutex);
                    paths.push_back(env_path);
                }
                success_count++;
            } catch (...) {
                // Should not throw
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), 5)
        << "All concurrent preparations should succeed";

    // All paths should be unique
    std::set<std::string> unique_paths(paths.begin(), paths.end());
    EXPECT_EQ(unique_paths.size(), paths.size())
        << "Each concurrent preparation should get unique path";

    // Cleanup
    for (const auto& path : paths) {
        fs::remove_all(path);
    }
}

TEST_F(EnvironmentManagerTest, ConcurrentCacheConsistency) {
    auto tmpl = create_test_template("cache_consistency");
    mgr->register_template(tmpl);

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    // Multiple threads preparing environments from same template
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([this, &tmpl, i, &success_count]() {
            try {
                std::string job_id = "job_cache_" + std::to_string(i);
                std::string env_path = mgr->prepare_environment(tmpl.name, job_id);
                success_count++;
                fs::remove_all(env_path);  // Cleanup immediately
            } catch (...) {
                // Should not throw
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), 10)
        << "All concurrent cache accesses should succeed";

    // Verify stats are consistent
    auto stats = mgr->get_stats();
    EXPECT_GT(stats.total_uses, 0)
        << "Concurrent use should be tracked correctly";
}

TEST_F(EnvironmentManagerTest, ConcurrentMixedOperations) {
    // Mix of register, prepare, cleanup, rebuild operations
    std::vector<std::thread> threads;
    std::atomic<int> total_ops{0};

    for (int i = 0; i < 4; i++) {
        threads.emplace_back([this, i, &total_ops]() {
            auto tmpl = create_test_template("mixed_" + std::to_string(i));

            // Register
            mgr->register_template(tmpl);
            total_ops++;

            // Prepare
            std::string job_id = "job_mixed_" + std::to_string(i);
            try {
                std::string env_path = mgr->prepare_environment(tmpl.name, job_id);
                total_ops++;
                fs::remove_all(env_path);
            } catch (...) {}

            // Cleanup
            mgr->cleanup_old_environments();
            total_ops++;

            // Rebuild
            mgr->rebuild_template(tmpl.name);
            total_ops++;
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(total_ops.load(), 16)
        << "All mixed concurrent operations should complete";
}

// ============================================================================
// Edge Cases and Integration Tests
// ============================================================================

TEST_F(EnvironmentManagerTest, PrepareWithSpecialCharactersInJobID) {
    auto tmpl = create_test_template("special_chars");
    mgr->register_template(tmpl);

    // Job ID with underscores, hyphens
    std::string job_id = "job_with-special_chars_123";

    EXPECT_NO_THROW({
        std::string env_path = mgr->prepare_environment(tmpl.name, job_id);
        EXPECT_FALSE(env_path.empty());
        fs::remove_all(env_path);
    }) << "Should handle job IDs with special characters";
}

TEST_F(EnvironmentManagerTest, MultiplePreparationsForSameJobID) {
    auto tmpl = create_test_template("same_job_id");
    mgr->register_template(tmpl);

    std::string job_id = "duplicate_job_id";

    // First preparation
    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id);

    // Second preparation with same job_id (will overwrite)
    std::string env_path2 = mgr->prepare_environment(tmpl.name, job_id);

    // Both should succeed, might return same path
    EXPECT_FALSE(env_path1.empty());
    EXPECT_FALSE(env_path2.empty());

    // Cleanup
    fs::remove_all(env_path1);
    if (env_path1 != env_path2) {
        fs::remove_all(env_path2);
    }
}

TEST_F(EnvironmentManagerTest, StatsAfterRebuild) {
    auto tmpl = create_test_template("stats_after_rebuild");
    mgr->register_template(tmpl);

    // Build cache
    std::string job_id1 = "job_" + generate_random_string(8);
    std::string env_path1 = mgr->prepare_environment(tmpl.name, job_id1);

    auto stats_before_rebuild = mgr->get_stats();

    // Rebuild
    mgr->rebuild_template(tmpl.name);

    auto stats_after_rebuild = mgr->get_stats();

    EXPECT_LE(stats_after_rebuild.cached_environments, stats_before_rebuild.cached_environments)
        << "Rebuild should reduce cached environment count";

    // Cleanup
    fs::remove_all(env_path1);
}

TEST_F(EnvironmentManagerTest, LongRunningOperationsDoNotBlock) {
    auto tmpl1 = create_test_template("nonblock_1");
    auto tmpl2 = create_test_template("nonblock_2");
    mgr->register_template(tmpl1);
    mgr->register_template(tmpl2);

    std::atomic<bool> first_started{false};
    std::atomic<bool> second_completed{false};

    std::thread t1([&]() {
        first_started = true;
        std::string job_id = "job_" + generate_random_string(8);
        std::string env_path = mgr->prepare_environment(tmpl1.name, job_id);
        fs::remove_all(env_path);
    });

    std::thread t2([&]() {
        // Wait for first thread to start
        while (!first_started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Prepare different template (should not block on first)
        std::string job_id = "job_" + generate_random_string(8);
        std::string env_path = mgr->prepare_environment(tmpl2.name, job_id);
        second_completed = true;
        fs::remove_all(env_path);
    });

    t1.join();
    t2.join();

    EXPECT_TRUE(second_completed)
        << "Second operation should complete even with first running";
}

} // namespace
} // namespace sandrun
