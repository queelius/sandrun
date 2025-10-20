#include <gtest/gtest.h>
#include "http_server.h"
#include "multipart.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <curl/curl.h>

namespace sandrun {
namespace {

// Callback for CURL to capture response
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class APIEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start test server on random port
        test_port = 18000 + (std::rand() % 1000);
        base_url = "http://localhost:" + std::to_string(test_port);
        
        // Start server in background thread
        server = std::make_unique<HttpServer>(test_port);
        server_thread = std::thread([this]() {
            server->start();
        });
        
        // Wait for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Initialize CURL
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    void TearDown() override {
        server->stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
        curl_global_cleanup();
    }

    std::string performGET(const std::string& endpoint) {
        CURL* curl = curl_easy_init();
        std::string response;
        
        if (curl) {
            std::string url = base_url + endpoint;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
            
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                response = "Error: " + std::string(curl_easy_strerror(res));
            }
            
            curl_easy_cleanup(curl);
        }
        
        return response;
    }

    std::string performPOST(const std::string& endpoint, const std::string& data, 
                           const std::string& content_type = "application/json") {
        CURL* curl = curl_easy_init();
        std::string response;
        
        if (curl) {
            std::string url = base_url + endpoint;
            
            struct curl_slist* headers = nullptr;
            std::string content_header = "Content-Type: " + content_type;
            headers = curl_slist_append(headers, content_header.c_str());
            
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
            
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                response = "Error: " + std::string(curl_easy_strerror(res));
            }
            
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        
        return response;
    }

    int test_port;
    std::string base_url;
    std::unique_ptr<HttpServer> server;
    std::thread server_thread;
};

TEST_F(APIEndpointTest, HealthCheck) {
    std::string response = performGET("/health");
    
    EXPECT_TRUE(response.find("\"status\":\"healthy\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"version\":") != std::string::npos);
}

TEST_F(APIEndpointTest, SubmitSimpleJob) {
    // Create multipart form data
    std::string boundary = "----TestBoundary123";
    std::stringstream data;
    
    // Add manifest
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"manifest\"\r\n";
    data << "\r\n";
    data << R"({
        "entrypoint": "test.py",
        "interpreter": "python3",
        "timeout": 10,
        "memory_mb": 128
    })" << "\r\n";
    
    // Add script file
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"files\"; filename=\"test.py\"\r\n";
    data << "Content-Type: text/x-python\r\n";
    data << "\r\n";
    data << "print('Hello from API test')\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    std::string response = performPOST("/submit", data.str(), 
                                       "multipart/form-data; boundary=" + boundary);
    
    // Should return job ID
    EXPECT_TRUE(response.find("\"job_id\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"status\":\"queued\"") != std::string::npos ||
                response.find("\"status\":\"running\"") != std::string::npos);
    
    // Extract job ID for further tests
    size_t id_start = response.find("\"job_id\":\"") + 10;
    size_t id_end = response.find("\"", id_start);
    if (id_start != std::string::npos && id_end != std::string::npos) {
        job_id = response.substr(id_start, id_end - id_start);
    }
}

TEST_F(APIEndpointTest, GetJobStatus) {
    // First submit a job
    SubmitSimpleJob(); // Reuse previous test
    
    if (!job_id.empty()) {
        std::string response = performGET("/status/" + job_id);
        
        EXPECT_TRUE(response.find("\"job_id\":\"" + job_id + "\"") != std::string::npos);
        EXPECT_TRUE(response.find("\"status\":") != std::string::npos);
        
        // Status should be one of valid states
        bool has_valid_status = 
            response.find("\"queued\"") != std::string::npos ||
            response.find("\"running\"") != std::string::npos ||
            response.find("\"completed\"") != std::string::npos ||
            response.find("\"failed\"") != std::string::npos;
        EXPECT_TRUE(has_valid_status);
    }
}

TEST_F(APIEndpointTest, GetJobLogs) {
    // Submit and wait for job to start
    SubmitSimpleJob();
    
    if (!job_id.empty()) {
        // Wait a bit for job to execute
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::string response = performGET("/logs/" + job_id);
        
        EXPECT_TRUE(response.find("\"stdout\":") != std::string::npos);
        EXPECT_TRUE(response.find("\"stderr\":") != std::string::npos);
        
        // Should contain our print output
        EXPECT_TRUE(response.find("Hello from API test") != std::string::npos);
    }
}

TEST_F(APIEndpointTest, ListJobOutputs) {
    // Submit job that creates files
    std::string boundary = "----TestBoundary456";
    std::stringstream data;
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"manifest\"\r\n";
    data << "\r\n";
    data << R"({
        "entrypoint": "create_files.py",
        "outputs": ["output.txt", "data.json"]
    })" << "\r\n";
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"files\"; filename=\"create_files.py\"\r\n";
    data << "\r\n";
    data << R"(
with open('output.txt', 'w') as f:
    f.write('Test output')
    
import json
with open('data.json', 'w') as f:
    json.dump({'result': 'success'}, f)
    
print('Files created')
)" << "\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    std::string submit_response = performPOST("/submit", data.str(),
                                              "multipart/form-data; boundary=" + boundary);
    
    // Extract job ID
    size_t id_start = submit_response.find("\"job_id\":\"") + 10;
    size_t id_end = submit_response.find("\"", id_start);
    std::string output_job_id;
    if (id_start != std::string::npos && id_end != std::string::npos) {
        output_job_id = submit_response.substr(id_start, id_end - id_start);
    }
    
    if (!output_job_id.empty()) {
        // Wait for job to complete
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        std::string response = performGET("/outputs/" + output_job_id);
        
        EXPECT_TRUE(response.find("\"files\":") != std::string::npos);
        EXPECT_TRUE(response.find("output.txt") != std::string::npos);
        EXPECT_TRUE(response.find("data.json") != std::string::npos);
    }
}

TEST_F(APIEndpointTest, DownloadJobOutput) {
    // Use job from ListJobOutputs test
    // Would implement similar setup and then:
    
    if (!job_id.empty()) {
        std::string response = performGET("/download/" + job_id);
        
        // Should return tar.gz archive
        // Check for gzip magic bytes
        if (response.size() > 2) {
            EXPECT_EQ((unsigned char)response[0], 0x1f); // Gzip magic byte 1
            EXPECT_EQ((unsigned char)response[1], 0x8b); // Gzip magic byte 2
        }
    }
}

TEST_F(APIEndpointTest, RateLimiting) {
    // Submit multiple jobs quickly to trigger rate limiting
    std::vector<std::string> job_ids;
    
    for (int i = 0; i < 5; i++) {
        std::string boundary = "----Boundary" + std::to_string(i);
        std::stringstream data;
        
        data << "--" << boundary << "\r\n";
        data << "Content-Disposition: form-data; name=\"manifest\"\r\n";
        data << "\r\n";
        data << R"({"entrypoint": "test.py"})" << "\r\n";
        
        data << "--" << boundary << "\r\n";
        data << "Content-Disposition: form-data; name=\"files\"; filename=\"test.py\"\r\n";
        data << "\r\n";
        data << "print(" << i << ")\r\n";
        
        data << "--" << boundary << "--\r\n";
        
        std::string response = performPOST("/submit", data.str(),
                                           "multipart/form-data; boundary=" + boundary);
        
        if (response.find("\"job_id\":") != std::string::npos) {
            job_ids.push_back(response);
        } else if (response.find("rate limit") != std::string::npos) {
            // Rate limit hit as expected
            EXPECT_GT(i, 1); // Should allow at least 2 jobs
            break;
        }
    }
    
    EXPECT_GE(job_ids.size(), 2); // Should allow at least 2 concurrent jobs per IP
}

TEST_F(APIEndpointTest, InvalidJobID) {
    std::string response = performGET("/status/invalid_job_id_12345");
    
    EXPECT_TRUE(response.find("\"error\":") != std::string::npos);
    EXPECT_TRUE(response.find("not found") != std::string::npos ||
                response.find("invalid") != std::string::npos);
}

TEST_F(APIEndpointTest, LargeFileUpload) {
    std::string boundary = "----LargeBoundary";
    std::stringstream data;
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"manifest\"\r\n";
    data << "\r\n";
    data << R"({"entrypoint": "process.py", "memory_mb": 512})" << "\r\n";
    
    // Create a large file (1MB)
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"files\"; filename=\"large.dat\"\r\n";
    data << "Content-Type: application/octet-stream\r\n";
    data << "\r\n";
    
    std::string large_content(1024 * 1024, 'X');
    data << large_content << "\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    std::string response = performPOST("/submit", data.str(),
                                       "multipart/form-data; boundary=" + boundary);
    
    // Should handle large files
    EXPECT_TRUE(response.find("\"job_id\":") != std::string::npos ||
                response.find("\"error\":\"file too large\"") != std::string::npos);
}

TEST_F(APIEndpointTest, GPUJobSubmission) {
    std::string boundary = "----GPUBoundary";
    std::stringstream data;
    
    // Manifest with GPU requirements
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"manifest\"\r\n";
    data << "\r\n";
    data << R"({
        "entrypoint": "gpu_test.py",
        "interpreter": "python3",
        "gpu": {
            "required": true,
            "min_vram_gb": 4,
            "cuda_version": "11.8"
        },
        "timeout": 60,
        "memory_mb": 2048
    })" << "\r\n";
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"files\"; filename=\"gpu_test.py\"\r\n";
    data << "\r\n";
    data << R"(
import os
print(f"CUDA_VISIBLE_DEVICES: {os.environ.get('CUDA_VISIBLE_DEVICES', 'not set')}")
print("GPU job running")
)" << "\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    std::string response = performPOST("/submit", data.str(),
                                       "multipart/form-data; boundary=" + boundary);
    
    // Should accept GPU job
    EXPECT_TRUE(response.find("\"job_id\":") != std::string::npos ||
                response.find("\"error\":\"no GPU available\"") != std::string::npos);
}

TEST_F(APIEndpointTest, WebSocketLogStreaming) {
    // Would test WebSocket endpoint for real-time log streaming
    // This requires WebSocket client implementation
    // Placeholder for WebSocket test
    GTEST_SKIP() << "WebSocket testing requires additional setup";
}

private:
    std::string job_id;
};

} // namespace
} // namespace sandrun