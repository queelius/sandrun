/*
 * Sandrun - Anonymous Ephemeral Code Execution
 * Batch job execution with manifest support
 */

#include "http_server.h"
#include "sandbox.h"
#include "multipart.h"
#include "rate_limiter.h"
#include "job_executor.h"
#include "websocket.h"
#include "file_utils.h"
#include "environment_manager.h"
#include "worker_identity.h"
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
#include <sys/socket.h>

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
    std::string environment;               // Environment template name (optional)
    std::string status = "queued";
    std::string stdout_log;
    std::string stderr_log;
    std::string working_dir;
    int queue_position = 0;
    double cpu_seconds = 0;
    size_t memory_mb = 0;
    std::chrono::steady_clock::time_point created_at;

    // Verification metadata (for trustless pools)
    std::string job_hash;                  // SHA256 of job inputs (commitment)
    std::map<std::string, FileMetadata> output_files;  // Output file metadata with hashes
    int64_t wall_time_ms = 0;              // Wall clock time in milliseconds
    int exit_code = 0;                     // Process exit code

    // Worker identity (for signed results)
    std::string worker_id;                 // Worker public key (base64)
    std::string result_signature;          // Signature of result data (base64)
};

std::map<std::string, std::unique_ptr<Job>> jobs;
std::queue<std::string> job_queue;
std::mutex jobs_mutex;

// Escape string for JSON
std::string json_escape(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            default:
                if (c < 32) {
                    // Escape other control characters
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    escaped += buf;
                } else {
                    escaped += c;
                }
        }
    }
    return escaped;
}

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

// Parse JSON array of strings
std::vector<std::string> json_get_string_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;

    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return result;

    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) return result;

    size_t array_start = json.find('[', colon);
    if (array_start == std::string::npos) return result;

    size_t array_end = json.find(']', array_start);
    if (array_end == std::string::npos) return result;

    // Extract array content
    std::string array_content = json.substr(array_start + 1, array_end - array_start - 1);

    // Parse each string in the array
    size_t pos = 0;
    while (pos < array_content.size()) {
        size_t quote_start = array_content.find('"', pos);
        if (quote_start == std::string::npos) break;

        size_t quote_end = array_content.find('"', quote_start + 1);
        if (quote_end == std::string::npos) break;

        std::string value = array_content.substr(quote_start + 1, quote_end - quote_start - 1);
        result.push_back(value);

        pos = quote_end + 1;
    }

    return result;
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
            // Check if this is the frontend's fake tar format (starts with "----Tar")
            std::string data_str(part.data.begin(), part.data.end());
            if (data_str.find("----Tar\nPath:") == 0) {
                // Parse the fake tar format from the web frontend
                // Format: ----Tar\nPath: filename\nSize: N\n\nCONTENT
                size_t path_start = data_str.find("Path: ") + 6;
                size_t path_end = data_str.find('\n', path_start);
                std::string filename = data_str.substr(path_start, path_end - path_start);

                size_t content_start = data_str.find("\n\n") + 2;
                std::string content = data_str.substr(content_start);

                // Write the file directly
                std::string file_path = job_dir + "/" + filename;
                std::ofstream out(file_path);
                out << content;
                out.close();
            }
            // Handle tar.gz
            else if (part.filename.find(".tar.gz") != std::string::npos ||
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
    std::string worker_key_file;
    bool generate_key = false;

    // Parse command line
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::string(argv[i]) == "--worker-key" && i + 1 < argc) {
            worker_key_file = argv[++i];
        } else if (std::string(argv[i]) == "--generate-key") {
            generate_key = true;
        }
    }

    // Load or generate worker identity
    std::unique_ptr<WorkerIdentity> worker_identity;
    if (generate_key) {
        std::cout << "Generating new worker identity..." << std::endl;
        worker_identity = WorkerIdentity::generate();
        if (worker_identity) {
            std::string keyfile = worker_key_file.empty() ? "worker_key.pem" : worker_key_file;
            if (worker_identity->save_to_file(keyfile)) {
                std::cout << "✅ Saved worker key to: " << keyfile << std::endl;
                std::cout << "   Worker ID: " << worker_identity->get_worker_id() << std::endl;
                return 0;
            } else {
                std::cerr << "❌ Failed to save worker key" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "❌ Failed to generate worker identity" << std::endl;
            return 1;
        }
    } else if (!worker_key_file.empty()) {
        worker_identity = WorkerIdentity::from_keyfile(worker_key_file);
        if (!worker_identity) {
            std::cerr << "❌ Failed to load worker key from: " << worker_key_file << std::endl;
            std::cerr << "   Generate a new key with: --generate-key --worker-key mykey.pem" << std::endl;
            return 1;
        }
    }
    
    std::cout << "🏃 Sandrun - Anonymous Batch Job Execution" << std::endl;
    std::cout << "   Directory Upload • Manifest-Driven • Sandboxed" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    if (worker_identity) {
        std::cout << "Worker Mode: IDENTIFIED" << std::endl;
        std::cout << "Worker ID: " << worker_identity->get_worker_id() << std::endl;
    } else {
        std::cout << "Worker Mode: ANONYMOUS (no worker key)" << std::endl;
    }
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
        job->working_dir = "/tmp/sandrun_jobs/" + job->job_id;
        
        // Save files
        save_files(job->working_dir, parts);
        
        // Parse manifest
        for (const auto& part : parts) {
            if (part.name == "manifest") {
                std::string manifest(part.data.begin(), part.data.end());
                job->entrypoint = json_get_string(manifest, "entrypoint");
                std::string interp = json_get_string(manifest, "interpreter");
                if (!interp.empty()) job->interpreter = interp;

                // Parse environment template
                job->environment = json_get_string(manifest, "environment");

                // Parse output patterns
                job->outputs = json_get_string_array(manifest, "outputs");

                // Parse args
                job->args = json_get_string_array(manifest, "args");
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

                // Parse environment template from file manifest
                if (job->environment.empty()) {
                    job->environment = json_get_string(manifest, "environment");
                }

                // Parse output patterns and args from file manifest
                if (job->outputs.empty()) {
                    job->outputs = json_get_string_array(manifest, "outputs");
                }
                if (job->args.empty()) {
                    job->args = json_get_string_array(manifest, "args");
                }
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

        // Calculate job hash (commitment to job inputs for verification)
        {
            std::ostringstream job_data;
            job_data << job->entrypoint << "|"
                     << job->interpreter << "|"
                     << job->environment << "|";
            for (const auto& arg : job->args) {
                job_data << arg << "|";
            }

            // Include entrypoint file content in hash
            std::string entrypoint_path = job->working_dir + "/" + job->entrypoint;
            if (fs::exists(entrypoint_path)) {
                std::ifstream ent_file(entrypoint_path);
                job_data << std::string((std::istreambuf_iterator<char>(ent_file)),
                                        std::istreambuf_iterator<char>());
            }

            job->job_hash = FileUtils::sha256_string(job_data.str());
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
    server.route("GET", "/status/", [&](const HttpRequest& req) {
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

        // Build comprehensive JSON response
        std::stringstream json;
        json << std::fixed << std::setprecision(2);
        json << "{\n";
        json << "  \"job_id\": \"" << job_id << "\",\n";
        json << "  \"status\": \"" << job->status << "\",\n";
        json << "  \"queue_position\": " << job->queue_position << ",\n";

        // Execution metadata
        json << "  \"execution_metadata\": {\n";
        json << "    \"cpu_seconds\": " << job->cpu_seconds << ",\n";
        json << "    \"memory_peak_bytes\": " << (job->memory_mb * 1024 * 1024) << ",\n";
        json << "    \"memory_peak_mb\": " << job->memory_mb << ",\n";
        json << "    \"wall_time_ms\": " << job->wall_time_ms << ",\n";
        json << "    \"exit_code\": " << job->exit_code << ",\n";
        json << "    \"environment\": \"" << json_escape(job->environment) << "\",\n";
        json << "    \"interpreter\": \"" << json_escape(job->interpreter) << "\"\n";
        json << "  },\n";

        // Job commitment (verification hash)
        json << "  \"job_hash\": \"" << job->job_hash << "\",\n";

        // Output files with hashes (for verification)
        json << "  \"output_files\": {\n";
        bool first_file = true;
        for (const auto& [path, metadata] : job->output_files) {
            if (!first_file) json << ",\n";
            first_file = false;
            json << "    \"" << json_escape(path) << "\": {\n";
            json << "      \"size_bytes\": " << metadata.size_bytes << ",\n";
            json << "      \"sha256\": \"" << metadata.sha256_hash << "\",\n";
            json << "      \"type\": \"" << FileUtils::file_type_to_string(metadata.type) << "\"\n";
            json << "    }";
        }
        json << "\n  },\n";

        // Worker identity (for signed results in pools)
        json << "  \"worker_metadata\": {\n";
        json << "    \"worker_id\": " << (job->worker_id.empty() ? "null" : "\"" + job->worker_id + "\"") << ",\n";
        json << "    \"signature\": " << (job->result_signature.empty() ? "null" : "\"" + job->result_signature + "\"") << "\n";
        json << "  }\n";
        json << "}";

        resp.body = json.str();
        return resp;
    });
    
    // GET /logs/{job_id}
    server.route("GET", "/logs/", [&](const HttpRequest& req) {
        HttpResponse resp;

        std::string job_id = req.path.substr(6);

        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }

        resp.body = "{\"stdout\":\"" + json_escape(it->second->stdout_log) +
                    "\",\"stderr\":\"" + json_escape(it->second->stderr_log) + "\"}";
        return resp;
    });
    
    // GET /outputs/{job_id} - List output files with rich metadata
    server.route("GET", "/outputs/", [&](const HttpRequest& req) {
        HttpResponse resp;

        std::string job_id = req.path.substr(9);

        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }

        // Collect files with metadata
        struct FileInfo {
            std::string path;
            size_t size;
            FileType type;
        };
        std::vector<FileInfo> files;

        if (fs::exists(it->second->working_dir)) {
            for (const auto& entry : fs::recursive_directory_iterator(it->second->working_dir)) {
                if (fs::is_regular_file(entry)) {
                    std::string rel_path = fs::relative(entry.path(), it->second->working_dir).string();
                    size_t size = fs::file_size(entry);
                    FileType type = FileUtils::detect_file_type(rel_path);

                    // Filter by output patterns if specified
                    bool include = true;
                    if (!it->second->outputs.empty()) {
                        include = false;
                        for (const auto& pattern : it->second->outputs) {
                            if (FileUtils::matches_pattern(rel_path, pattern)) {
                                include = true;
                                break;
                            }
                        }
                    }

                    if (include) {
                        files.push_back({rel_path, size, type});
                    }
                }
            }
        }

        // Build rich JSON response
        std::stringstream json;
        json << "{\n  \"job_id\": \"" << job_id << "\",\n";
        json << "  \"status\": \"" << it->second->status << "\",\n";
        json << "  \"total_files\": " << files.size() << ",\n";
        json << "  \"files\": [\n";

        for (size_t i = 0; i < files.size(); i++) {
            if (i > 0) json << ",\n";
            json << "    {\n";
            json << "      \"name\": \"" << json_escape(files[i].path) << "\",\n";
            json << "      \"size\": " << files[i].size << ",\n";
            json << "      \"size_formatted\": \"" << FileUtils::format_file_size(files[i].size) << "\",\n";
            json << "      \"type\": \"" << FileUtils::file_type_to_string(files[i].type) << "\",\n";
            json << "      \"mime_type\": \"" << FileUtils::get_mime_type(files[i].path) << "\",\n";
            json << "      \"download_url\": \"/download/" << job_id << "/" << json_escape(files[i].path) << "\"\n";
            json << "    }";
        }

        json << "\n  ]\n}";

        resp.body = json.str();
        return resp;
    });
    
    // GET /download/{job_id} or /download/{job_id}/{file_path}
    server.route("GET", "/download/", [&](const HttpRequest& req) {
        HttpResponse resp;

        std::string path_after_download = req.path.substr(10);  // After "/download/"
        size_t slash_pos = path_after_download.find('/');

        std::string job_id;
        std::string file_path;

        if (slash_pos == std::string::npos) {
            // /download/{job_id} - download all files as tar.gz
            job_id = path_after_download;
        } else {
            // /download/{job_id}/{file_path} - download single file
            job_id = path_after_download.substr(0, slash_pos);
            file_path = path_after_download.substr(slash_pos + 1);
        }

        std::lock_guard<std::mutex> lock(jobs_mutex);
        auto it = jobs.find(job_id);
        if (it == jobs.end()) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Job not found\"}";
            return resp;
        }

        if (file_path.empty()) {
            // Download all files as tar.gz
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
        } else {
            // Download single file
            std::string full_path = it->second->working_dir + "/" + file_path;

            // Security: ensure path doesn't escape working directory
            std::string canonical_work_dir = fs::canonical(it->second->working_dir).string();
            std::string canonical_file = fs::canonical(full_path).string();

            if (canonical_file.find(canonical_work_dir) != 0) {
                resp.status_code = 403;
                resp.body = "{\"error\":\"Access denied: path traversal detected\"}";
                return resp;
            }

            if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) {
                resp.status_code = 404;
                resp.body = "{\"error\":\"File not found\"}";
                return resp;
            }

            // Read file
            std::ifstream file(full_path, std::ios::binary);
            std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
            file.close();

            // Set appropriate content type and headers
            std::string mime_type = FileUtils::get_mime_type(file_path);
            std::string filename = fs::path(file_path).filename().string();

            resp.headers["Content-Type"] = mime_type;
            resp.headers["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";
            resp.body = std::string(file_data.begin(), file_data.end());
        }

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

    // GET /environments - List available environment templates
    server.route("GET", "/environments", [](const HttpRequest& req) {
        HttpResponse resp;

        auto& env_mgr = EnvironmentManager::instance();
        auto templates = env_mgr.list_templates();
        auto stats = env_mgr.get_stats();

        std::stringstream json;
        json << "{\n  \"templates\": [\n";

        for (size_t i = 0; i < templates.size(); i++) {
            if (i > 0) json << ",\n";
            json << "    \"" << templates[i] << "\"";
        }

        json << "\n  ],\n";
        json << "  \"stats\": {\n";
        json << "    \"total_templates\": " << stats.total_templates << ",\n";
        json << "    \"cached_environments\": " << stats.cached_environments << ",\n";
        json << "    \"total_uses\": " << stats.total_uses << ",\n";
        json << "    \"disk_usage_mb\": " << stats.disk_usage_mb << "\n";
        json << "  }\n";
        json << "}";

        resp.body = json.str();
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

                    // Broadcast status change
                    auto& broadcaster = OutputBroadcaster::instance();
                    broadcaster.broadcast(next_job_id, "[STATUS] Job started\n");

                    // Prepare environment if template specified
                    std::string pythonpath;
                    if (!job->environment.empty()) {
                        try {
                            auto& env_mgr = EnvironmentManager::instance();
                            if (env_mgr.has_template(job->environment)) {
                                std::cout << "Preparing environment: " << job->environment << std::endl;
                                broadcaster.broadcast(next_job_id,
                                    "[ENV] Preparing environment: " + job->environment + "\n");

                                // Prepare environment (creates job-specific clone)
                                std::string env_dir = env_mgr.prepare_environment(
                                    job->environment,
                                    next_job_id
                                );

                                // Set PYTHONPATH to include environment packages
                                // Keep job files in their own directory, just add environment packages to path
                                pythonpath = env_dir + "/site-packages";

                                broadcaster.broadcast(next_job_id, "[ENV] Environment ready\n");
                            } else {
                                broadcaster.broadcast(next_job_id,
                                    "[ENV] Warning: Environment '" + job->environment +
                                    "' not found, using default\n");
                            }
                        } catch (const std::exception& e) {
                            broadcaster.broadcast(next_job_id,
                                "[ENV] Error preparing environment: " + std::string(e.what()) + "\n");
                            // Continue with default environment
                        }
                    }

                    // Execute with proper sandboxing
                    std::cout << "Executing in sandbox: " << job->working_dir << std::endl;

                    auto result = JobExecutor::execute(
                        job->working_dir,
                        job->interpreter,
                        job->entrypoint,
                        job->args,
                        pythonpath
                    );

                    // Update job with results
                    job->stdout_log = result.stdout_log;
                    job->stderr_log = result.stderr_log;
                    job->cpu_seconds = result.cpu_seconds;
                    job->memory_mb = result.memory_bytes / (1024 * 1024);
                    cpu_seconds = result.cpu_seconds;

                    // Broadcast output to WebSocket subscribers
                    if (!result.stdout_log.empty()) {
                        broadcaster.broadcast(next_job_id, result.stdout_log);
                        broadcaster.append_output(next_job_id, result.stdout_log);
                    }
                    if (!result.stderr_log.empty()) {
                        broadcaster.broadcast(next_job_id, "[STDERR]\n" + result.stderr_log);
                        broadcaster.append_output(next_job_id, "[STDERR]\n" + result.stderr_log);
                    }

                    job->status = (result.exit_code == 0) ? "completed" : "failed";
                    job->exit_code = result.exit_code;

                    // Hash output files (for verification in trustless pools)
                    if (!job->outputs.empty()) {
                        job->output_files = FileUtils::hash_directory(job->working_dir, job->outputs);
                    } else {
                        // Hash all output files if no patterns specified
                        job->output_files = FileUtils::hash_directory(job->working_dir);
                    }

                    // Sign result if worker has identity
                    if (worker_identity) {
                        job->worker_id = worker_identity->get_worker_id();

                        // Build data to sign (job_hash + output hashes + metadata)
                        std::ostringstream sign_data;
                        sign_data << job->job_hash << "|"
                                  << job->exit_code << "|"
                                  << job->cpu_seconds << "|"
                                  << job->memory_mb << "|";

                        // Include output file hashes in signature
                        for (const auto& [path, metadata] : job->output_files) {
                            sign_data << path << ":" << metadata.sha256_hash << "|";
                        }

                        job->result_signature = worker_identity->sign(sign_data.str());
                    }

                    // Broadcast completion status
                    std::string completion_msg = "[DONE] Job " + job->status +
                                                 " (exit=" + std::to_string(result.exit_code) +
                                                 ", CPU=" + std::to_string(cpu_seconds) + "s" +
                                                 ", Mem=" + std::to_string(job->memory_mb) + "MB)\n";
                    broadcaster.broadcast(next_job_id, completion_msg);

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

            // Cleanup old environments every 10 iterations (~10 seconds)
            static int cleanup_counter = 0;
            if (++cleanup_counter >= 10) {
                cleanup_counter = 0;
                auto& env_mgr = EnvironmentManager::instance();
                env_mgr.cleanup_old_environments();
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
    std::cout << "  WS   /stream/{id}    - WebSocket stream of live output" << std::endl;
    std::cout << "  GET  /environments   - List environment templates" << std::endl;
    std::cout << std::endl;

    // WebSocket route for streaming job output
    server.websocket_route("/stream/", [&](int client_fd, const std::string& job_id) {
        std::cout << "[WebSocket] Client connected to stream job: " << job_id << std::endl;

        // Subscribe to job output
        auto& broadcaster = OutputBroadcaster::instance();
        broadcaster.subscribe(job_id, client_fd);

        // Send accumulated output if job already started
        std::string accumulated = broadcaster.get_accumulated_output(job_id);
        if (!accumulated.empty()) {
            WebSocketManager::send_text(client_fd, accumulated);
        }

        // Check job status
        {
            std::lock_guard<std::mutex> lock(jobs_mutex);
            auto it = jobs.find(job_id);
            if (it == jobs.end()) {
                WebSocketManager::send_text(client_fd, "[ERROR] Job not found: " + job_id + "\n");
                WebSocketManager::send_close(client_fd);
                broadcaster.unsubscribe(job_id, client_fd);
                return;
            }

            std::string status = it->second->status;
            WebSocketManager::send_text(client_fd, "[STATUS] Job status: " + status + "\n");

            // If job already completed, send final logs and close
            if (status == "completed" || status == "failed") {
                if (!it->second->stdout_log.empty()) {
                    WebSocketManager::send_text(client_fd, it->second->stdout_log);
                }
                if (!it->second->stderr_log.empty()) {
                    WebSocketManager::send_text(client_fd, "[STDERR]\n" + it->second->stderr_log);
                }
                WebSocketManager::send_text(client_fd, "[DONE] Job " + status + "\n");
                WebSocketManager::send_close(client_fd);
                broadcaster.unsubscribe(job_id, client_fd);
                return;
            }
        }

        // Keep connection alive and wait for incoming messages (ping/pong)
        bool should_close = false;
        while (!should_close) {
            std::string msg = WebSocketManager::read_frame(client_fd, should_close);
            if (should_close) break;

            // Check if job completed
            {
                std::lock_guard<std::mutex> lock(jobs_mutex);
                auto it = jobs.find(job_id);
                if (it != jobs.end()) {
                    if (it->second->status == "completed" || it->second->status == "failed") {
                        WebSocketManager::send_text(client_fd, "[DONE] Job " + it->second->status + "\n");
                        should_close = true;
                    }
                }
            }
        }

        // Unsubscribe and close
        broadcaster.unsubscribe(job_id, client_fd);
        std::cout << "[WebSocket] Client disconnected from job: " << job_id << std::endl;
    });

    // Start server (blocks)
    server.start();
    
    executor.join();
    return 0;
}