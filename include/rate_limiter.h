#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <atomic>
#include <unordered_map>

namespace sandrun {

enum class LimitType {
    REQUESTS_PER_SECOND,
    REQUESTS_PER_MINUTE,
    REQUESTS_PER_HOUR,
    REQUESTS_PER_DAY,
    CONCURRENT_JOBS,
    MEMORY_USAGE,
    GPU_USAGE,
    BANDWIDTH
};

struct RateLimit {
    LimitType type;
    size_t limit;                                    // Maximum allowed
    std::chrono::seconds window_size{60};            // Time window for rate limiting
    std::chrono::seconds burst_window{5};            // Burst allowance window
    size_t burst_limit = 0;                         // Burst capacity (0 = no burst)
};

struct QuotaUsage {
    size_t current_usage = 0;
    size_t limit = 0;
    std::chrono::system_clock::time_point reset_time;
    std::chrono::system_clock::time_point last_request;
    size_t requests_in_window = 0;
    double utilization_percentage = 0.0;
};

struct ClientQuota {
    std::string client_id;
    std::vector<RateLimit> limits;
    std::unordered_map<LimitType, QuotaUsage> usage;
    bool is_premium = false;
    int priority_level = 0;  // Higher number = higher priority
};

class RateLimitExceededException : public std::runtime_error {
public:
    explicit RateLimitExceededException(const std::string& message, LimitType type, std::chrono::seconds retry_after)
        : std::runtime_error(message), limit_type_(type), retry_after_(retry_after) {}
    
    LimitType getLimitType() const { return limit_type_; }
    std::chrono::seconds getRetryAfter() const { return retry_after_; }
    
private:
    LimitType limit_type_;
    std::chrono::seconds retry_after_;
};

class RateLimiter {
public:
    RateLimiter();
    ~RateLimiter();

    // Configuration methods
    void setClientQuota(const std::string& client_id, const ClientQuota& quota);
    void updateLimit(const std::string& client_id, LimitType type, size_t new_limit);
    void removeClient(const std::string& client_id);
    void setGlobalLimit(LimitType type, size_t limit);
    
    // Rate limiting operations
    bool checkLimit(const std::string& client_id, LimitType type, size_t requested_amount = 1);
    void recordUsage(const std::string& client_id, LimitType type, size_t amount = 1);
    bool acquireResource(const std::string& client_id, LimitType type, size_t amount = 1);
    void releaseResource(const std::string& client_id, LimitType type, size_t amount = 1);
    
    // Query methods
    QuotaUsage getUsage(const std::string& client_id, LimitType type) const;
    std::vector<std::string> getActiveClients() const;
    std::chrono::seconds getTimeToReset(const std::string& client_id, LimitType type) const;
    double getUtilizationRate(const std::string& client_id, LimitType type) const;
    
    // Advanced features
    void enableBurstMode(const std::string& client_id, LimitType type, size_t burst_capacity);
    void setPriorityLevel(const std::string& client_id, int priority);
    bool canScheduleWithPriority(const std::string& client_id, LimitType type, size_t amount = 1);
    
    // Concurrent job management
    size_t getCurrentConcurrentJobs(const std::string& client_id) const;
    size_t getMaxConcurrentJobs(const std::string& client_id) const;
    bool canStartNewJob(const std::string& client_id) const;
    void markJobStarted(const std::string& client_id, const std::string& job_id);
    void markJobCompleted(const std::string& client_id, const std::string& job_id);
    
    // IP-based rate limiting
    void setIPBasedLimiting(bool enable = true);
    bool checkIPLimit(const std::string& ip_address, LimitType type, size_t amount = 1);
    void recordIPUsage(const std::string& ip_address, LimitType type, size_t amount = 1);
    void banIP(const std::string& ip_address, std::chrono::seconds duration);
    void unbanIP(const std::string& ip_address);
    bool isIPBanned(const std::string& ip_address) const;
    
    // Statistics and monitoring
    std::unordered_map<std::string, size_t> getTopConsumers(LimitType type, size_t count = 10) const;
    std::unordered_map<LimitType, size_t> getGlobalUsageStats() const;
    void resetUsageStats(const std::string& client_id = "");
    
    // Cleanup and maintenance
    void cleanupExpiredEntries();
    void resetWindow(const std::string& client_id, LimitType type);
    size_t getMemoryUsage() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Token bucket implementation for smooth rate limiting
class TokenBucket {
public:
    TokenBucket(size_t capacity, size_t refill_rate, std::chrono::milliseconds refill_interval);
    ~TokenBucket();
    
    bool consume(size_t tokens = 1);
    size_t availableTokens() const;
    void refill();
    void reset();
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Sliding window rate limiter for precise time-based limiting
class SlidingWindowLimiter {
public:
    SlidingWindowLimiter(size_t max_requests, std::chrono::seconds window_size);
    ~SlidingWindowLimiter();
    
    bool allowRequest();
    size_t getCurrentRequestCount() const;
    std::chrono::seconds getTimeToNextSlot() const;
    void cleanup();
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace sandrun