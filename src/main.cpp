/*
 * Sandrun - Anonymous Ephemeral Code Execution
 * Simple, private, sandboxed batch job execution
 */

#include "http_server.h"
#include "sandbox.h"
#include <iostream>
#include <thread>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <map>
#include <queue>
#include <mutex>
#include <memory>

using namespace sandrun;

// Simple in-memory job queue (for MVP)
struct SimpleJob {
    std::string job_id;
    std::string code;
    std::string status = "queued";
    std::string output;
    std::string client_ip;
    std::chrono::steady_clock::time_point created_at;
};

std::map<std::string, std::unique_ptr<SimpleJob>> jobs;
std::queue<std::string> job_queue;
std::mutex jobs_mutex;

// Generate unique job ID
std::string generate_job_id() {
    static int counter = 0;
    std::stringstream ss;
    ss << "job_" << std::time(nullptr) << "_" << (++counter);
    return ss.str();
}

int main(int argc, char* argv[]) {
    int port = 8443;
    
    // Parse command line
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        }
    }
    
    std::cout << "🏃 Sandrun - Anonymous Ephemeral Code Execution" << std::endl;
    std::cout << "   Simple • Private • Sandboxed" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    // Create HTTP server
    HttpServer server(port);
    
    // POST /run - Submit code for execution
    server.route("POST", "/run", [](const HttpRequest& req) {
        HttpResponse resp;
        
        // Extract code from body
        std::string code = req.body;
        if (code.empty()) {
            resp.status_code = 400;
            resp.body = "{\"error\":\"No code provided\"}";
            return resp;
        }
        
        // Create job
        auto job = std::make_unique<SimpleJob>();
        job->job_id = generate_job_id();
        job->code = code;
        job->client_ip = req.client_ip;
        job->created_at = std::chrono::steady_clock::now();
        
        // Add to queue
        std::string job_id_copy = job->job_id;
        {
            std::lock_guard<std::mutex> lock(jobs_mutex);
            job_queue.push(job->job_id);
            jobs[job->job_id] = std::move(job);
        }
        
        resp.body = "{\"job_id\":\"" + job_id_copy + "\",\"status\":\"queued\"}";
        return resp;
    });
    
    // GET /status/{job_id} - Get job status
    server.route("GET", "/status/", [](const HttpRequest& req) {
        HttpResponse resp;
        
        // Extract job_id from path
        std::string job_id = req.path.substr(8); // Skip "/status/"
        
        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }
        
        resp.body = "{\"status\":\"" + it->second->status + "\"}";
        return resp;
    });
    
    // GET /result/{job_id} - Get job result
    server.route("GET", "/result/", [](const HttpRequest& req) {
        HttpResponse resp;
        
        // Extract job_id from path
        std::string job_id = req.path.substr(8); // Skip "/result/"
        
        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }
        
        if (it->second->status != "done" && it->second->status != "failed") {
            resp.status_code = 400;
            resp.body = "{\"error\":\"Job not complete\"}";
            return resp;
        }
        
        resp.headers["Content-Type"] = "text/plain";
        resp.body = it->second->output;
        
        // Auto-delete after retrieval (privacy)
        jobs.erase(it);
        
        return resp;
    });
    
    // GET / - Basic info
    server.route("GET", "/", [](const HttpRequest& req) {
        HttpResponse resp;
        resp.body = R"({
            "service": "sandrun",
            "status": "running",
            "description": "Anonymous job execution - no accounts required",
            "privacy": "Jobs auto-delete after completion",
            "limits": "10 CPU-sec/min, 512MB RAM, 5 min timeout"
        })";
        return resp;
    });
    
    // Job executor thread
    std::thread executor([&]() {
        Sandbox sandbox;
        
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            std::string next_job_id;
            {
                std::lock_guard<std::mutex> lock(jobs_mutex);
                if (!job_queue.empty()) {
                    next_job_id = job_queue.front();
                    job_queue.pop();
                }
            }
            
            if (!next_job_id.empty()) {
                std::lock_guard<std::mutex> lock(jobs_mutex);
                auto& job = jobs[next_job_id];
                
                std::cout << "Executing job: " << next_job_id 
                          << " from IP: " << job->client_ip << std::endl;
                
                job->status = "running";
                
                // Execute in sandbox
                try {
                    JobResult result = sandbox.execute(job->code, next_job_id);
                    job->output = result.output;
                    if (!result.error.empty()) {
                        job->output += "\n--- stderr ---\n" + result.error;
                    }
                    job->status = "done";
                    
                    std::cout << "Job " << next_job_id << " completed. "
                              << "CPU: " << result.cpu_seconds << "s, "
                              << "Memory: " << result.memory_bytes / 1024 / 1024 << "MB" 
                              << std::endl;
                } catch (const std::exception& e) {
                    job->output = "Execution failed: " + std::string(e.what());
                    job->status = "failed";
                    std::cout << "Job " << next_job_id << " failed: " << e.what() << std::endl;
                }
            }
            
            // Cleanup old jobs (privacy - delete after 1 minute)
            {
                std::lock_guard<std::mutex> lock(jobs_mutex);
                auto now = std::chrono::steady_clock::now();
                auto it = jobs.begin();
                while (it != jobs.end()) {
                    auto age = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it->second->created_at).count();
                    
                    if (age > 60 && it->second->status != "running") {
                        std::cout << "Auto-deleting old job: " << it->first << std::endl;
                        it = jobs.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    });
    
    std::cout << "Starting server on port " << port << "..." << std::endl;
    std::cout << "API endpoints:" << std::endl;
    std::cout << "  POST /run          - Submit code" << std::endl;
    std::cout << "  GET  /status/{id}  - Check status" << std::endl;
    std::cout << "  GET  /result/{id}  - Get result (auto-deletes)" << std::endl;
    std::cout << std::endl;
    
    // Start server (blocks)
    server.start();
    
    executor.join();
    return 0;
}