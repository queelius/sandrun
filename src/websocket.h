#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <set>
#include <map>
#include <openssl/sha.h>
#include <arpa/inet.h>

namespace sandrun {

// WebSocket frame opcodes
enum class WSOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

// WebSocket connection manager
class WebSocketManager {
public:
    // Check if request is WebSocket upgrade
    static bool is_websocket_upgrade(const std::map<std::string, std::string>& headers);

    // Perform WebSocket handshake
    static std::string create_handshake_response(const std::string& sec_key);

    // Send text frame to client
    static bool send_text(int client_fd, const std::string& message);

    // Send close frame
    static bool send_close(int client_fd);

    // Read and decode incoming frame
    static std::string read_frame(int client_fd, bool& is_close);

private:
    // Base64 encoding for WebSocket handshake
    static std::string base64_encode(const unsigned char* data, size_t len);

    // Create WebSocket frame
    static std::vector<uint8_t> create_frame(WSOpcode opcode, const std::string& payload);
};

// Job output broadcaster - manages streaming output to multiple WebSocket clients
class OutputBroadcaster {
public:
    static OutputBroadcaster& instance() {
        static OutputBroadcaster instance;
        return instance;
    }

    // Register a WebSocket client for a job's output
    void subscribe(const std::string& job_id, int client_fd);

    // Unregister a WebSocket client
    void unsubscribe(const std::string& job_id, int client_fd);

    // Broadcast output to all subscribers of a job
    void broadcast(const std::string& job_id, const std::string& message);

    // Get accumulated output for a job (for late subscribers)
    std::string get_accumulated_output(const std::string& job_id);

    // Append output to accumulator
    void append_output(const std::string& job_id, const std::string& output);

    // Clear accumulated output for a job
    void clear_job(const std::string& job_id);

private:
    OutputBroadcaster() = default;

    std::mutex mutex_;
    std::map<std::string, std::set<int>> subscribers_;  // job_id -> set of client fds
    std::map<std::string, std::string> accumulated_output_;  // job_id -> accumulated output
};

} // namespace sandrun
