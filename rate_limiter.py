"""
Dead simple IP-based rate limiting for anonymous users.
No user accounts, just fair resource sharing.
"""

import time
from collections import defaultdict, deque
from typing import Dict, Optional
import threading

class RateLimiter:
    """
    IP-based rate limiting with CPU time quotas.
    Tracks CPU seconds used per IP per minute.
    """
    
    def __init__(
        self,
        cpu_seconds_per_minute: int = 10,  # Max CPU seconds per minute
        max_concurrent_jobs: int = 2,       # Max jobs per IP at once  
        max_jobs_per_hour: int = 20,        # Max jobs per IP per hour
        cleanup_after_hours: int = 24       # Forget IP after inactivity
    ):
        self.cpu_seconds_per_minute = cpu_seconds_per_minute
        self.max_concurrent_jobs = max_concurrent_jobs
        self.max_jobs_per_hour = max_jobs_per_hour
        self.cleanup_after_hours = cleanup_after_hours
        
        # Thread-safe tracking
        self.lock = threading.Lock()
        
        # Track CPU usage: IP -> deque of (timestamp, cpu_seconds)
        self.cpu_usage: Dict[str, deque] = defaultdict(lambda: deque())
        
        # Track active jobs: IP -> set of job_ids
        self.active_jobs: Dict[str, set] = defaultdict(set)
        
        # Track job history: IP -> deque of timestamps
        self.job_history: Dict[str, deque] = defaultdict(lambda: deque())
        
        # Last seen: IP -> timestamp (for cleanup)
        self.last_seen: Dict[str, float] = {}
    
    def can_submit_job(self, ip: str) -> tuple[bool, Optional[str]]:
        """Check if IP can submit a new job."""
        with self.lock:
            now = time.time()
            self.last_seen[ip] = now
            
            # Check concurrent jobs
            if len(self.active_jobs[ip]) >= self.max_concurrent_jobs:
                return False, f"Max {self.max_concurrent_jobs} concurrent jobs"
            
            # Check hourly limit
            self._cleanup_old_entries(ip, now)
            hour_ago = now - 3600
            recent_jobs = [t for t in self.job_history[ip] if t > hour_ago]
            if len(recent_jobs) >= self.max_jobs_per_hour:
                return False, f"Max {self.max_jobs_per_hour} jobs per hour"
            
            # Check CPU quota
            minute_ago = now - 60
            recent_cpu = sum(cpu for t, cpu in self.cpu_usage[ip] if t > minute_ago)
            if recent_cpu >= self.cpu_seconds_per_minute:
                wait_time = 60 - (now - self.cpu_usage[ip][0][0])
                return False, f"CPU quota exceeded, wait {wait_time:.0f}s"
            
            return True, None
    
    def register_job_start(self, ip: str, job_id: str):
        """Register that a job has started."""
        with self.lock:
            now = time.time()
            self.active_jobs[ip].add(job_id)
            self.job_history[ip].append(now)
            self.last_seen[ip] = now
    
    def register_job_end(self, ip: str, job_id: str, cpu_seconds: float):
        """Register that a job has ended and track CPU usage."""
        with self.lock:
            now = time.time()
            self.active_jobs[ip].discard(job_id)
            self.cpu_usage[ip].append((now, cpu_seconds))
            self.last_seen[ip] = now
            self._cleanup_old_entries(ip, now)
    
    def get_available_cpu_seconds(self, ip: str) -> float:
        """Get remaining CPU seconds available for this IP."""
        with self.lock:
            now = time.time()
            minute_ago = now - 60
            recent_cpu = sum(cpu for t, cpu in self.cpu_usage[ip] if t > minute_ago)
            return max(0, self.cpu_seconds_per_minute - recent_cpu)
    
    def _cleanup_old_entries(self, ip: str, now: float):
        """Remove old entries to prevent memory growth."""
        # Remove CPU usage older than 1 minute
        minute_ago = now - 60
        while self.cpu_usage[ip] and self.cpu_usage[ip][0][0] < minute_ago:
            self.cpu_usage[ip].popleft()
        
        # Remove job history older than 1 hour  
        hour_ago = now - 3600
        while self.job_history[ip] and self.job_history[ip][0] < hour_ago:
            self.job_history[ip].popleft()
    
    def cleanup_inactive_ips(self):
        """Remove IPs that haven't been seen in cleanup_after_hours."""
        with self.lock:
            now = time.time()
            cutoff = now - (self.cleanup_after_hours * 3600)
            
            inactive_ips = [ip for ip, last in self.last_seen.items() if last < cutoff]
            for ip in inactive_ips:
                del self.cpu_usage[ip]
                del self.active_jobs[ip]
                del self.job_history[ip]
                del self.last_seen[ip]
            
            return len(inactive_ips)

# Global rate limiter instance
rate_limiter = RateLimiter(
    cpu_seconds_per_minute=10,  # Fair share for anonymous users
    max_concurrent_jobs=2,       # Prevent DoS
    max_jobs_per_hour=20,        # Reasonable limit
    cleanup_after_hours=24       # Privacy: forget after 1 day
)