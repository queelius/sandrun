#include "http_server.h"
#include "constants.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <thread>

namespace sandrun {

HttpServer::HttpServer(int port) : port_(port), server_fd_(-1), running_(false) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::route(const std::string& method, const std::string& path, HandlerFunc handler) {
    routes_[method + " " + path] = handler;
}

void HttpServer::start() {
    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    
    // Allow reuse
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to bind to port");
    }
    
    // Listen
    if (listen(server_fd_, LISTEN_BACKLOG) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to listen");
    }
    
    running_ = true;
    std::cout << "Server listening on port " << port_ << std::endl;
    
    // Accept connections
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) continue;
            break;
        }
        
        // Get client IP
        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        
        // Handle in new thread (simple concurrency)
        std::thread([this, client_fd, client_ip]() {
            handle_client(client_fd, client_ip);
            close(client_fd);
        }).detach();
    }
}

void HttpServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
}

void HttpServer::handle_client(int client_fd, const std::string& client_ip) {
    // Read request with size limit
    std::string request_data;
    request_data.reserve(INITIAL_HTTP_BUFFER);
    
    char buffer[PIPE_BUFFER_SIZE];
    ssize_t bytes_read;
    size_t total_read = 0;
    
    // Read request in chunks with size limit
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        if (total_read + bytes_read > MAX_REQUEST_SIZE) {
            // Request too large
            std::string error_response = "HTTP/1.1 413 Payload Too Large\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Content-Length: 36\r\n\r\n"
                                        "{\"error\":\"Request exceeds 100MB limit\"}";
            write(client_fd, error_response.c_str(), error_response.length());
            return;
        }
        
        request_data.append(buffer, bytes_read);
        total_read += bytes_read;
        
        // Check if we've received the complete headers
        if (request_data.find("\r\n\r\n") != std::string::npos) {
            // For non-chunked requests, check Content-Length
            size_t content_length_pos = request_data.find("Content-Length: ");
            if (content_length_pos != std::string::npos) {
                size_t line_end = request_data.find("\r\n", content_length_pos);
                std::string length_str = request_data.substr(
                    content_length_pos + 16, 
                    line_end - content_length_pos - 16
                );
                size_t content_length = std::stoul(length_str);
                
                // Calculate expected total size
                size_t header_end = request_data.find("\r\n\r\n") + 4;
                size_t expected_size = header_end + content_length;
                
                if (expected_size > MAX_REQUEST_SIZE) {
                    // Request will be too large
                    std::string error_response = "HTTP/1.1 413 Payload Too Large\r\n"
                                                "Content-Type: application/json\r\n"
                                                "Content-Length: 36\r\n\r\n"
                                                "{\"error\":\"Request exceeds 100MB limit\"}";
                    write(client_fd, error_response.c_str(), error_response.length());
                    return;
                }
                
                // Read remaining body if needed
                while (request_data.size() < expected_size) {
                    bytes_read = read(client_fd, buffer, 
                        std::min(sizeof(buffer), expected_size - request_data.size()));
                    if (bytes_read <= 0) break;
                    request_data.append(buffer, bytes_read);
                }
                break;
            } else {
                // No Content-Length, assume request is complete
                break;
            }
        }
    }
    
    if (request_data.empty()) return;
    
    // Parse request
    HttpRequest req = parse_request(request_data);
    req.client_ip = client_ip;
    
    // Find handler
    std::string route_key = req.method + " " + req.path;
    HttpResponse resp;
    
    // Check for exact match
    auto it = routes_.find(route_key);
    if (it != routes_.end()) {
        try {
            resp = it->second(req);
        } catch (const std::exception& e) {
            resp.status_code = 500;
            resp.body = "{\"error\":\"" + std::string(e.what()) + "\"}";
        }
    } else {
        // Check for prefix matches (for /download/{job_id}/{file} style routes)
        bool found = false;
        for (const auto& [pattern, handler] : routes_) {
            size_t space_pos = pattern.find(' ');
            std::string method = pattern.substr(0, space_pos);
            std::string path_pattern = pattern.substr(space_pos + 1);
            
            if (method == req.method && req.path.find(path_pattern) == 0) {
                try {
                    resp = handler(req);
                    found = true;
                    break;
                } catch (const std::exception& e) {
                    resp.status_code = 500;
                    resp.body = "{\"error\":\"" + std::string(e.what()) + "\"}";
                    found = true;
                    break;
                }
            }
        }
        
        if (!found) {
            resp.status_code = 404;
            resp.body = "{\"error\":\"Not found\"}";
        }
    }
    
    // Send response
    std::string response_str = build_response(resp);
    write(client_fd, response_str.c_str(), response_str.length());
}

HttpRequest HttpServer::parse_request(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    
    // Parse request line
    std::string line;
    std::getline(stream, line);
    
    size_t space1 = line.find(' ');
    size_t space2 = line.find(' ', space1 + 1);
    
    if (space1 != std::string::npos && space2 != std::string::npos) {
        req.method = line.substr(0, space1);
        req.path = line.substr(space1 + 1, space2 - space1 - 1);
    }
    
    // Parse headers
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        if (line.back() == '\r') line.pop_back();
        
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 2); // Skip ": "
            req.headers[key] = value;
        }
    }
    
    // Rest is body
    std::string body;
    std::string rest;
    while (std::getline(stream, rest)) {
        if (!body.empty()) body += "\n";
        body += rest;
    }
    req.body = body;
    
    return req;
}

std::string HttpServer::build_response(const HttpResponse& resp) {
    std::ostringstream out;
    
    // Status line
    out << "HTTP/1.1 " << resp.status_code << " ";
    switch (resp.status_code) {
        case 200: out << "OK"; break;
        case 400: out << "Bad Request"; break;
        case 404: out << "Not Found"; break;
        case 500: out << "Internal Server Error"; break;
        default: out << "Unknown"; break;
    }
    out << "\r\n";
    
    // Headers
    for (const auto& [key, value] : resp.headers) {
        out << key << ": " << value << "\r\n";
    }
    
    // Content length
    out << "Content-Length: " << resp.body.length() << "\r\n";
    out << "\r\n";
    
    // Body
    out << resp.body;
    
    return out.str();
}

} // namespace sandrun