#include "websocket.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <iostream>

namespace sandrun {

// Base64 encoding table
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string WebSocketManager::base64_encode(const unsigned char* data, size_t len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while (i++ < 3)
            ret += '=';
    }

    return ret;
}

bool WebSocketManager::is_websocket_upgrade(const std::map<std::string, std::string>& headers) {
    auto upgrade_it = headers.find("Upgrade");
    auto connection_it = headers.find("Connection");

    if (upgrade_it == headers.end() || connection_it == headers.end()) {
        return false;
    }

    // Case-insensitive comparison
    std::string upgrade = upgrade_it->second;
    std::string connection = connection_it->second;

    // Convert to lowercase
    for (auto& c : upgrade) c = tolower(c);
    for (auto& c : connection) c = tolower(c);

    return upgrade == "websocket" && connection.find("upgrade") != std::string::npos;
}

std::string WebSocketManager::create_handshake_response(const std::string& sec_key) {
    // WebSocket magic string
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = sec_key + magic;

    // SHA-1 hash
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);

    // Base64 encode
    std::string accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);

    // Build response
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
             << "\r\n";

    return response.str();
}

std::vector<uint8_t> WebSocketManager::create_frame(WSOpcode opcode, const std::string& payload) {
    std::vector<uint8_t> frame;

    // First byte: FIN bit + opcode
    frame.push_back(0x80 | static_cast<uint8_t>(opcode));

    // Payload length
    size_t payload_len = payload.size();
    if (payload_len <= 125) {
        frame.push_back(static_cast<uint8_t>(payload_len));
    } else if (payload_len <= 65535) {
        frame.push_back(126);
        frame.push_back((payload_len >> 8) & 0xFF);
        frame.push_back(payload_len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((payload_len >> (i * 8)) & 0xFF);
        }
    }

    // Payload data
    frame.insert(frame.end(), payload.begin(), payload.end());

    return frame;
}

bool WebSocketManager::send_text(int client_fd, const std::string& message) {
    auto frame = create_frame(WSOpcode::TEXT, message);
    ssize_t sent = write(client_fd, frame.data(), frame.size());
    return sent == static_cast<ssize_t>(frame.size());
}

bool WebSocketManager::send_close(int client_fd) {
    auto frame = create_frame(WSOpcode::CLOSE, "");
    ssize_t sent = write(client_fd, frame.data(), frame.size());
    return sent == static_cast<ssize_t>(frame.size());
}

std::string WebSocketManager::read_frame(int client_fd, bool& is_close) {
    is_close = false;

    // Read first 2 bytes
    uint8_t header[2];
    if (read(client_fd, header, 2) != 2) {
        is_close = true;
        return "";
    }

    // Parse opcode
    uint8_t opcode = header[0] & 0x0F;
    if (opcode == static_cast<uint8_t>(WSOpcode::CLOSE)) {
        is_close = true;
        return "";
    }

    // Parse payload length
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t len_bytes[2];
        if (read(client_fd, len_bytes, 2) != 2) {
            is_close = true;
            return "";
        }
        payload_len = (len_bytes[0] << 8) | len_bytes[1];
    } else if (payload_len == 127) {
        uint8_t len_bytes[8];
        if (read(client_fd, len_bytes, 8) != 8) {
            is_close = true;
            return "";
        }
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | len_bytes[i];
        }
    }

    // Read masking key if present
    uint8_t mask[4] = {0};
    if (masked) {
        if (read(client_fd, mask, 4) != 4) {
            is_close = true;
            return "";
        }
    }

    // Read payload
    std::vector<uint8_t> payload(payload_len);
    size_t total_read = 0;
    while (total_read < payload_len) {
        ssize_t n = read(client_fd, payload.data() + total_read, payload_len - total_read);
        if (n <= 0) {
            is_close = true;
            return "";
        }
        total_read += n;
    }

    // Unmask if needed
    if (masked) {
        for (size_t i = 0; i < payload_len; i++) {
            payload[i] ^= mask[i % 4];
        }
    }

    return std::string(payload.begin(), payload.end());
}

// OutputBroadcaster implementation

void OutputBroadcaster::subscribe(const std::string& job_id, int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[job_id].insert(client_fd);
    std::cout << "[WebSocket] Client " << client_fd << " subscribed to job " << job_id << std::endl;
}

void OutputBroadcaster::unsubscribe(const std::string& job_id, int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(job_id);
    if (it != subscribers_.end()) {
        it->second.erase(client_fd);
        if (it->second.empty()) {
            subscribers_.erase(it);
        }
    }
    std::cout << "[WebSocket] Client " << client_fd << " unsubscribed from job " << job_id << std::endl;
}

void OutputBroadcaster::broadcast(const std::string& job_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = subscribers_.find(job_id);
    if (it != subscribers_.end()) {
        std::vector<int> dead_clients;

        for (int client_fd : it->second) {
            if (!WebSocketManager::send_text(client_fd, message)) {
                dead_clients.push_back(client_fd);
            }
        }

        // Remove dead clients
        for (int fd : dead_clients) {
            it->second.erase(fd);
            close(fd);
        }
    }
}

std::string OutputBroadcaster::get_accumulated_output(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accumulated_output_.find(job_id);
    return (it != accumulated_output_.end()) ? it->second : "";
}

void OutputBroadcaster::append_output(const std::string& job_id, const std::string& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    accumulated_output_[job_id] += output;
}

void OutputBroadcaster::clear_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(job_id);
    accumulated_output_.erase(job_id);
}

} // namespace sandrun
