#pragma once

#include <string>
#include <functional>
#include <map>
#include <thread>
#include <atomic>

namespace sandrun {

// Simple HTTP request
struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string client_ip;
};

// Simple HTTP response
struct HttpResponse {
    int status_code = 200;
    std::map<std::string, std::string> headers;
    std::string body;
    
    HttpResponse() {
        headers["Content-Type"] = "application/json";
        headers["Access-Control-Allow-Origin"] = "*";
    }
};

// Request handler function type
using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

// Minimal HTTP server - no external dependencies
class HttpServer {
public:
    HttpServer(int port = 8443);
    ~HttpServer();
    
    // Register route handlers
    void route(const std::string& method, const std::string& path, HandlerFunc handler);
    
    // Start server (blocks)
    void start();
    
    // Stop server
    void stop();
    
private:
    int port_;
    int server_fd_;
    std::atomic<bool> running_;
    std::map<std::string, HandlerFunc> routes_;
    
    void handle_client(int client_fd, const std::string& client_ip);
    HttpRequest parse_request(const std::string& raw);
    std::string build_response(const HttpResponse& resp);
    std::string get_client_ip(int client_fd);
};

} // namespace sandrun