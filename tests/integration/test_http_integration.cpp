/**
 * HTTP Server Integration Tests
 *
 * Tests the HTTP server via actual socket connections.
 * These tests exercise the full request/response lifecycle.
 */

#include <gtest/gtest.h>
#include "../../src/http_server.h"
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <random>
#include <atomic>

using namespace sandrun;

// ============================================================================
// Test Fixture
// ============================================================================

class HttpIntegrationTest : public ::testing::Test {
protected:
    std::unique_ptr<HttpServer> server;
    std::thread server_thread;
    int test_port;
    std::atomic<bool> server_started{false};

    void SetUp() override {
        // Use random port to avoid conflicts
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(18000, 19000);
        test_port = dis(gen);

        server = std::make_unique<HttpServer>(test_port);

        // Register test routes
        server->route("GET", "/health", [](const HttpRequest&) {
            HttpResponse resp;
            resp.status_code = 200;
            resp.body = "{\"status\":\"healthy\"}";
            return resp;
        });

        server->route("GET", "/echo-headers", [](const HttpRequest& req) {
            HttpResponse resp;
            resp.status_code = 200;
            std::string body = "{";
            bool first = true;
            for (const auto& [key, value] : req.headers) {
                if (!first) body += ",";
                body += "\"" + key + "\":\"" + value + "\"";
                first = false;
            }
            body += "}";
            resp.body = body;
            return resp;
        });

        server->route("POST", "/echo-body", [](const HttpRequest& req) {
            HttpResponse resp;
            resp.status_code = 200;
            resp.body = req.body;
            return resp;
        });

        server->route("GET", "/status/", [](const HttpRequest& req) {
            HttpResponse resp;
            resp.status_code = 200;
            // Extract job_id from path
            std::string path = req.path;
            size_t prefix_len = std::string("/status/").length();
            std::string job_id = path.substr(prefix_len);
            resp.body = "{\"job_id\":\"" + job_id + "\"}";
            return resp;
        });

        server->route("GET", "/error", [](const HttpRequest&) -> HttpResponse {
            throw std::runtime_error("Test exception");
        });

        server->route("GET", "/slow", [](const HttpRequest&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            HttpResponse resp;
            resp.status_code = 200;
            resp.body = "{\"delayed\":true}";
            return resp;
        });

        // Start server in background thread
        server_thread = std::thread([this]() {
            server_started = true;
            server->start();
        });

        // Wait for server to start
        int attempts = 0;
        while (!server_started && attempts < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            attempts++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        if (server) {
            server->stop();
        }
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }

    // Helper: Connect to server
    int connect_to_server() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(test_port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        // Set socket timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            return -1;
        }

        return sock;
    }

    // Helper: Send HTTP request and receive response
    std::string send_request(const std::string& request) {
        int sock = connect_to_server();
        if (sock < 0) {
            return "CONNECTION_FAILED";
        }

        // Send request
        ssize_t sent = send(sock, request.c_str(), request.length(), 0);
        if (sent < 0) {
            close(sock);
            return "SEND_FAILED";
        }

        // Receive response
        std::string response;
        char buffer[4096];
        ssize_t n;
        while ((n = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[n] = '\0';
            response += buffer;

            // Check if we've received complete response
            // Simple check: look for \r\n\r\n and Content-Length
            size_t header_end = response.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                size_t cl_pos = response.find("Content-Length: ");
                if (cl_pos != std::string::npos) {
                    size_t cl_end = response.find("\r\n", cl_pos);
                    int content_length = std::stoi(response.substr(cl_pos + 16, cl_end - cl_pos - 16));
                    size_t body_start = header_end + 4;
                    if (response.length() >= body_start + content_length) {
                        break;  // Complete response received
                    }
                } else {
                    // No Content-Length, assume response is complete after headers
                    break;
                }
            }
        }

        close(sock);
        return response;
    }
};

// ============================================================================
// Basic Request/Response Tests
// ============================================================================

TEST_F(HttpIntegrationTest, BasicGetRequest) {
    // Given: Server is running with /health route

    // When: Sending GET request
    std::string request =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Should receive 200 OK with JSON body
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos)
        << "Response should be 200 OK. Got:\n" << response;
    EXPECT_TRUE(response.find("{\"status\":\"healthy\"}") != std::string::npos)
        << "Body should contain health status";
}

TEST_F(HttpIntegrationTest, PostRequestWithBody) {
    // Given: Server echoes POST body

    // When: Sending POST with JSON body
    std::string body = "{\"test\":\"data\"}";
    std::string request =
        "POST /echo-body HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.length()) + "\r\n"
        "\r\n" + body;

    std::string response = send_request(request);

    // Then: Should echo back the body
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("{\"test\":\"data\"}") != std::string::npos)
        << "Should echo back request body. Got:\n" << response;
}

TEST_F(HttpIntegrationTest, RouteNotFound) {
    // Given: Request for non-existent route

    // When: Sending request
    std::string request =
        "GET /nonexistent HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Should return 404
    EXPECT_TRUE(response.find("HTTP/1.1 404") != std::string::npos)
        << "Should return 404 for unknown route. Got:\n" << response;
}

// ============================================================================
// Header Handling Tests
// ============================================================================

TEST_F(HttpIntegrationTest, HeadersPassedToHandler) {
    // Given: Route that echoes headers

    // When: Sending request with custom headers
    std::string request =
        "GET /echo-headers HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "X-Custom-Header: test-value\r\n"
        "Accept: application/json\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Headers should be available in handler
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Host") != std::string::npos)
        << "Host header should be passed";
}

TEST_F(HttpIntegrationTest, ResponseIncludesCORSHeaders) {
    // Given: Any request to server

    // When: Response is received
    std::string request =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Should include CORS headers
    EXPECT_TRUE(response.find("Access-Control-Allow-Origin: *") != std::string::npos)
        << "Should include CORS header. Got:\n" << response;
}

TEST_F(HttpIntegrationTest, ContentTypeSetCorrectly) {
    // Given: JSON response

    // When: Response is received
    std::string request =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Content-Type should be application/json
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos)
        << "Should set Content-Type. Got:\n" << response;
}

// ============================================================================
// Routing Tests
// ============================================================================

TEST_F(HttpIntegrationTest, PrefixRouteMatching) {
    // Given: Route registered as "/status/"

    // When: Requesting /status/job123
    std::string request =
        "GET /status/job123 HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Should match prefix route and extract job_id
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"job_id\":\"job123\"") != std::string::npos)
        << "Should extract path parameter. Got:\n" << response;
}

TEST_F(HttpIntegrationTest, MethodMismatchReturns404) {
    // Given: POST route exists but not GET

    // When: Sending wrong method
    std::string request =
        "GET /echo-body HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Should return 404 (method doesn't match)
    EXPECT_TRUE(response.find("HTTP/1.1 404") != std::string::npos)
        << "Should return 404 for wrong method. Got:\n" << response;
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(HttpIntegrationTest, HandlerExceptionReturns500) {
    // Given: Route that throws exception

    // When: Request triggers exception
    std::string request =
        "GET /error HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Should return 500 Internal Server Error
    EXPECT_TRUE(response.find("HTTP/1.1 500") != std::string::npos)
        << "Should return 500 for handler exception. Got:\n" << response;
}

// ============================================================================
// Connection Handling Tests
// ============================================================================

TEST_F(HttpIntegrationTest, MultipleSequentialRequests) {
    // Given: Server is running

    // When: Sending multiple requests sequentially
    for (int i = 0; i < 3; i++) {
        std::string request =
            "GET /health HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n";

        std::string response = send_request(request);

        // Then: All should succeed
        EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos)
            << "Request " << i << " should succeed";
    }
}

TEST_F(HttpIntegrationTest, ConcurrentRequests) {
    // Given: Server is running

    // When: Sending multiple requests concurrently
    std::vector<std::thread> threads;
    std::vector<bool> results(5, false);

    for (int i = 0; i < 5; i++) {
        threads.emplace_back([this, i, &results]() {
            std::string request =
                "GET /health HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "\r\n";

            std::string response = send_request(request);
            results[i] = (response.find("HTTP/1.1 200 OK") != std::string::npos);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Then: All should succeed
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(results[i]) << "Concurrent request " << i << " should succeed";
    }
}

TEST_F(HttpIntegrationTest, SlowRequestDoesNotBlock) {
    // Given: Server has a slow route

    // When: Requesting slow route
    auto start = std::chrono::steady_clock::now();

    std::string request =
        "GET /slow HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Then: Should complete with expected delay
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("{\"delayed\":true}") != std::string::npos);
    EXPECT_GE(duration_ms, 100) << "Should have delayed at least 100ms";
}

// ============================================================================
// Content-Length Handling Tests
// ============================================================================

TEST_F(HttpIntegrationTest, ResponseHasCorrectContentLength) {
    // Given: Request that returns known body

    // When: Response is received
    std::string request =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);

    // Then: Content-Length should match actual body
    std::string body = "{\"status\":\"healthy\"}";
    std::string expected_cl = "Content-Length: " + std::to_string(body.length());

    EXPECT_TRUE(response.find(expected_cl) != std::string::npos)
        << "Content-Length should be " << body.length() << ". Got:\n" << response;
}

TEST_F(HttpIntegrationTest, LargeBodyHandledCorrectly) {
    // Given: POST with larger body

    // When: Sending 10KB body
    std::string body(10 * 1024, 'x');
    std::string request =
        "POST /echo-body HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(body.length()) + "\r\n"
        "\r\n" + body;

    std::string response = send_request(request);

    // Then: Should echo back full body
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);

    // Find body in response
    size_t body_start = response.find("\r\n\r\n");
    ASSERT_NE(body_start, std::string::npos);
    std::string response_body = response.substr(body_start + 4);

    EXPECT_EQ(response_body.length(), body.length())
        << "Response body should match request body length";
}

// ============================================================================
// Malformed Request Handling
// ============================================================================

TEST_F(HttpIntegrationTest, HandlesMalformedRequest) {
    // Given: Completely malformed request

    // When: Sending garbage
    std::string request = "NOT A VALID HTTP REQUEST\r\n\r\n";

    std::string response = send_request(request);

    // Then: Should handle gracefully (either 400 or connection close)
    // Server should not crash
    EXPECT_FALSE(response.empty() || response == "CONNECTION_FAILED")
        << "Server should respond to malformed requests";
}

TEST_F(HttpIntegrationTest, HandlesEmptyRequest) {
    // Given: Empty request

    // When: Connecting but sending nothing then closing
    int sock = connect_to_server();
    ASSERT_GE(sock, 0) << "Should connect successfully";

    // Close immediately without sending
    close(sock);

    // Then: Server should not crash - verify with another request
    std::string request =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    std::string response = send_request(request);
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos)
        << "Server should still respond after handling empty request";
}
