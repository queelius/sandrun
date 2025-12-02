#include <gtest/gtest.h>
#include "rate_limiter.h"
#include <thread>
#include <chrono>
#include <vector>

namespace sandrun {
namespace {

class RateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create rate limiter with test config
        RateLimiter::Config config;
        config.cpu_seconds_per_minute = 10.0;
        config.max_concurrent_jobs = 2;
        config.max_jobs_per_hour = 10;
        config.cleanup_after_minutes = 60;
        
        limiter = std::make_unique<RateLimiter>(config);
    }

    std::unique_ptr<RateLimiter> limiter;
};

TEST_F(RateLimiterTest, BasicQuotaCheck) {
    std::string ip = "192.168.1.1";
    
    auto quota = limiter->check_quota(ip);
    EXPECT_TRUE(quota.can_submit);
    EXPECT_EQ(quota.active_jobs, 0);
    EXPECT_EQ(quota.jobs_this_hour, 0);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_available, 10.0);
}

TEST_F(RateLimiterTest, JobRegistration) {
    std::string ip = "192.168.1.2";
    
    // Register first job
    bool success = limiter->register_job_start(ip, "job_1");
    EXPECT_TRUE(success);
    
    auto quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.active_jobs, 1);
    EXPECT_TRUE(quota.can_submit); // Should still be able to submit another
    
    // Register second job
    success = limiter->register_job_start(ip, "job_2");
    EXPECT_TRUE(success);
    
    quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.active_jobs, 2);
    EXPECT_FALSE(quota.can_submit); // At max concurrent jobs
    
    // Try to register third job - should fail
    success = limiter->register_job_start(ip, "job_3");
    EXPECT_FALSE(success);
}

TEST_F(RateLimiterTest, JobCompletion) {
    std::string ip = "192.168.1.3";
    
    // Start and complete a job
    limiter->register_job_start(ip, "job_1");
    
    auto quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.active_jobs, 1);
    
    // Complete job with CPU usage
    limiter->register_job_end(ip, "job_1", 5.0); // Used 5 CPU seconds
    
    quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.active_jobs, 0);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_used, 5.0);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_available, 5.0); // 10 - 5 = 5
}

TEST_F(RateLimiterTest, CPUQuotaExhaustion) {
    std::string ip = "192.168.1.4";
    
    // Use up CPU quota
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 9.5); // Use 9.5 seconds
    
    auto quota = limiter->check_quota(ip);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_available, 0.5);
    EXPECT_TRUE(quota.can_submit); // Still can submit with 0.5 seconds left
    
    // Use remaining quota
    limiter->register_job_start(ip, "job_2");
    limiter->register_job_end(ip, "job_2", 0.6); // Use 0.6 seconds (over limit)
    
    quota = limiter->check_quota(ip);
    EXPECT_LT(quota.cpu_seconds_available, 0.1); // Should be near 0
    EXPECT_FALSE(quota.can_submit); // Cannot submit when quota exhausted
}

TEST_F(RateLimiterTest, HourlyJobLimit) {
    std::string ip = "192.168.1.5";
    
    // Submit max jobs per hour
    for (int i = 0; i < 10; i++) {
        std::string job_id = "job_" + std::to_string(i);
        limiter->register_job_start(ip, job_id);
        limiter->register_job_end(ip, job_id, 0.1); // Minimal CPU usage
    }
    
    auto quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.jobs_this_hour, 10);
    EXPECT_FALSE(quota.can_submit); // Hit hourly limit
    EXPECT_TRUE(quota.reason.find("jobs per hour") != std::string::npos);
}

TEST_F(RateLimiterTest, MultipleIPsIndependent) {
    std::string ip1 = "192.168.1.10";
    std::string ip2 = "192.168.1.11";
    
    // Use quota on IP1
    limiter->register_job_start(ip1, "job_1");
    limiter->register_job_end(ip1, "job_1", 8.0);
    
    auto quota1 = limiter->check_quota(ip1);
    EXPECT_DOUBLE_EQ(quota1.cpu_seconds_available, 2.0);
    
    // IP2 should have full quota
    auto quota2 = limiter->check_quota(ip2);
    EXPECT_DOUBLE_EQ(quota2.cpu_seconds_available, 10.0);
    EXPECT_TRUE(quota2.can_submit);
}

TEST_F(RateLimiterTest, QuotaReplenishment) {
    std::string ip = "192.168.1.20";
    
    // Use up quota
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 10.0);
    
    auto quota = limiter->check_quota(ip);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_available, 0.0);
    
    // Note: In real implementation, quota would replenish over time
    // This test would need time mocking to properly test replenishment
}

TEST_F(RateLimiterTest, CleanupOldEntries) {
    std::string ip = "192.168.1.30";
    
    // Register activity
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 1.0);
    
    // Cleanup should preserve recent entries
    limiter->cleanup_old_entries();
    
    auto quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.jobs_this_hour, 1); // Should still be tracked
}

TEST_F(RateLimiterTest, ConcurrentAccess) {
    // Test thread safety
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([this, i]() {
            std::string ip = "192.168.2." + std::to_string(i);
            for (int j = 0; j < 5; j++) {
                std::string job_id = "job_" + std::to_string(i) + "_" + std::to_string(j);
                if (limiter->register_job_start(ip, job_id)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    limiter->register_job_end(ip, job_id, 0.5);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify all IPs are tracked
    for (int i = 0; i < 10; i++) {
        std::string ip = "192.168.2." + std::to_string(i);
        auto quota = limiter->check_quota(ip);
        EXPECT_GT(quota.jobs_this_hour, 0);
    }
}

TEST_F(RateLimiterTest, ZeroCPUJob) {
    std::string ip = "192.168.1.40";

    // Job that uses no CPU (e.g., immediately fails)
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 0.0);

    auto quota = limiter->check_quota(ip);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_used, 0.0);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_available, 10.0);
    EXPECT_EQ(quota.jobs_this_hour, 1);
}

// ============================================================================
// get_available_cpu_seconds() Tests
// ============================================================================

TEST_F(RateLimiterTest, GetAvailableCPUSeconds_NewIP) {
    // Given: A new IP with no history
    std::string ip = "192.168.3.1";

    // When: Getting available CPU seconds
    double available = limiter->get_available_cpu_seconds(ip);

    // Then: Should return full quota
    EXPECT_DOUBLE_EQ(available, 10.0);
}

TEST_F(RateLimiterTest, GetAvailableCPUSeconds_AfterUsage) {
    // Given: An IP that has used some CPU
    std::string ip = "192.168.3.2";
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 3.5);

    // When: Getting available CPU seconds
    double available = limiter->get_available_cpu_seconds(ip);

    // Then: Should return remaining quota
    EXPECT_DOUBLE_EQ(available, 6.5);
}

TEST_F(RateLimiterTest, GetAvailableCPUSeconds_ExhaustedQuota) {
    // Given: An IP that has exhausted CPU quota
    std::string ip = "192.168.3.3";
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 10.0);

    // When: Getting available CPU seconds
    double available = limiter->get_available_cpu_seconds(ip);

    // Then: Should return 0
    EXPECT_DOUBLE_EQ(available, 0.0);
}

TEST_F(RateLimiterTest, GetAvailableCPUSeconds_OverusedQuota) {
    // Given: An IP that has used more than quota (edge case)
    std::string ip = "192.168.3.4";
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 15.0);  // Over limit

    // When: Getting available CPU seconds
    double available = limiter->get_available_cpu_seconds(ip);

    // Then: Should return 0 (clamped to 0)
    EXPECT_DOUBLE_EQ(available, 0.0);
}

TEST_F(RateLimiterTest, GetAvailableCPUSeconds_MultipleJobs) {
    // Given: An IP with multiple completed jobs
    std::string ip = "192.168.3.5";
    for (int i = 0; i < 3; i++) {
        std::string job_id = "job_" + std::to_string(i);
        limiter->register_job_start(ip, job_id);
        limiter->register_job_end(ip, job_id, 2.0);  // 2 seconds each
    }

    // When: Getting available CPU seconds
    double available = limiter->get_available_cpu_seconds(ip);

    // Then: Should return 10 - 6 = 4
    EXPECT_DOUBLE_EQ(available, 4.0);
}

// ============================================================================
// register_job_end() Edge Cases
// ============================================================================

TEST_F(RateLimiterTest, RegisterJobEnd_UnknownIP) {
    // Given: An IP that has never registered
    std::string unknown_ip = "10.0.0.99";

    // When: Trying to end a job for unknown IP
    // Then: Should not crash (graceful no-op)
    limiter->register_job_end(unknown_ip, "nonexistent_job", 1.0);

    // Verify IP is still unknown (not created by register_job_end)
    double available = limiter->get_available_cpu_seconds(unknown_ip);
    EXPECT_DOUBLE_EQ(available, 10.0);  // Full quota = IP wasn't registered
}

TEST_F(RateLimiterTest, RegisterJobEnd_UnknownJobID) {
    // Given: An IP with a known job
    std::string ip = "192.168.4.1";
    limiter->register_job_start(ip, "known_job");

    // When: Trying to end a different job
    limiter->register_job_end(ip, "unknown_job", 1.0);

    // Then: Known job should still be active
    auto quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.active_jobs, 1);
}

// ============================================================================
// Quota Message Tests
// ============================================================================

TEST_F(RateLimiterTest, QuotaMessage_ConcurrentJobsReason) {
    // Given: An IP at max concurrent jobs
    std::string ip = "192.168.5.1";
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_start(ip, "job_2");

    // When: Checking quota
    auto quota = limiter->check_quota(ip);

    // Then: Reason should mention concurrent jobs
    EXPECT_FALSE(quota.can_submit);
    EXPECT_TRUE(quota.reason.find("concurrent") != std::string::npos ||
                quota.reason.find("Max") != std::string::npos);
}

TEST_F(RateLimiterTest, QuotaMessage_CPUExhaustedWithHistory) {
    // Given: An IP that has exhausted CPU quota with history
    std::string ip = "192.168.5.2";
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 10.5);

    // When: Checking quota
    auto quota = limiter->check_quota(ip);

    // Then: Reason should mention CPU quota and wait time
    EXPECT_FALSE(quota.can_submit);
    EXPECT_TRUE(quota.reason.find("CPU") != std::string::npos ||
                quota.reason.find("quota") != std::string::npos);
}

// ============================================================================
// Cleanup Tests
// ============================================================================

TEST_F(RateLimiterTest, CleanupOldEntries_PreservesActiveJobs) {
    // Given: An IP with an active job
    std::string ip = "192.168.6.1";
    limiter->register_job_start(ip, "active_job");

    // When: Running cleanup
    limiter->cleanup_old_entries();

    // Then: Active job should still be tracked
    auto quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.active_jobs, 1);
}

TEST_F(RateLimiterTest, CleanupOldEntries_PreservesRecentHistory) {
    // Given: An IP with recent CPU usage
    std::string ip = "192.168.6.2";
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", 5.0);

    // When: Running cleanup
    limiter->cleanup_old_entries();

    // Then: CPU usage should still be tracked
    double available = limiter->get_available_cpu_seconds(ip);
    EXPECT_DOUBLE_EQ(available, 5.0);
}

// ============================================================================
// Custom Config Tests
// ============================================================================

TEST_F(RateLimiterTest, CustomConfig_HigherLimits) {
    // Given: A rate limiter with higher limits
    RateLimiter::Config config;
    config.cpu_seconds_per_minute = 60.0;
    config.max_concurrent_jobs = 10;
    config.max_jobs_per_hour = 100;
    config.cleanup_after_minutes = 120;

    RateLimiter custom_limiter(config);
    std::string ip = "192.168.7.1";

    // When: Checking quota
    auto quota = custom_limiter.check_quota(ip);

    // Then: Should reflect custom limits
    EXPECT_TRUE(quota.can_submit);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_available, 60.0);
}

TEST_F(RateLimiterTest, CustomConfig_RestrictiveLimits) {
    // Given: A rate limiter with restrictive limits
    RateLimiter::Config config;
    config.cpu_seconds_per_minute = 1.0;
    config.max_concurrent_jobs = 1;
    config.max_jobs_per_hour = 2;
    config.cleanup_after_minutes = 10;

    RateLimiter custom_limiter(config);
    std::string ip = "192.168.7.2";

    // When: Starting one job
    bool success = custom_limiter.register_job_start(ip, "job_1");
    EXPECT_TRUE(success);

    // Then: Second job should fail
    success = custom_limiter.register_job_start(ip, "job_2");
    EXPECT_FALSE(success);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(RateLimiterTest, DuplicateJobID) {
    // Given: A job is started
    std::string ip = "192.168.8.1";
    limiter->register_job_start(ip, "duplicate_job");

    // When: Same job ID is started again (edge case)
    // This is unusual but shouldn't crash
    (void)limiter->register_job_start(ip, "duplicate_job");

    // Then: May succeed or fail depending on implementation
    // The key is it doesn't crash
    auto quota = limiter->check_quota(ip);
    EXPECT_GE(quota.active_jobs, 1);
}

TEST_F(RateLimiterTest, EmptyIPAddress) {
    // Given: An empty IP address
    std::string empty_ip = "";

    // When: Using empty IP
    auto quota = limiter->check_quota(empty_ip);

    // Then: Should handle gracefully
    EXPECT_TRUE(quota.can_submit);
    EXPECT_DOUBLE_EQ(quota.cpu_seconds_available, 10.0);
}

TEST_F(RateLimiterTest, VeryLongJobID) {
    // Given: A very long job ID
    std::string ip = "192.168.8.2";
    std::string long_job_id(1000, 'x');  // 1000 character job ID

    // When: Registering with long job ID
    bool success = limiter->register_job_start(ip, long_job_id);

    // Then: Should handle gracefully
    EXPECT_TRUE(success);

    // And: Should be able to end it
    limiter->register_job_end(ip, long_job_id, 1.0);
    auto quota = limiter->check_quota(ip);
    EXPECT_EQ(quota.active_jobs, 0);
}

TEST_F(RateLimiterTest, NegativeCPUSeconds) {
    // Given: A job that somehow reports negative CPU (edge case)
    std::string ip = "192.168.8.3";
    limiter->register_job_start(ip, "job_1");
    limiter->register_job_end(ip, "job_1", -1.0);  // Negative CPU

    // When: Checking quota
    auto quota = limiter->check_quota(ip);

    // Then: Should handle gracefully (may have full quota or ignore negative)
    EXPECT_TRUE(quota.cpu_seconds_available >= 0);
}

} // namespace
} // namespace sandrun