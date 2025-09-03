#pragma once

#include <string>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <chrono>

namespace sandrun {

// Rate limiter for IP-based CPU quota management
class RateLimiter {
public:
    struct Config {
        double cpu_seconds_per_minute;
        int max_concurrent_jobs;
        int max_jobs_per_hour;
        int cleanup_after_minutes;
        
        Config() : 
            cpu_seconds_per_minute(10.0),
            max_concurrent_jobs(2),
            max_jobs_per_hour(20),
            cleanup_after_minutes(60) {}
    };
    
    struct QuotaInfo {
        double cpu_seconds_used = 0;
        double cpu_seconds_available = 0;
        int active_jobs = 0;
        int jobs_this_hour = 0;
        bool can_submit = false;
        std::string reason;
    };
    
    explicit RateLimiter(const Config& config = Config());
    
    // Check if IP can submit a new job
    QuotaInfo check_quota(const std::string& ip);
    
    // Register job start
    bool register_job_start(const std::string& ip, const std::string& job_id);
    
    // Register job completion with CPU usage
    void register_job_end(const std::string& ip, const std::string& job_id, double cpu_seconds);
    
    // Get remaining CPU seconds for IP
    double get_available_cpu_seconds(const std::string& ip);
    
    // Periodic cleanup of old entries
    void cleanup_old_entries();
    
private:
    Config config_;
    mutable std::mutex mutex_;
    
    struct IpState {
        std::deque<std::pair<std::chrono::steady_clock::time_point, double>> cpu_usage_history;
        std::set<std::string> active_jobs;
        std::deque<std::chrono::steady_clock::time_point> job_submissions;
        std::chrono::steady_clock::time_point last_seen;
    };
    
    std::map<std::string, IpState> ip_states_;
    
    void cleanup_ip_history(IpState& state, const std::chrono::steady_clock::time_point& now);
};

} // namespace sandrun