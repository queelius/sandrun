#include "rate_limiter.h"
#include <algorithm>

namespace sandrun {

RateLimiter::RateLimiter(const Config& config) : config_(config) {}

RateLimiter::QuotaInfo RateLimiter::check_quota(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    QuotaInfo info;
    auto now = std::chrono::steady_clock::now();
    
    // Get or create IP state
    auto& state = ip_states_[ip];
    state.last_seen = now;
    
    // Clean up old entries
    cleanup_ip_history(state, now);
    
    // Check concurrent jobs
    info.active_jobs = state.active_jobs.size();
    if (info.active_jobs >= config_.max_concurrent_jobs) {
        info.can_submit = false;
        info.reason = "Max concurrent jobs reached (" + 
                     std::to_string(config_.max_concurrent_jobs) + ")";
        return info;
    }
    
    // Check hourly job limit
    auto hour_ago = now - std::chrono::hours(1);
    info.jobs_this_hour = std::count_if(
        state.job_submissions.begin(), 
        state.job_submissions.end(),
        [hour_ago](const auto& time) { return time > hour_ago; }
    );
    
    if (info.jobs_this_hour >= config_.max_jobs_per_hour) {
        info.can_submit = false;
        info.reason = "Max jobs per hour reached (" + 
                     std::to_string(config_.max_jobs_per_hour) + ")";
        return info;
    }
    
    // Check CPU quota
    auto minute_ago = now - std::chrono::minutes(1);
    info.cpu_seconds_used = 0;
    for (const auto& [time, cpu] : state.cpu_usage_history) {
        if (time > minute_ago) {
            info.cpu_seconds_used += cpu;
        }
    }
    
    info.cpu_seconds_available = std::max(0.0, 
        config_.cpu_seconds_per_minute - info.cpu_seconds_used);
    
    if (info.cpu_seconds_available <= 0) {
        info.can_submit = false;
        
        // Calculate when quota will be available
        if (!state.cpu_usage_history.empty()) {
            auto oldest_usage_time = state.cpu_usage_history.front().first;
            auto wait_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                oldest_usage_time + std::chrono::minutes(1) - now
            ).count();
            info.reason = "CPU quota exhausted, wait " + 
                         std::to_string(wait_seconds) + " seconds";
        } else {
            info.reason = "CPU quota exhausted";
        }
        return info;
    }
    
    info.can_submit = true;
    return info;
}

bool RateLimiter::register_job_start(const std::string& ip, const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& state = ip_states_[ip];
    auto now = std::chrono::steady_clock::now();
    state.last_seen = now;
    
    // Check if we can accept this job
    if (state.active_jobs.size() >= config_.max_concurrent_jobs) {
        return false;
    }
    
    state.active_jobs.insert(job_id);
    state.job_submissions.push_back(now);
    
    // Clean up old submission history
    auto hour_ago = now - std::chrono::hours(1);
    while (!state.job_submissions.empty() && state.job_submissions.front() < hour_ago) {
        state.job_submissions.pop_front();
    }
    
    return true;
}

void RateLimiter::register_job_end(const std::string& ip, const std::string& job_id, double cpu_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ip_states_.find(ip);
    if (it == ip_states_.end()) return;
    
    auto& state = it->second;
    auto now = std::chrono::steady_clock::now();
    state.last_seen = now;
    
    // Remove from active jobs
    state.active_jobs.erase(job_id);
    
    // Record CPU usage
    state.cpu_usage_history.push_back({now, cpu_seconds});
    
    // Clean up old history
    cleanup_ip_history(state, now);
}

double RateLimiter::get_available_cpu_seconds(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ip_states_.find(ip);
    if (it == ip_states_.end()) {
        return config_.cpu_seconds_per_minute;
    }
    
    auto& state = it->second;
    auto now = std::chrono::steady_clock::now();
    auto minute_ago = now - std::chrono::minutes(1);
    
    double used = 0;
    for (const auto& [time, cpu] : state.cpu_usage_history) {
        if (time > minute_ago) {
            used += cpu;
        }
    }
    
    return std::max(0.0, config_.cpu_seconds_per_minute - used);
}

void RateLimiter::cleanup_old_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::minutes(config_.cleanup_after_minutes);
    
    auto it = ip_states_.begin();
    while (it != ip_states_.end()) {
        if (it->second.last_seen < cutoff && it->second.active_jobs.empty()) {
            it = ip_states_.erase(it);
        } else {
            cleanup_ip_history(it->second, now);
            ++it;
        }
    }
}

void RateLimiter::cleanup_ip_history(IpState& state, const std::chrono::steady_clock::time_point& now) {
    // Remove CPU usage older than 1 minute
    auto minute_ago = now - std::chrono::minutes(1);
    while (!state.cpu_usage_history.empty() && state.cpu_usage_history.front().first < minute_ago) {
        state.cpu_usage_history.pop_front();
    }
    
    // Remove job submissions older than 1 hour
    auto hour_ago = now - std::chrono::hours(1);
    while (!state.job_submissions.empty() && state.job_submissions.front() < hour_ago) {
        state.job_submissions.pop_front();
    }
}

} // namespace sandrun