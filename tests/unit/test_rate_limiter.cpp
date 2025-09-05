#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "rate_limiter.h"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using namespace sandrun;
using namespace testing;

class RateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        rate_limiter = std::make_unique<RateLimiter>();
        client_id = "test_client_" + std::to_string(std::time(nullptr));
    }
    
    void TearDown() override {
        rate_limiter.reset();
    }
    
    std::unique_ptr<RateLimiter> rate_limiter;
    std::string client_id;
    
    ClientQuota createTestQuota(const std::string& id = "") {
        ClientQuota quota;
        quota.client_id = id.empty() ? client_id : id;
        quota.limits = {
            {LimitType::REQUESTS_PER_SECOND, 10, std::chrono::seconds(1)},
            {LimitType::REQUESTS_PER_MINUTE, 100, std::chrono::seconds(60)},
            {LimitType::CONCURRENT_JOBS, 5, std::chrono::seconds(1)}
        };
        quota.is_premium = false;
        quota.priority_level = 1;
        return quota;
    }
};

// Basic rate limiting tests
TEST_F(RateLimiterTest, BasicRateLimiting) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Should allow requests within limit
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    // Should reject requests over limit
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
}

TEST_F(RateLimiterTest, RateLimitRecovery) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Exhaust the limit
    for (int i = 0; i < 10; ++i) {
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
    
    // Wait for window to reset (1 second + small buffer)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    
    // Should allow requests again
    EXPECT_TRUE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
}

TEST_F(RateLimiterTest, MultipleClientIsolation) {
    std::string client1 = "client_1";
    std::string client2 = "client_2";
    
    ClientQuota quota1 = createTestQuota(client1);
    ClientQuota quota2 = createTestQuota(client2);
    
    rate_limiter->setClientQuota(client1, quota1);
    rate_limiter->setClientQuota(client2, quota2);
    
    // Exhaust client1's limit
    for (int i = 0; i < 10; ++i) {
        rate_limiter->recordUsage(client1, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    // Client1 should be blocked
    EXPECT_FALSE(rate_limiter->checkLimit(client1, LimitType::REQUESTS_PER_SECOND, 1));
    
    // Client2 should still have full quota
    EXPECT_TRUE(rate_limiter->checkLimit(client2, LimitType::REQUESTS_PER_SECOND, 1));
    
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(rate_limiter->checkLimit(client2, LimitType::REQUESTS_PER_SECOND, 1));
        rate_limiter->recordUsage(client2, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    // Now client2 should be blocked too
    EXPECT_FALSE(rate_limiter->checkLimit(client2, LimitType::REQUESTS_PER_SECOND, 1));
}

// Resource acquisition and release tests
TEST_F(RateLimiterTest, ResourceAcquisitionAndRelease) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Acquire resources
    EXPECT_TRUE(rate_limiter->acquireResource(client_id, LimitType::CONCURRENT_JOBS, 3));
    EXPECT_TRUE(rate_limiter->acquireResource(client_id, LimitType::CONCURRENT_JOBS, 2));
    
    // Should be at limit now (5 concurrent jobs)
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::CONCURRENT_JOBS, 1));
    
    // Release some resources
    rate_limiter->releaseResource(client_id, LimitType::CONCURRENT_JOBS, 2);
    
    // Should be able to acquire again
    EXPECT_TRUE(rate_limiter->acquireResource(client_id, LimitType::CONCURRENT_JOBS, 1));
}

TEST_F(RateLimiterTest, ResourceAcquisitionException) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Exhaust limit first
    for (int i = 0; i < 10; ++i) {
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    // Should throw exception when trying to acquire
    EXPECT_THROW(
        rate_limiter->acquireResource(client_id, LimitType::REQUESTS_PER_SECOND, 1),
        RateLimitExceededException
    );
    
    // Verify exception contains correct information
    try {
        rate_limiter->acquireResource(client_id, LimitType::REQUESTS_PER_SECOND, 1);
        FAIL() << "Expected RateLimitExceededException";
    } catch (const RateLimitExceededException& e) {
        EXPECT_EQ(e.getLimitType(), LimitType::REQUESTS_PER_SECOND);
        EXPECT_GT(e.getRetryAfter().count(), 0);
        EXPECT_THAT(std::string(e.what()), HasSubstr("Rate limit exceeded"));
    }
}

// Usage tracking and statistics tests
TEST_F(RateLimiterTest, UsageTracking) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Record some usage
    for (int i = 0; i < 5; ++i) {
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    QuotaUsage usage = rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND);
    
    EXPECT_EQ(usage.current_usage, 5);
    EXPECT_EQ(usage.limit, 10);
    EXPECT_EQ(usage.requests_in_window, 5);
    EXPECT_DOUBLE_EQ(usage.utilization_percentage, 50.0);
    EXPECT_GT(usage.reset_time, std::chrono::system_clock::now());
}

TEST_F(RateLimiterTest, ActiveClientsTracking) {
    std::vector<std::string> client_ids = {"client_1", "client_2", "client_3"};
    
    for (const auto& id : client_ids) {
        ClientQuota quota = createTestQuota(id);
        rate_limiter->setClientQuota(id, quota);
        rate_limiter->recordUsage(id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    std::vector<std::string> active_clients = rate_limiter->getActiveClients();
    EXPECT_EQ(active_clients.size(), 3);
    
    for (const auto& id : client_ids) {
        EXPECT_THAT(active_clients, Contains(id));
    }
}

TEST_F(RateLimiterTest, TimeToReset) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Record usage
    rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 5);
    
    std::chrono::seconds time_to_reset = rate_limiter->getTimeToReset(client_id, LimitType::REQUESTS_PER_SECOND);
    
    EXPECT_GT(time_to_reset.count(), 0);
    EXPECT_LE(time_to_reset.count(), 1);  // Should reset within 1 second
}

TEST_F(RateLimiterTest, UtilizationRate) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Use 70% of quota
    for (int i = 0; i < 7; ++i) {
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    double utilization = rate_limiter->getUtilizationRate(client_id, LimitType::REQUESTS_PER_SECOND);
    EXPECT_DOUBLE_EQ(utilization, 70.0);
}

// Burst mode tests
TEST_F(RateLimiterTest, BurstModeAllowance) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    rate_limiter->enableBurstMode(client_id, LimitType::REQUESTS_PER_SECOND, 15);
    
    // Exhaust normal limit (10 requests)
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    // Should still allow burst requests (up to 15 total)
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    // Now should be blocked
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
}

// Priority-based scheduling tests
TEST_F(RateLimiterTest, PriorityScheduling) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    rate_limiter->setPriorityLevel(client_id, 8);  // High priority
    
    // Exhaust normal limit
    for (int i = 0; i < 10; ++i) {
        rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
    }
    
    // Normal check should fail
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
    
    // Priority check should allow some over-limit usage
    EXPECT_TRUE(rate_limiter->canScheduleWithPriority(client_id, LimitType::REQUESTS_PER_SECOND, 1));
}

// Concurrent job management tests
TEST_F(RateLimiterTest, ConcurrentJobManagement) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    EXPECT_EQ(rate_limiter->getCurrentConcurrentJobs(client_id), 0);
    EXPECT_TRUE(rate_limiter->canStartNewJob(client_id));
    
    // Start jobs
    std::vector<std::string> job_ids = {"job_1", "job_2", "job_3"};
    for (const auto& job_id : job_ids) {
        rate_limiter->markJobStarted(client_id, job_id);
    }
    
    EXPECT_EQ(rate_limiter->getCurrentConcurrentJobs(client_id), 3);
    EXPECT_TRUE(rate_limiter->canStartNewJob(client_id));  // Still under limit of 5
    
    // Complete a job
    rate_limiter->markJobCompleted(client_id, "job_1");
    EXPECT_EQ(rate_limiter->getCurrentConcurrentJobs(client_id), 2);
    
    // Start more jobs to reach limit
    rate_limiter->markJobStarted(client_id, "job_4");
    rate_limiter->markJobStarted(client_id, "job_5");
    rate_limiter->markJobStarted(client_id, "job_6");
    
    EXPECT_EQ(rate_limiter->getCurrentConcurrentJobs(client_id), 5);
    EXPECT_FALSE(rate_limiter->canStartNewJob(client_id));
}

// IP-based rate limiting tests
TEST_F(RateLimiterTest, IPBasedLimiting) {
    rate_limiter->setIPBasedLimiting(true);
    
    std::string ip = "192.168.1.100";
    
    // Should start with no limits
    EXPECT_TRUE(rate_limiter->checkIPLimit(ip, LimitType::REQUESTS_PER_SECOND, 1));
    
    // Record usage and check limits would need global IP limits to be set
    rate_limiter->recordIPUsage(ip, LimitType::REQUESTS_PER_SECOND, 1);
    EXPECT_TRUE(rate_limiter->checkIPLimit(ip, LimitType::REQUESTS_PER_SECOND, 1));
}

TEST_F(RateLimiterTest, IPBanning) {
    rate_limiter->setIPBasedLimiting(true);
    
    std::string ip = "192.168.1.100";
    
    EXPECT_FALSE(rate_limiter->isIPBanned(ip));
    EXPECT_TRUE(rate_limiter->checkIPLimit(ip, LimitType::REQUESTS_PER_SECOND, 1));
    
    // Ban IP for 2 seconds
    rate_limiter->banIP(ip, std::chrono::seconds(2));
    
    EXPECT_TRUE(rate_limiter->isIPBanned(ip));
    EXPECT_FALSE(rate_limiter->checkIPLimit(ip, LimitType::REQUESTS_PER_SECOND, 1));
    
    // Wait for ban to expire
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    EXPECT_FALSE(rate_limiter->isIPBanned(ip));
    EXPECT_TRUE(rate_limiter->checkIPLimit(ip, LimitType::REQUESTS_PER_SECOND, 1));
}

TEST_F(RateLimiterTest, IPUnbanning) {
    rate_limiter->setIPBasedLimiting(true);
    
    std::string ip = "192.168.1.100";
    
    rate_limiter->banIP(ip, std::chrono::seconds(3600));  // Ban for 1 hour
    EXPECT_TRUE(rate_limiter->isIPBanned(ip));
    
    rate_limiter->unbanIP(ip);
    EXPECT_FALSE(rate_limiter->isIPBanned(ip));
}

// Statistics and monitoring tests
TEST_F(RateLimiterTest, TopConsumersTracking) {
    std::vector<std::string> clients = {"heavy_user", "medium_user", "light_user"};
    std::vector<int> usage_amounts = {50, 20, 5};
    
    for (size_t i = 0; i < clients.size(); ++i) {
        ClientQuota quota = createTestQuota(clients[i]);
        quota.limits[0].limit = 100;  // Increase limit to allow more usage
        rate_limiter->setClientQuota(clients[i], quota);
        
        for (int j = 0; j < usage_amounts[i]; ++j) {
            rate_limiter->recordUsage(clients[i], LimitType::REQUESTS_PER_SECOND, 1);
        }
    }
    
    auto top_consumers = rate_limiter->getTopConsumers(LimitType::REQUESTS_PER_SECOND, 2);
    
    EXPECT_EQ(top_consumers.size(), 2);
    EXPECT_EQ(top_consumers["heavy_user"], 50);
    EXPECT_EQ(top_consumers["medium_user"], 20);
    EXPECT_EQ(top_consumers.find("light_user"), top_consumers.end());
}

TEST_F(RateLimiterTest, GlobalUsageStats) {
    std::vector<std::string> clients = {"client_1", "client_2", "client_3"};
    
    for (const auto& client : clients) {
        ClientQuota quota = createTestQuota(client);
        rate_limiter->setClientQuota(client, quota);
        
        rate_limiter->recordUsage(client, LimitType::REQUESTS_PER_SECOND, 5);
        rate_limiter->recordUsage(client, LimitType::REQUESTS_PER_MINUTE, 3);
    }
    
    auto global_stats = rate_limiter->getGlobalUsageStats();
    
    EXPECT_EQ(global_stats[LimitType::REQUESTS_PER_SECOND], 15);  // 5 * 3 clients
    EXPECT_EQ(global_stats[LimitType::REQUESTS_PER_MINUTE], 9);   // 3 * 3 clients
}

// Cleanup and maintenance tests
TEST_F(RateLimiterTest, ExpiredEntriesCleanup) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Add some usage that will expire
    rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 5);
    
    QuotaUsage before_cleanup = rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND);
    EXPECT_EQ(before_cleanup.current_usage, 5);
    
    // Wait for entries to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    rate_limiter->cleanupExpiredEntries();
    
    QuotaUsage after_cleanup = rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND);
    EXPECT_EQ(after_cleanup.current_usage, 0);
}

TEST_F(RateLimiterTest, ClientRemoval) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 3);
    EXPECT_EQ(rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND).current_usage, 3);
    
    rate_limiter->removeClient(client_id);
    
    // Usage should be reset (no longer tracked)
    EXPECT_EQ(rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND).current_usage, 0);
    
    auto active_clients = rate_limiter->getActiveClients();
    EXPECT_THAT(active_clients, Not(Contains(client_id)));
}

TEST_F(RateLimiterTest, UsageStatsReset) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 7);
    EXPECT_EQ(rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND).current_usage, 7);
    
    rate_limiter->resetUsageStats(client_id);
    EXPECT_EQ(rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND).current_usage, 0);
}

TEST_F(RateLimiterTest, GlobalStatsReset) {
    std::vector<std::string> clients = {"client_1", "client_2"};
    
    for (const auto& client : clients) {
        ClientQuota quota = createTestQuota(client);
        rate_limiter->setClientQuota(client, quota);
        rate_limiter->recordUsage(client, LimitType::REQUESTS_PER_SECOND, 5);
    }
    
    EXPECT_EQ(rate_limiter->getActiveClients().size(), 2);
    
    rate_limiter->resetUsageStats();  // Reset all
    EXPECT_EQ(rate_limiter->getActiveClients().size(), 0);
}

TEST_F(RateLimiterTest, MemoryUsageTracking) {
    size_t initial_memory = rate_limiter->getMemoryUsage();
    
    // Add many clients to increase memory usage
    for (int i = 0; i < 100; ++i) {
        std::string client = "client_" + std::to_string(i);
        ClientQuota quota = createTestQuota(client);
        rate_limiter->setClientQuota(client, quota);
        
        for (int j = 0; j < 10; ++j) {
            rate_limiter->recordUsage(client, LimitType::REQUESTS_PER_SECOND, 1);
        }
    }
    
    size_t final_memory = rate_limiter->getMemoryUsage();
    EXPECT_GT(final_memory, initial_memory);
}

// Concurrent access tests
TEST_F(RateLimiterTest, ConcurrentAccess) {
    ClientQuota quota = createTestQuota();
    quota.limits[0].limit = 1000;  // Higher limit for concurrent test
    rate_limiter->setClientQuota(client_id, quota);
    
    const int num_threads = 10;
    const int requests_per_thread = 50;
    std::atomic<int> successful_requests{0};
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < requests_per_thread; ++j) {
                if (rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1)) {
                    rate_limiter->recordUsage(client_id, LimitType::REQUESTS_PER_SECOND, 1);
                    successful_requests.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Should have processed most requests (allow some margin for race conditions)
    EXPECT_GE(successful_requests.load(), 400);
    EXPECT_LE(successful_requests.load(), 500);
    
    // Verify usage tracking is consistent
    QuotaUsage usage = rate_limiter->getUsage(client_id, LimitType::REQUESTS_PER_SECOND);
    EXPECT_EQ(usage.current_usage, successful_requests.load());
}

// TokenBucket tests
TEST_F(RateLimiterTest, TokenBucketBasicOperation) {
    TokenBucket bucket(10, 5, std::chrono::milliseconds(100));  // 10 capacity, 5 tokens per 100ms
    
    // Should start with full capacity
    EXPECT_EQ(bucket.availableTokens(), 10);
    
    // Consume some tokens
    EXPECT_TRUE(bucket.consume(3));
    EXPECT_EQ(bucket.availableTokens(), 7);
    
    // Try to consume more than available
    EXPECT_FALSE(bucket.consume(8));
    EXPECT_EQ(bucket.availableTokens(), 7);  // Should remain unchanged
    
    // Consume remaining tokens
    EXPECT_TRUE(bucket.consume(7));
    EXPECT_EQ(bucket.availableTokens(), 0);
}

TEST_F(RateLimiterTest, TokenBucketRefill) {
    TokenBucket bucket(10, 5, std::chrono::milliseconds(100));
    
    // Consume all tokens
    EXPECT_TRUE(bucket.consume(10));
    EXPECT_EQ(bucket.availableTokens(), 0);
    
    // Wait for refill
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    bucket.refill();
    
    // Should have refilled 5 tokens
    EXPECT_EQ(bucket.availableTokens(), 5);
}

TEST_F(RateLimiterTest, TokenBucketReset) {
    TokenBucket bucket(10, 2, std::chrono::milliseconds(100));
    
    bucket.consume(8);
    EXPECT_EQ(bucket.availableTokens(), 2);
    
    bucket.reset();
    EXPECT_EQ(bucket.availableTokens(), 10);
}

// SlidingWindowLimiter tests
TEST_F(RateLimiterTest, SlidingWindowLimiterBasicOperation) {
    SlidingWindowLimiter limiter(5, std::chrono::seconds(1));  // 5 requests per second
    
    // Should allow requests within limit
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.allowRequest());
    }
    
    // Should reject additional requests
    EXPECT_FALSE(limiter.allowRequest());
    
    EXPECT_EQ(limiter.getCurrentRequestCount(), 5);
}

TEST_F(RateLimiterTest, SlidingWindowLimiterRecovery) {
    SlidingWindowLimiter limiter(3, std::chrono::seconds(1));
    
    // Exhaust limit
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(limiter.allowRequest());
    }
    EXPECT_FALSE(limiter.allowRequest());
    
    // Wait for window to slide
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    
    // Should allow requests again
    EXPECT_TRUE(limiter.allowRequest());
    EXPECT_EQ(limiter.getCurrentRequestCount(), 1);
}

TEST_F(RateLimiterTest, SlidingWindowTimeToNextSlot) {
    SlidingWindowLimiter limiter(2, std::chrono::seconds(2));
    
    limiter.allowRequest();
    limiter.allowRequest();
    
    // Should be blocked now
    EXPECT_FALSE(limiter.allowRequest());
    
    // Time to next slot should be approximately the window size
    std::chrono::seconds time_to_next = limiter.getTimeToNextSlot();
    EXPECT_GT(time_to_next.count(), 0);
    EXPECT_LE(time_to_next.count(), 2);
}

// Edge cases and error handling
TEST_F(RateLimiterTest, NonExistentClient) {
    std::string non_existent = "does_not_exist";
    
    // Should work gracefully with default limits
    EXPECT_TRUE(rate_limiter->checkLimit(non_existent, LimitType::REQUESTS_PER_SECOND, 1));
    
    QuotaUsage usage = rate_limiter->getUsage(non_existent, LimitType::REQUESTS_PER_SECOND);
    EXPECT_EQ(usage.current_usage, 0);
    
    EXPECT_EQ(rate_limiter->getCurrentConcurrentJobs(non_existent), 0);
}

TEST_F(RateLimiterTest, ZeroLimits) {
    ClientQuota quota = createTestQuota();
    quota.limits[0].limit = 0;  // Zero limit
    rate_limiter->setClientQuota(client_id, quota);
    
    // Should reject all requests with zero limit
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
}

TEST_F(RateLimiterTest, LargeBurstRequests) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Try to acquire large number of requests at once
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 50));
    
    // Should still allow normal requests
    EXPECT_TRUE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 1));
}

TEST_F(RateLimiterTest, UpdateExistingLimit) {
    ClientQuota quota = createTestQuota();
    rate_limiter->setClientQuota(client_id, quota);
    
    // Initial limit is 10 per second
    EXPECT_TRUE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 10));
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 11));
    
    // Update limit to 20
    rate_limiter->updateLimit(client_id, LimitType::REQUESTS_PER_SECOND, 20);
    
    // Should now allow up to 20
    EXPECT_TRUE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 20));
    EXPECT_FALSE(rate_limiter->checkLimit(client_id, LimitType::REQUESTS_PER_SECOND, 21));
}

TEST_F(RateLimiterTest, GlobalLimitFallback) {
    // Set global limit
    rate_limiter->setGlobalLimit(LimitType::REQUESTS_PER_SECOND, 50);
    
    // Client without specific quota should use global limit
    std::string new_client = "global_limit_client";
    
    EXPECT_TRUE(rate_limiter->checkLimit(new_client, LimitType::REQUESTS_PER_SECOND, 50));
    EXPECT_FALSE(rate_limiter->checkLimit(new_client, LimitType::REQUESTS_PER_SECOND, 51));
}