#include "rate_limiter.h"
#include <unordered_map>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <algorithm>
#include <numeric>

namespace sandrun {

class RateLimiter::Impl {
public:
    struct ClientState {
        ClientQuota quota;
        std::unordered_map<LimitType, std::deque<std::chrono::system_clock::time_point>> request_times;
        std::unordered_map<LimitType, size_t> current_resource_usage;
        std::unordered_set<std::string> active_jobs;
        std::chrono::system_clock::time_point last_cleanup;
    };
    
    mutable std::shared_mutex clients_mutex_;
    std::unordered_map<std::string, ClientState> clients_;
    
    mutable std::shared_mutex ip_mutex_;
    std::unordered_map<std::string, ClientState> ip_clients_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> banned_ips_;
    
    std::unordered_map<LimitType, RateLimit> global_limits_;
    std::atomic<bool> ip_based_limiting_{false};
    
    // Helper methods
    bool checkRateLimit(ClientState& client_state, LimitType type, size_t amount) {
        auto now = std::chrono::system_clock::now();
        auto& rate_limit = findRateLimit(client_state.quota, type);
        auto& request_times = client_state.request_times[type];
        
        // Clean old requests outside the window
        auto window_start = now - rate_limit.window_size;
        while (!request_times.empty() && request_times.front() < window_start) {
            request_times.pop_front();
        }
        
        // Check if adding this request would exceed the limit
        if (request_times.size() + amount > rate_limit.limit) {
            // Check burst allowance
            if (rate_limit.burst_limit > 0) {
                auto burst_window_start = now - rate_limit.burst_window;
                size_t requests_in_burst = std::count_if(request_times.begin(), request_times.end(),
                    [burst_window_start](const auto& time) { return time >= burst_window_start; });
                
                if (requests_in_burst + amount <= rate_limit.burst_limit) {
                    return true;  // Allow burst
                }
            }
            return false;
        }
        
        return true;
    }
    
    void recordRequest(ClientState& client_state, LimitType type, size_t amount) {
        auto now = std::chrono::system_clock::now();
        auto& request_times = client_state.request_times[type];
        
        for (size_t i = 0; i < amount; ++i) {
            request_times.push_back(now);
        }
        
        updateUsage(client_state, type);
    }
    
    void updateUsage(ClientState& client_state, LimitType type) {
        auto now = std::chrono::system_clock::now();
        auto& rate_limit = findRateLimit(client_state.quota, type);
        auto& request_times = client_state.request_times[type];
        auto& usage = client_state.quota.usage[type];
        
        // Clean old requests
        auto window_start = now - rate_limit.window_size;
        while (!request_times.empty() && request_times.front() < window_start) {
            request_times.pop_front();
        }
        
        usage.current_usage = request_times.size();
        usage.limit = rate_limit.limit;
        usage.last_request = now;
        usage.requests_in_window = request_times.size();
        usage.utilization_percentage = (double)usage.current_usage / usage.limit * 100.0;
        
        // Calculate reset time
        if (!request_times.empty()) {
            usage.reset_time = request_times.front() + rate_limit.window_size;
        } else {
            usage.reset_time = now + rate_limit.window_size;
        }
    }
    
    const RateLimit& findRateLimit(const ClientQuota& quota, LimitType type) const {
        auto it = std::find_if(quota.limits.begin(), quota.limits.end(),
            [type](const RateLimit& limit) { return limit.type == type; });
        
        if (it != quota.limits.end()) {
            return *it;
        }
        
        // Fall back to global limit
        auto global_it = global_limits_.find(type);
        if (global_it != global_limits_.end()) {
            return global_it->second;
        }
        
        // Default limit
        static RateLimit default_limit{type, 100, std::chrono::seconds(60)};
        return default_limit;
    }
    
    void cleanupOldEntries(ClientState& client_state) {
        auto now = std::chrono::system_clock::now();
        
        for (auto& [type, request_times] : client_state.request_times) {
            auto& rate_limit = findRateLimit(client_state.quota, type);
            auto window_start = now - rate_limit.window_size;
            
            while (!request_times.empty() && request_times.front() < window_start) {
                request_times.pop_front();
            }
        }
        
        client_state.last_cleanup = now;
    }
};

// TokenBucket implementation
class TokenBucket::Impl {
public:
    size_t capacity_;
    size_t available_tokens_;
    size_t refill_rate_;
    std::chrono::milliseconds refill_interval_;
    std::chrono::system_clock::time_point last_refill_;
    mutable std::mutex mutex_;
    
    Impl(size_t capacity, size_t refill_rate, std::chrono::milliseconds refill_interval)
        : capacity_(capacity), available_tokens_(capacity), refill_rate_(refill_rate),
          refill_interval_(refill_interval), last_refill_(std::chrono::system_clock::now()) {}
    
    void refillTokens() {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refill_);
        
        if (elapsed >= refill_interval_) {
            size_t intervals = elapsed.count() / refill_interval_.count();
            size_t tokens_to_add = intervals * refill_rate_;
            
            available_tokens_ = std::min(capacity_, available_tokens_ + tokens_to_add);
            last_refill_ = now;
        }
    }
};

// SlidingWindowLimiter implementation  
class SlidingWindowLimiter::Impl {
public:
    size_t max_requests_;
    std::chrono::seconds window_size_;
    std::deque<std::chrono::system_clock::time_point> request_times_;
    mutable std::mutex mutex_;
    
    Impl(size_t max_requests, std::chrono::seconds window_size)
        : max_requests_(max_requests), window_size_(window_size) {}
    
    void cleanupOldRequests() {
        auto now = std::chrono::system_clock::now();
        auto window_start = now - window_size_;
        
        while (!request_times_.empty() && request_times_.front() < window_start) {
            request_times_.pop_front();
        }
    }
};

// Main RateLimiter implementation
RateLimiter::RateLimiter() : pImpl(std::make_unique<Impl>()) {}

RateLimiter::~RateLimiter() = default;

void RateLimiter::setClientQuota(const std::string& client_id, const ClientQuota& quota) {
    std::unique_lock lock(pImpl->clients_mutex_);
    pImpl->clients_[client_id].quota = quota;
}

void RateLimiter::updateLimit(const std::string& client_id, LimitType type, size_t new_limit) {
    std::unique_lock lock(pImpl->clients_mutex_);
    auto& client = pImpl->clients_[client_id];
    
    auto it = std::find_if(client.quota.limits.begin(), client.quota.limits.end(),
        [type](const RateLimit& limit) { return limit.type == type; });
    
    if (it != client.quota.limits.end()) {
        it->limit = new_limit;
    } else {
        client.quota.limits.push_back({type, new_limit, std::chrono::seconds(60)});
    }
}

void RateLimiter::removeClient(const std::string& client_id) {
    std::unique_lock lock(pImpl->clients_mutex_);
    pImpl->clients_.erase(client_id);
}

void RateLimiter::setGlobalLimit(LimitType type, size_t limit) {
    pImpl->global_limits_[type] = {type, limit, std::chrono::seconds(60)};
}

bool RateLimiter::checkLimit(const std::string& client_id, LimitType type, size_t requested_amount) {
    std::shared_lock lock(pImpl->clients_mutex_);
    auto it = pImpl->clients_.find(client_id);
    
    if (it == pImpl->clients_.end()) {
        // Create default client
        lock.unlock();
        std::unique_lock write_lock(pImpl->clients_mutex_);
        auto& client_state = pImpl->clients_[client_id];
        client_state.quota.client_id = client_id;
        return pImpl->checkRateLimit(client_state, type, requested_amount);
    }
    
    return pImpl->checkRateLimit(const_cast<Impl::ClientState&>(it->second), type, requested_amount);
}

void RateLimiter::recordUsage(const std::string& client_id, LimitType type, size_t amount) {
    std::unique_lock lock(pImpl->clients_mutex_);
    auto& client_state = pImpl->clients_[client_id];
    
    pImpl->recordRequest(client_state, type, amount);
    
    // Periodic cleanup
    auto now = std::chrono::system_clock::now();
    if (now - client_state.last_cleanup > std::chrono::minutes(5)) {
        pImpl->cleanupOldEntries(client_state);
    }
}

bool RateLimiter::acquireResource(const std::string& client_id, LimitType type, size_t amount) {
    if (!checkLimit(client_id, type, amount)) {
        auto usage = getUsage(client_id, type);
        auto retry_after = getTimeToReset(client_id, type);
        throw RateLimitExceededException(
            "Rate limit exceeded for " + client_id + " on " + std::to_string(static_cast<int>(type)),
            type, retry_after);
    }
    
    recordUsage(client_id, type, amount);
    
    // For resource-based limits, track current usage
    if (type == LimitType::MEMORY_USAGE || type == LimitType::GPU_USAGE || 
        type == LimitType::CONCURRENT_JOBS) {
        std::unique_lock lock(pImpl->clients_mutex_);
        pImpl->clients_[client_id].current_resource_usage[type] += amount;
    }
    
    return true;
}

void RateLimiter::releaseResource(const std::string& client_id, LimitType type, size_t amount) {
    std::unique_lock lock(pImpl->clients_mutex_);
    auto& client_state = pImpl->clients_[client_id];
    
    if (client_state.current_resource_usage[type] >= amount) {
        client_state.current_resource_usage[type] -= amount;
    } else {
        client_state.current_resource_usage[type] = 0;
    }
}

QuotaUsage RateLimiter::getUsage(const std::string& client_id, LimitType type) const {
    std::shared_lock lock(pImpl->clients_mutex_);
    auto it = pImpl->clients_.find(client_id);
    
    if (it == pImpl->clients_.end()) {
        return QuotaUsage{};
    }
    
    auto usage_it = it->second.quota.usage.find(type);
    if (usage_it != it->second.quota.usage.end()) {
        return usage_it->second;
    }
    
    return QuotaUsage{};
}

std::vector<std::string> RateLimiter::getActiveClients() const {
    std::shared_lock lock(pImpl->clients_mutex_);
    std::vector<std::string> clients;
    
    for (const auto& [client_id, _] : pImpl->clients_) {
        clients.push_back(client_id);
    }
    
    return clients;
}

std::chrono::seconds RateLimiter::getTimeToReset(const std::string& client_id, LimitType type) const {
    auto usage = getUsage(client_id, type);
    auto now = std::chrono::system_clock::now();
    
    if (usage.reset_time > now) {
        return std::chrono::duration_cast<std::chrono::seconds>(usage.reset_time - now);
    }
    
    return std::chrono::seconds(0);
}

double RateLimiter::getUtilizationRate(const std::string& client_id, LimitType type) const {
    auto usage = getUsage(client_id, type);
    return usage.utilization_percentage;
}

void RateLimiter::enableBurstMode(const std::string& client_id, LimitType type, size_t burst_capacity) {
    std::unique_lock lock(pImpl->clients_mutex_);
    auto& client_state = pImpl->clients_[client_id];
    
    auto it = std::find_if(client_state.quota.limits.begin(), client_state.quota.limits.end(),
        [type](RateLimit& limit) { return limit.type == type; });
    
    if (it != client_state.quota.limits.end()) {
        it->burst_limit = burst_capacity;
        it->burst_window = std::chrono::seconds(10);  // Default burst window
    }
}

void RateLimiter::setPriorityLevel(const std::string& client_id, int priority) {
    std::unique_lock lock(pImpl->clients_mutex_);
    pImpl->clients_[client_id].quota.priority_level = priority;
}

bool RateLimiter::canScheduleWithPriority(const std::string& client_id, LimitType type, size_t amount) {
    std::shared_lock lock(pImpl->clients_mutex_);
    auto it = pImpl->clients_.find(client_id);
    
    if (it == pImpl->clients_.end()) {
        return checkLimit(client_id, type, amount);
    }
    
    // For high-priority clients, allow some over-limit usage
    if (it->second.quota.priority_level > 5) {
        auto& rate_limit = pImpl->findRateLimit(it->second.quota, type);
        auto extended_limit = rate_limit.limit * 1.2;  // 20% over-limit for high priority
        
        // Check against extended limit
        auto usage = getUsage(client_id, type);
        return usage.current_usage + amount <= extended_limit;
    }
    
    return checkLimit(client_id, type, amount);
}

// Concurrent job management
size_t RateLimiter::getCurrentConcurrentJobs(const std::string& client_id) const {
    std::shared_lock lock(pImpl->clients_mutex_);
    auto it = pImpl->clients_.find(client_id);
    
    if (it != pImpl->clients_.end()) {
        return it->second.active_jobs.size();
    }
    
    return 0;
}

size_t RateLimiter::getMaxConcurrentJobs(const std::string& client_id) const {
    auto usage = getUsage(client_id, LimitType::CONCURRENT_JOBS);
    return usage.limit > 0 ? usage.limit : 10;  // Default of 10 concurrent jobs
}

bool RateLimiter::canStartNewJob(const std::string& client_id) const {
    return getCurrentConcurrentJobs(client_id) < getMaxConcurrentJobs(client_id);
}

void RateLimiter::markJobStarted(const std::string& client_id, const std::string& job_id) {
    std::unique_lock lock(pImpl->clients_mutex_);
    pImpl->clients_[client_id].active_jobs.insert(job_id);
}

void RateLimiter::markJobCompleted(const std::string& client_id, const std::string& job_id) {
    std::unique_lock lock(pImpl->clients_mutex_);
    auto it = pImpl->clients_.find(client_id);
    if (it != pImpl->clients_.end()) {
        it->second.active_jobs.erase(job_id);
    }
}

// IP-based rate limiting
void RateLimiter::setIPBasedLimiting(bool enable) {
    pImpl->ip_based_limiting_ = enable;
}

bool RateLimiter::checkIPLimit(const std::string& ip_address, LimitType type, size_t amount) {
    if (!pImpl->ip_based_limiting_) {
        return true;
    }
    
    // Check if IP is banned
    if (isIPBanned(ip_address)) {
        return false;
    }
    
    std::shared_lock lock(pImpl->ip_mutex_);
    auto it = pImpl->ip_clients_.find(ip_address);
    
    if (it == pImpl->ip_clients_.end()) {
        return true;  // No limits set for this IP yet
    }
    
    return pImpl->checkRateLimit(const_cast<Impl::ClientState&>(it->second), type, amount);
}

void RateLimiter::recordIPUsage(const std::string& ip_address, LimitType type, size_t amount) {
    if (!pImpl->ip_based_limiting_) {
        return;
    }
    
    std::unique_lock lock(pImpl->ip_mutex_);
    auto& client_state = pImpl->ip_clients_[ip_address];
    client_state.quota.client_id = ip_address;
    
    pImpl->recordRequest(client_state, type, amount);
}

void RateLimiter::banIP(const std::string& ip_address, std::chrono::seconds duration) {
    std::unique_lock lock(pImpl->ip_mutex_);
    pImpl->banned_ips_[ip_address] = std::chrono::system_clock::now() + duration;
}

void RateLimiter::unbanIP(const std::string& ip_address) {
    std::unique_lock lock(pImpl->ip_mutex_);
    pImpl->banned_ips_.erase(ip_address);
}

bool RateLimiter::isIPBanned(const std::string& ip_address) const {
    std::shared_lock lock(pImpl->ip_mutex_);
    auto it = pImpl->banned_ips_.find(ip_address);
    
    if (it != pImpl->banned_ips_.end()) {
        auto now = std::chrono::system_clock::now();
        if (it->second > now) {
            return true;  // Still banned
        } else {
            // Ban expired, remove it
            lock.unlock();
            std::unique_lock write_lock(pImpl->ip_mutex_);
            pImpl->banned_ips_.erase(it);
        }
    }
    
    return false;
}

// Statistics and monitoring
std::unordered_map<std::string, size_t> RateLimiter::getTopConsumers(LimitType type, size_t count) const {
    std::shared_lock lock(pImpl->clients_mutex_);
    std::vector<std::pair<std::string, size_t>> consumers;
    
    for (const auto& [client_id, client_state] : pImpl->clients_) {
        auto usage_it = client_state.quota.usage.find(type);
        if (usage_it != client_state.quota.usage.end()) {
            consumers.emplace_back(client_id, usage_it->second.current_usage);
        }
    }
    
    std::sort(consumers.begin(), consumers.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::unordered_map<std::string, size_t> result;
    for (size_t i = 0; i < std::min(count, consumers.size()); ++i) {
        result[consumers[i].first] = consumers[i].second;
    }
    
    return result;
}

std::unordered_map<LimitType, size_t> RateLimiter::getGlobalUsageStats() const {
    std::shared_lock lock(pImpl->clients_mutex_);
    std::unordered_map<LimitType, size_t> stats;
    
    for (const auto& [_, client_state] : pImpl->clients_) {
        for (const auto& [type, usage] : client_state.quota.usage) {
            stats[type] += usage.current_usage;
        }
    }
    
    return stats;
}

void RateLimiter::resetUsageStats(const std::string& client_id) {
    std::unique_lock lock(pImpl->clients_mutex_);
    
    if (client_id.empty()) {
        // Reset all clients
        pImpl->clients_.clear();
    } else {
        // Reset specific client
        auto it = pImpl->clients_.find(client_id);
        if (it != pImpl->clients_.end()) {
            it->second.request_times.clear();
            it->second.quota.usage.clear();
            it->second.current_resource_usage.clear();
        }
    }
}

void RateLimiter::cleanupExpiredEntries() {
    std::unique_lock lock(pImpl->clients_mutex_);
    
    for (auto& [client_id, client_state] : pImpl->clients_) {
        pImpl->cleanupOldEntries(client_state);
    }
    
    // Cleanup IP bans
    std::unique_lock ip_lock(pImpl->ip_mutex_);
    auto now = std::chrono::system_clock::now();
    
    auto it = pImpl->banned_ips_.begin();
    while (it != pImpl->banned_ips_.end()) {
        if (it->second <= now) {
            it = pImpl->banned_ips_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t RateLimiter::getMemoryUsage() const {
    std::shared_lock lock(pImpl->clients_mutex_);
    size_t total_size = sizeof(*this) + sizeof(*pImpl);
    
    for (const auto& [client_id, client_state] : pImpl->clients_) {
        total_size += client_id.size() + sizeof(client_state);
        for (const auto& [type, times] : client_state.request_times) {
            total_size += sizeof(type) + times.size() * sizeof(std::chrono::system_clock::time_point);
        }
    }
    
    return total_size;
}

// TokenBucket implementation
TokenBucket::TokenBucket(size_t capacity, size_t refill_rate, std::chrono::milliseconds refill_interval)
    : pImpl(std::make_unique<Impl>(capacity, refill_rate, refill_interval)) {}

TokenBucket::~TokenBucket() = default;

bool TokenBucket::consume(size_t tokens) {
    std::lock_guard lock(pImpl->mutex_);
    pImpl->refillTokens();
    
    if (pImpl->available_tokens_ >= tokens) {
        pImpl->available_tokens_ -= tokens;
        return true;
    }
    
    return false;
}

size_t TokenBucket::availableTokens() const {
    std::lock_guard lock(pImpl->mutex_);
    const_cast<Impl*>(pImpl.get())->refillTokens();
    return pImpl->available_tokens_;
}

void TokenBucket::refill() {
    std::lock_guard lock(pImpl->mutex_);
    pImpl->refillTokens();
}

void TokenBucket::reset() {
    std::lock_guard lock(pImpl->mutex_);
    pImpl->available_tokens_ = pImpl->capacity_;
    pImpl->last_refill_ = std::chrono::system_clock::now();
}

// SlidingWindowLimiter implementation
SlidingWindowLimiter::SlidingWindowLimiter(size_t max_requests, std::chrono::seconds window_size)
    : pImpl(std::make_unique<Impl>(max_requests, window_size)) {}

SlidingWindowLimiter::~SlidingWindowLimiter() = default;

bool SlidingWindowLimiter::allowRequest() {
    std::lock_guard lock(pImpl->mutex_);
    pImpl->cleanupOldRequests();
    
    if (pImpl->request_times_.size() < pImpl->max_requests_) {
        pImpl->request_times_.push_back(std::chrono::system_clock::now());
        return true;
    }
    
    return false;
}

size_t SlidingWindowLimiter::getCurrentRequestCount() const {
    std::lock_guard lock(pImpl->mutex_);
    const_cast<Impl*>(pImpl.get())->cleanupOldRequests();
    return pImpl->request_times_.size();
}

std::chrono::seconds SlidingWindowLimiter::getTimeToNextSlot() const {
    std::lock_guard lock(pImpl->mutex_);
    
    if (pImpl->request_times_.empty()) {
        return std::chrono::seconds(0);
    }
    
    auto oldest_request = pImpl->request_times_.front();
    auto next_slot = oldest_request + pImpl->window_size_;
    auto now = std::chrono::system_clock::now();
    
    if (next_slot > now) {
        return std::chrono::duration_cast<std::chrono::seconds>(next_slot - now);
    }
    
    return std::chrono::seconds(0);
}

void SlidingWindowLimiter::cleanup() {
    std::lock_guard lock(pImpl->mutex_);
    pImpl->cleanupOldRequests();
}

} // namespace sandrun