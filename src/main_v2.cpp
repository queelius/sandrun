/*
 * Sandrun - Anonymous Ephemeral Code Execution
 * Batch job execution with manifest support
 */

#include "http_server.h"
#include "sandbox.h"
#include "multipart.h"
#include "rate_limiter.h"
#include "job_executor.h"
#include <iostream>
#include <thread>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <map>
#include <queue>
#include <mutex>
#include <memory>
#include <filesystem>
#include <cstring>

using namespace sandrun;
namespace fs = std::filesystem;

// Job with manifest support
struct Job {
    std::string job_id;
    std::string client_ip;
    std::string entrypoint;
    std::string interpreter = "python3";
    std::vector<std::string> args;
    std::vector<std::string> outputs;
    std::string status = "queued";
    std::string stdout_log;
    std::string stderr_log;
    std::string working_dir;
    int queue_position = 0;
    double cpu_seconds = 0;
    size_t memory_mb = 0;
    std::chrono::steady_clock::time_point created_at;
};

std::map<std::string, std::unique_ptr<Job>> jobs;
std::queue<std::string> job_queue;
std::mutex jobs_mutex;

// Parse simple JSON (minimal implementation)
std::string json_get_string(const std::string& json, const std::string& key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";
    
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return "";
    
    size_t value_start = json.find('"', colon);
    if (value_start == std::string::npos) return "";
    value_start++;
    
    size_t value_end = json.find('"', value_start);
    if (value_end == std::string::npos) return "";
    
    return json.substr(value_start, value_end - value_start);
}

// Generate unique job ID
std::string generate_job_id() {
    static int counter = 0;
    std::stringstream ss;
    ss << "job_" << std::time(nullptr) << "_" << (++counter);
    return ss.str();
}

// Save uploaded files to job directory
void save_files(const std::string& job_dir, const std::vector<MultipartPart>& parts) {
    fs::create_directories(job_dir);
    
    for (const auto& part : parts) {
        if (part.name == "files" && !part.filename.empty()) {
            // Handle tar.gz
            if (part.filename.find(".tar.gz") != std::string::npos ||
                part.filename.find(".tgz") != std::string::npos) {
                std::string tar_path = job_dir + "/upload.tar.gz";
                std::ofstream out(tar_path, std::ios::binary);
                out.write((char*)part.data.data(), part.data.size());
                out.close();
                
                // Extract tar.gz
                std::string cmd = "cd " + job_dir + " && tar -xzf upload.tar.gz && rm upload.tar.gz";
                system(cmd.c_str());
            }
            // Handle zip
            else if (part.filename.find(".zip") != std::string::npos) {
                std::string zip_path = job_dir + "/upload.zip";
                std::ofstream out(zip_path, std::ios::binary);
                out.write((char*)part.data.data(), part.data.size());
                out.close();
                
                // Extract zip
                std::string cmd = "cd " + job_dir + " && unzip -q upload.zip && rm upload.zip";
                system(cmd.c_str());
            }
            // Handle individual file
            else {
                std::string file_path = job_dir + "/" + part.filename;
                std::ofstream out(file_path, std::ios::binary);
                out.write((char*)part.data.data(), part.data.size());
                out.close();
            }
        }
    }
}

int main(int argc, char* argv[]) {
    int port = 8443;
    
    // Parse command line
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        }
    }
    
    std::cout << "🏃 Sandrun - Anonymous Batch Job Execution" << std::endl;
    std::cout << "   Directory Upload • Manifest-Driven • Sandboxed" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
    
    // Initialize rate limiter
    RateLimiter::Config rate_config;
    rate_config.cpu_seconds_per_minute = 10;    // 10 CPU seconds per minute
    rate_config.max_concurrent_jobs = 2;        // 2 jobs at once per IP
    rate_config.max_jobs_per_hour = 20;         // 20 jobs per hour per IP
    RateLimiter rate_limiter(rate_config);
    
    // Create HTTP server
    HttpServer server(port);
    
    // POST /submit - Submit job with files and manifest
    server.route("POST", "/submit", [&rate_limiter](const HttpRequest& req) {
        HttpResponse resp;
        
        // Check rate limit
        auto quota = rate_limiter.check_quota(req.client_ip);
        if (!quota.can_submit) {
            resp.status_code = 429;  // Too Many Requests
            std::stringstream json;
            json << "{\"error\":\"" << quota.reason << "\","
                 << "\"cpu_available\":" << quota.cpu_seconds_available << ","
                 << "\"active_jobs\":" << quota.active_jobs << "}";
            resp.body = json.str();
            return resp;
        }
        
        // Parse multipart form data
        std::vector<MultipartPart> parts = MultipartParser::parse(
            req.headers.count("Content-Type") ? req.headers.at("Content-Type") : "",
            req.body
        );
        
        if (parts.empty()) {
            resp.status_code = 400;
            resp.body = "{\"error\":\"No files uploaded\"}";
            return resp;
        }
        
        // Create job
        auto job = std::make_unique<Job>();
        job->job_id = generate_job_id();
        job->client_ip = req.client_ip;
        job->created_at = std::chrono::steady_clock::now();
        job->working_dir = "/tmp/sandrun/" + job->job_id;
        
        // Save files
        save_files(job->working_dir, parts);
        
        // Parse manifest
        for (const auto& part : parts) {
            if (part.name == "manifest") {
                std::string manifest(part.data.begin(), part.data.end());
                job->entrypoint = json_get_string(manifest, "entrypoint");
                std::string interp = json_get_string(manifest, "interpreter");
                if (!interp.empty()) job->interpreter = interp;
                // TODO: Parse args and outputs arrays
            }
        }
        
        // Check for job.json in uploaded files
        if (job->entrypoint.empty()) {
            std::string manifest_path = job->working_dir + "/job.json";
            if (fs::exists(manifest_path)) {
                std::ifstream manifest_file(manifest_path);
                std::string manifest((std::istreambuf_iterator<char>(manifest_file)),
                                    std::istreambuf_iterator<char>());
                job->entrypoint = json_get_string(manifest, "entrypoint");
                std::string interp = json_get_string(manifest, "interpreter");
                if (!interp.empty()) job->interpreter = interp;
            }
        }
        
        // Default entrypoint if not specified
        if (job->entrypoint.empty()) {
            if (fs::exists(job->working_dir + "/main.py")) {
                job->entrypoint = "main.py";
                job->interpreter = "python3";
            } else if (fs::exists(job->working_dir + "/index.js")) {
                job->entrypoint = "index.js";
                job->interpreter = "node";
            } else if (fs::exists(job->working_dir + "/run.sh")) {
                job->entrypoint = "run.sh";
                job->interpreter = "bash";
            }
        }
        
        if (job->entrypoint.empty()) {
            resp.status_code = 400;
            resp.body = "{\"error\":\"No entrypoint specified\"}";
            fs::remove_all(job->working_dir);
            return resp;
        }
        
        // Add to queue
        std::string job_id = job->job_id;
        std::string client_ip = req.client_ip;
        
        // Register with rate limiter
        if (!rate_limiter.register_job_start(client_ip, job_id)) {
            resp.status_code = 429;
            resp.body = "{\"error\":\"Rate limit exceeded\"}";
            fs::remove_all(job->working_dir);
            return resp;
        }
        
        {
            std::lock_guard<std::mutex> lock(jobs_mutex);
            job_queue.push(job_id);
            job->queue_position = job_queue.size();
            jobs[job_id] = std::move(job);
        }
        
        std::cout << "Job submitted: " << job_id 
                  << " from IP: " << client_ip
                  << " (entrypoint: " << jobs[job_id]->entrypoint << ")" << std::endl;
        
        resp.body = "{\"job_id\":\"" + job_id + "\",\"status\":\"queued\"}";
        return resp;
    });
    
    // GET /status/{job_id}
    server.route("GET", "/status/", [](const HttpRequest& req) {
        HttpResponse resp;
        
        std::string job_id = req.path.substr(8);
        
        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }
        
        auto& job = it->second;
        std::stringstream json;
        json << "{"
             << "\"status\":\"" << job->status << "\","
             << "\"queue_position\":" << job->queue_position << ","
             << "\"metrics\":{"
             << "\"cpu_seconds\":" << job->cpu_seconds << ","
             << "\"memory_mb\":" << job->memory_mb << ","
             << "\"runtime\":" << (job->status == "completed" || job->status == "failed" ? 
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - job->created_at).count() : 0)
             << "}}";
        
        resp.body = json.str();
        return resp;
    });
    
    // GET /logs/{job_id}
    server.route("GET", "/logs/", [](const HttpRequest& req) {
        HttpResponse resp;
        
        std::string job_id = req.path.substr(6);
        
        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }
        
        resp.body = "{\"stdout\":\"" + it->second->stdout_log + 
                    "\",\"stderr\":\"" + it->second->stderr_log + "\"}";
        return resp;
    });
    
    // GET /outputs/{job_id}
    server.route("GET", "/outputs/", [](const HttpRequest& req) {
        HttpResponse resp;
        
        std::string job_id = req.path.substr(9);
        
        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }
        
        // List files in working directory
        std::vector<std::string> files;
        if (fs::exists(it->second->working_dir)) {
            for (const auto& entry : fs::recursive_directory_iterator(it->second->working_dir)) {
                if (fs::is_regular_file(entry)) {
                    std::string rel_path = fs::relative(entry.path(), it->second->working_dir).string();
                    files.push_back(rel_path);
                }
            }
        }
        
        // Build JSON array
        std::stringstream json;
        json << "{\"files\":[";
        for (size_t i = 0; i < files.size(); i++) {
            if (i > 0) json << ",";
            json << "\"" << files[i] << "\"";
        }
        json << "]}";
        
        resp.body = json.str();
        return resp;
    });
    
    // GET /download/{job_id}
    server.route("GET", "/download/", [](const HttpRequest& req) {
        HttpResponse resp;
        
        std::string job_id = req.path.substr(10);
        
        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }
        
        // Create tar.gz of working directory
        std::string tar_path = "/tmp/" + job_id + ".tar.gz";
        std::string cmd = "tar -czf " + tar_path + " -C " + it->second->working_dir + " .";
        system(cmd.c_str());
        
        // Read tar file
        std::ifstream tar_file(tar_path, std::ios::binary);
        std::vector<uint8_t> tar_data((std::istreambuf_iterator<char>(tar_file)),
                                      std::istreambuf_iterator<char>());
        tar_file.close();
        
        // Delete tar file
        fs::remove(tar_path);
        
        // Delete job data (privacy)
        fs::remove_all(it->second->working_dir);
        jobs.erase(it);
        
        resp.headers["Content-Type"] = "application/gzip";
        resp.headers["Content-Disposition"] = "attachment; filename=\"" + job_id + ".tar.gz\"";
        resp.body = std::string(tar_data.begin(), tar_data.end());
        
        return resp;
    });
    
    // GET / - Basic info
    server.route("GET", "/", [](const HttpRequest& req) {
        HttpResponse resp;
        resp.body = R"({
            "service": "sandrun",
            "status": "running",
            "description": "Batch job execution with directory upload",
            "privacy": "Jobs auto-delete after download",
            "limits": "10 CPU-sec/min, 512MB RAM, 5 min timeout"
        })";
        return resp;
    });
    
    // GET /stats - Get quota and system stats for IP
    server.route("GET", "/stats", [&rate_limiter](const HttpRequest& req) {
        HttpResponse resp;
        
        // Get quota info for this IP
        auto quota = rate_limiter.check_quota(req.client_ip);
        
        // Count total jobs in system
        int total_queued = 0;
        int total_running = 0;
        {
            std::lock_guard<std::mutex> lock(jobs_mutex);
            for (const auto& [id, job] : jobs) {
                if (job->status == "queued") total_queued++;
                else if (job->status == "running") total_running++;
            }
        }
        
        std::stringstream json;
        json << "{"
             << "\"your_quota\":{"
             << "\"used\":" << quota.cpu_seconds_used << ","
             << "\"limit\":" << 10.0 << ","
             << "\"available\":" << quota.cpu_seconds_available << ","
             << "\"active_jobs\":" << quota.active_jobs << ","
             << "\"can_submit\":" << (quota.can_submit ? "true" : "false") << ","
             << "\"reason\":\"" << quota.reason << "\""
             << "},"
             << "\"system\":{"
             << "\"queue_length\":" << total_queued << ","
             << "\"active_jobs\":" << total_running
             << "}}";
        
        resp.body = json.str();
        return resp;
    });
    
    // Job executor thread
    std::thread executor([&rate_limiter]() {
        Sandbox sandbox;
        
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            std::string next_job_id;
            {
                std::lock_guard<std::mutex> lock(jobs_mutex);
                if (!job_queue.empty()) {
                    next_job_id = job_queue.front();
                    job_queue.pop();
                    
                    // Update queue positions
                    std::queue<std::string> temp = job_queue;
                    int pos = 1;
                    while (!temp.empty()) {
                        if (jobs.count(temp.front())) {
                            jobs[temp.front()]->queue_position = pos++;
                        }
                        temp.pop();
                    }
                }
            }
            
            if (!next_job_id.empty()) {
                std::string client_ip;
                double cpu_seconds = 0;
                
                {
                    std::lock_guard<std::mutex> lock(jobs_mutex);
                    auto& job = jobs[next_job_id];
                    client_ip = job->client_ip;
                    
                    std::cout << "Executing job: " << next_job_id 
                              << " (" << job->entrypoint << ")" << std::endl;
                    
                    job->status = "running";
                    job->queue_position = 0;
                    
                    // Execute with proper sandboxing
                    std::cout << "Executing in sandbox: " << job->working_dir << std::endl;
                    
                    auto result = JobExecutor::execute(
                        job->working_dir,
                        job->interpreter,
                        job->entrypoint,
                        job->args
                    );
                    
                    // Update job with results
                    job->stdout_log = result.stdout_log;
                    job->stderr_log = result.stderr_log;
                    job->cpu_seconds = result.cpu_seconds;
                    job->memory_mb = result.memory_bytes / (1024 * 1024);
                    cpu_seconds = result.cpu_seconds;
                    
                    job->status = (result.exit_code == 0) ? "completed" : "failed";
                    std::cout << "Job " << next_job_id << " " << job->status 
                              << " (exit=" << result.exit_code
                              << ", CPU=" << cpu_seconds << "s"
                              << ", Mem=" << job->memory_mb << "MB)" << std::endl;
                }
                
                // Register job completion with rate limiter
                rate_limiter.register_job_end(client_ip, next_job_id, cpu_seconds);
            }
            
            // Cleanup old jobs (privacy - delete after 5 minutes)
            {
                std::lock_guard<std::mutex> lock(jobs_mutex);
                auto now = std::chrono::steady_clock::now();
                auto it = jobs.begin();
                while (it != jobs.end()) {
                    auto age = std::chrono::duration_cast<std::chrono::minutes>(
                        now - it->second->created_at).count();
                    
                    if (age > 5 && it->second->status != "running") {
                        std::cout << "Auto-deleting old job: " << it->first << std::endl;
                        fs::remove_all(it->second->working_dir);
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
    std::cout << "  POST /submit         - Submit job with files" << std::endl;
    std::cout << "  GET  /status/{id}    - Check status" << std::endl;
    std::cout << "  GET  /logs/{id}      - Get logs" << std::endl;
    std::cout << "  GET  /outputs/{id}   - List output files" << std::endl;
    std::cout << "  GET  /download/{id}  - Download outputs" << std::endl;
    std::cout << std::endl;
    
    // Start server (blocks)
    server.start();
    
    executor.join();
    return 0;
}