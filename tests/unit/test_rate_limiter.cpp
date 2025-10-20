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

} // namespace
} // namespace sandrun