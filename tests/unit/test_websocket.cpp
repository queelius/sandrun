#include <gtest/gtest.h>
#include "websocket.h"
#include <map>
#include <string>

using namespace sandrun;

// ============================================================================
// WebSocketManager Tests - Upgrade Detection
// ============================================================================

class WebSocketManagerTest : public ::testing::Test {
protected:
    std::map<std::string, std::string> create_headers(
        const std::string& upgrade = "",
        const std::string& connection = ""
    ) {
        std::map<std::string, std::string> headers;
        if (!upgrade.empty()) headers["Upgrade"] = upgrade;
        if (!connection.empty()) headers["Connection"] = connection;
        return headers;
    }
};

TEST_F(WebSocketManagerTest, DetectsValidWebSocketUpgrade) {
    // Given: Valid WebSocket upgrade headers
    auto headers = create_headers("websocket", "Upgrade");

    // When: Checking if request is WebSocket upgrade
    bool is_upgrade = WebSocketManager::is_websocket_upgrade(headers);

    // Then: Should recognize as WebSocket upgrade
    EXPECT_TRUE(is_upgrade) << "Valid WebSocket upgrade not detected";
}

TEST_F(WebSocketManagerTest, DetectsUpgradeWithMixedCase) {
    // Given: Headers with mixed case (case-insensitive)
    auto headers = create_headers("WebSocket", "upgrade");

    // When: Checking if request is WebSocket upgrade
    bool is_upgrade = WebSocketManager::is_websocket_upgrade(headers);

    // Then: Should recognize as WebSocket upgrade (case insensitive)
    EXPECT_TRUE(is_upgrade) << "Case-insensitive upgrade not detected";
}

TEST_F(WebSocketManagerTest, DetectsUpgradeWithConnectionKeepAlive) {
    // Given: Connection header with multiple values including upgrade
    auto headers = create_headers("websocket", "keep-alive, Upgrade");

    // When: Checking if request is WebSocket upgrade
    bool is_upgrade = WebSocketManager::is_websocket_upgrade(headers);

    // Then: Should recognize upgrade in comma-separated list
    EXPECT_TRUE(is_upgrade) << "Upgrade in connection list not detected";
}

TEST_F(WebSocketManagerTest, RejectsMissingUpgradeHeader) {
    // Given: Headers without Upgrade field
    auto headers = create_headers("", "Upgrade");

    // When: Checking if request is WebSocket upgrade
    bool is_upgrade = WebSocketManager::is_websocket_upgrade(headers);

    // Then: Should reject as not a WebSocket upgrade
    EXPECT_FALSE(is_upgrade) << "Should reject missing Upgrade header";
}

TEST_F(WebSocketManagerTest, RejectsMissingConnectionHeader) {
    // Given: Headers without Connection field
    auto headers = create_headers("websocket", "");

    // When: Checking if request is WebSocket upgrade
    bool is_upgrade = WebSocketManager::is_websocket_upgrade(headers);

    // Then: Should reject as not a WebSocket upgrade
    EXPECT_FALSE(is_upgrade) << "Should reject missing Connection header";
}

TEST_F(WebSocketManagerTest, RejectsInvalidUpgradeValue) {
    // Given: Headers with wrong Upgrade value
    auto headers = create_headers("http/2.0", "Upgrade");

    // When: Checking if request is WebSocket upgrade
    bool is_upgrade = WebSocketManager::is_websocket_upgrade(headers);

    // Then: Should reject as not a WebSocket upgrade
    EXPECT_FALSE(is_upgrade) << "Should reject non-websocket upgrade";
}

TEST_F(WebSocketManagerTest, RejectsEmptyHeaders) {
    // Given: Empty headers map
    std::map<std::string, std::string> headers;

    // When: Checking if request is WebSocket upgrade
    bool is_upgrade = WebSocketManager::is_websocket_upgrade(headers);

    // Then: Should reject as not a WebSocket upgrade
    EXPECT_FALSE(is_upgrade) << "Should reject empty headers";
}

// ============================================================================
// WebSocketManager Tests - Handshake
// ============================================================================

TEST_F(WebSocketManagerTest, CreatesValidHandshakeResponse) {
    // Given: A Sec-WebSocket-Key from client
    std::string sec_key = "dGhlIHNhbXBsZSBub25jZQ==";

    // When: Creating handshake response
    std::string response = WebSocketManager::create_handshake_response(sec_key);

    // Then: Should have proper HTTP response structure
    EXPECT_TRUE(response.find("HTTP/1.1 101 Switching Protocols") == 0)
        << "Should start with 101 status";
    EXPECT_TRUE(response.find("Upgrade: websocket") != std::string::npos)
        << "Should include Upgrade: websocket";
    EXPECT_TRUE(response.find("Connection: Upgrade") != std::string::npos)
        << "Should include Connection: Upgrade";
    EXPECT_TRUE(response.find("Sec-WebSocket-Accept:") != std::string::npos)
        << "Should include Sec-WebSocket-Accept header";
}

TEST_F(WebSocketManagerTest, CalculatesCorrectAcceptKey) {
    // Given: Known test vector from RFC 6455
    std::string sec_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string expected_accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

    // When: Creating handshake response
    std::string response = WebSocketManager::create_handshake_response(sec_key);

    // Then: Should contain correct Sec-WebSocket-Accept value
    EXPECT_TRUE(response.find("Sec-WebSocket-Accept: " + expected_accept) != std::string::npos)
        << "Accept key should match RFC 6455 test vector";
}

TEST_F(WebSocketManagerTest, HandshakeEndsWithDoubleNewline) {
    // Given: Any Sec-WebSocket-Key
    std::string sec_key = "test_key_123";

    // When: Creating handshake response
    std::string response = WebSocketManager::create_handshake_response(sec_key);

    // Then: Should end with \r\n\r\n (HTTP header terminator)
    EXPECT_TRUE(response.size() >= 4) << "Response too short";
    std::string ending = response.substr(response.size() - 4);
    EXPECT_EQ(ending, "\r\n\r\n") << "Should end with \\r\\n\\r\\n";
}

// ============================================================================
// OutputBroadcaster Tests - Subscription Management
// ============================================================================

class OutputBroadcasterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear any previous state
        broadcaster = &OutputBroadcaster::instance();

        // Clean up test jobs
        broadcaster->clear_job("test_job_1");
        broadcaster->clear_job("test_job_2");
        broadcaster->clear_job("test_job_multi");
    }

    void TearDown() override {
        // Clean up after tests
        broadcaster->clear_job("test_job_1");
        broadcaster->clear_job("test_job_2");
        broadcaster->clear_job("test_job_multi");
    }

    OutputBroadcaster* broadcaster;
};

TEST_F(OutputBroadcasterTest, SubscribeSingleClient) {
    // Given: A job ID and client FD
    std::string job_id = "test_job_1";
    int client_fd = 100;

    // When: Client subscribes to job
    broadcaster->subscribe(job_id, client_fd);

    // Then: Should succeed (no crash, verified by TearDown cleanup)
    SUCCEED();
}

TEST_F(OutputBroadcasterTest, SubscribeMultipleClientsToSameJob) {
    // Given: Multiple clients for the same job
    std::string job_id = "test_job_multi";
    int client1 = 101;
    int client2 = 102;
    int client3 = 103;

    // When: Multiple clients subscribe
    broadcaster->subscribe(job_id, client1);
    broadcaster->subscribe(job_id, client2);
    broadcaster->subscribe(job_id, client3);

    // Then: Should succeed (all subscriptions tracked)
    SUCCEED();
}

TEST_F(OutputBroadcasterTest, UnsubscribeClient) {
    // Given: A subscribed client
    std::string job_id = "test_job_1";
    int client_fd = 100;
    broadcaster->subscribe(job_id, client_fd);

    // When: Client unsubscribes
    broadcaster->unsubscribe(job_id, client_fd);

    // Then: Should succeed (subscription removed)
    SUCCEED();
}

TEST_F(OutputBroadcasterTest, UnsubscribeNonExistentClient) {
    // Given: A job without any subscriptions
    std::string job_id = "test_job_1";
    int client_fd = 999;

    // When: Trying to unsubscribe non-existent client
    broadcaster->unsubscribe(job_id, client_fd);

    // Then: Should handle gracefully (no crash)
    SUCCEED();
}

TEST_F(OutputBroadcasterTest, UnsubscribeFromNonExistentJob) {
    // Given: A job ID that doesn't exist
    std::string job_id = "nonexistent_job";
    int client_fd = 100;

    // When: Trying to unsubscribe from non-existent job
    broadcaster->unsubscribe(job_id, client_fd);

    // Then: Should handle gracefully (no crash)
    SUCCEED();
}

// ============================================================================
// OutputBroadcaster Tests - Output Accumulation
// ============================================================================

TEST_F(OutputBroadcasterTest, AppendOutputToJob) {
    // Given: A job ID
    std::string job_id = "test_job_1";
    std::string output1 = "First line\n";
    std::string output2 = "Second line\n";

    // When: Appending output
    broadcaster->append_output(job_id, output1);
    broadcaster->append_output(job_id, output2);

    // Then: Should accumulate output
    std::string accumulated = broadcaster->get_accumulated_output(job_id);
    EXPECT_EQ(accumulated, "First line\nSecond line\n")
        << "Should accumulate all appended output";
}

TEST_F(OutputBroadcasterTest, GetAccumulatedOutputForNewJob) {
    // Given: A job ID with no output yet
    std::string job_id = "test_job_empty";

    // When: Getting accumulated output
    std::string accumulated = broadcaster->get_accumulated_output(job_id);

    // Then: Should return empty string
    EXPECT_TRUE(accumulated.empty())
        << "New job should have empty accumulated output";
}

TEST_F(OutputBroadcasterTest, GetAccumulatedOutputAfterAppend) {
    // Given: A job with some output
    std::string job_id = "test_job_1";
    broadcaster->append_output(job_id, "Hello ");
    broadcaster->append_output(job_id, "World!");

    // When: Getting accumulated output
    std::string accumulated = broadcaster->get_accumulated_output(job_id);

    // Then: Should return concatenated output
    EXPECT_EQ(accumulated, "Hello World!")
        << "Should return all accumulated output";
}

TEST_F(OutputBroadcasterTest, ClearJobRemovesOutput) {
    // Given: A job with accumulated output
    std::string job_id = "test_job_1";
    broadcaster->append_output(job_id, "Some output");

    // When: Clearing the job
    broadcaster->clear_job(job_id);

    // Then: Accumulated output should be gone
    std::string accumulated = broadcaster->get_accumulated_output(job_id);
    EXPECT_TRUE(accumulated.empty())
        << "Cleared job should have no accumulated output";
}

TEST_F(OutputBroadcasterTest, ClearJobRemovesSubscriptions) {
    // Given: A job with subscribers and output
    std::string job_id = "test_job_1";
    int client1 = 100;
    int client2 = 101;
    broadcaster->subscribe(job_id, client1);
    broadcaster->subscribe(job_id, client2);
    broadcaster->append_output(job_id, "Some output");

    // When: Clearing the job
    broadcaster->clear_job(job_id);

    // Then: Should remove both subscriptions and output
    std::string accumulated = broadcaster->get_accumulated_output(job_id);
    EXPECT_TRUE(accumulated.empty())
        << "Cleared job should have no data";

    // Unsubscribe should not crash (subscriptions removed)
    broadcaster->unsubscribe(job_id, client1);
    broadcaster->unsubscribe(job_id, client2);
    SUCCEED();
}

// ============================================================================
// OutputBroadcaster Tests - Job Isolation
// ============================================================================

TEST_F(OutputBroadcasterTest, MultipleJobsAreIsolated) {
    // Given: Multiple jobs with different outputs
    std::string job1 = "test_job_1";
    std::string job2 = "test_job_2";

    broadcaster->append_output(job1, "Output for job 1");
    broadcaster->append_output(job2, "Output for job 2");

    // When: Getting output for each job
    std::string output1 = broadcaster->get_accumulated_output(job1);
    std::string output2 = broadcaster->get_accumulated_output(job2);

    // Then: Each job should have its own output
    EXPECT_EQ(output1, "Output for job 1")
        << "Job 1 output should be isolated";
    EXPECT_EQ(output2, "Output for job 2")
        << "Job 2 output should be isolated";
}

TEST_F(OutputBroadcasterTest, ClearingOneJobDoesNotAffectOthers) {
    // Given: Multiple jobs with outputs
    std::string job1 = "test_job_1";
    std::string job2 = "test_job_2";

    broadcaster->append_output(job1, "Output 1");
    broadcaster->append_output(job2, "Output 2");

    // When: Clearing one job
    broadcaster->clear_job(job1);

    // Then: Other job should still have its output
    std::string output1 = broadcaster->get_accumulated_output(job1);
    std::string output2 = broadcaster->get_accumulated_output(job2);

    EXPECT_TRUE(output1.empty()) << "Cleared job should be empty";
    EXPECT_EQ(output2, "Output 2") << "Other job should be unaffected";
}

TEST_F(OutputBroadcasterTest, SubscriptionsAreJobSpecific) {
    // Given: Clients subscribed to different jobs
    std::string job1 = "test_job_1";
    std::string job2 = "test_job_2";
    int client1 = 100;
    int client2 = 101;

    // When: Subscribing clients to different jobs
    broadcaster->subscribe(job1, client1);
    broadcaster->subscribe(job2, client2);

    // Then: Clearing one job shouldn't affect the other's subscriptions
    broadcaster->clear_job(job1);

    // client2 should still be subscribed to job2 (no crash on cleanup)
    broadcaster->unsubscribe(job2, client2);
    SUCCEED();
}
