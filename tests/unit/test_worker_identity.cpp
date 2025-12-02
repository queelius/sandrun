#include <gtest/gtest.h>
#include "worker_identity.h"
#include <filesystem>
#include <fstream>
#include <thread>

namespace sandrun {
namespace {

class WorkerIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory for temporary files
        test_dir = std::filesystem::temp_directory_path() / "sandrun_worker_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // Cleanup test directory
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::path test_dir;
};

// ============================================================================
// Base64 Encoding/Decoding Tests
// ============================================================================

TEST_F(WorkerIdentityTest, Base64EncodeDecodeRoundtrip) {
    // Given: Some test data
    const unsigned char test_data[] = {0x01, 0x02, 0x03, 0x04, 0xFF, 0xFE, 0xFD};
    size_t test_data_len = sizeof(test_data);

    // When: We encode and then decode
    std::string encoded = WorkerIdentity::base64_encode(test_data, test_data_len);
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(encoded);

    // Then: We should get back the original data
    ASSERT_EQ(decoded.size(), test_data_len) << "Decoded size should match original";
    for (size_t i = 0; i < test_data_len; i++) {
        EXPECT_EQ(decoded[i], test_data[i]) << "Byte " << i << " should match";
    }
}

TEST_F(WorkerIdentityTest, Base64EmptyData) {
    // Given: Empty data
    const unsigned char* empty_data = nullptr;

    // When: We encode empty data
    std::string encoded = WorkerIdentity::base64_encode(empty_data, 0);
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(encoded);

    // Then: We should get empty result
    EXPECT_TRUE(decoded.empty()) << "Decoding empty base64 should return empty vector";
}

TEST_F(WorkerIdentityTest, Base64InvalidData) {
    // Given: Invalid base64 string (contains invalid characters)
    std::string invalid_b64 = "!!!invalid!!!";

    // When: We try to decode invalid base64
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(invalid_b64);

    // Then: We should get empty or minimal result (graceful failure)
    // The exact behavior depends on OpenSSL, but it shouldn't crash
    SUCCEED() << "Base64 decode should handle invalid input gracefully";
}

TEST_F(WorkerIdentityTest, Base64Ed25519KeySize) {
    // Given: A 32-byte buffer (Ed25519 key size)
    unsigned char key_data[32];
    for (int i = 0; i < 32; i++) {
        key_data[i] = static_cast<unsigned char>(i);
    }

    // When: We encode and decode
    std::string encoded = WorkerIdentity::base64_encode(key_data, 32);
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(encoded);

    // Then: Size should be preserved
    ASSERT_EQ(decoded.size(), 32) << "Ed25519 key size should be preserved";
}

// ============================================================================
// Key Generation Tests
// ============================================================================

TEST_F(WorkerIdentityTest, GenerateCreatesValidIdentity) {
    // When: We generate a new identity
    auto identity = WorkerIdentity::generate();

    // Then: Identity should be valid
    ASSERT_NE(identity, nullptr) << "Generated identity should not be null";

    // And: Public key should be non-empty
    auto pub_key = identity->get_public_key();
    EXPECT_EQ(pub_key.size(), 32) << "Ed25519 public key should be 32 bytes";

    // And: Worker ID should be base64 of public key
    std::string worker_id = identity->get_worker_id();
    EXPECT_FALSE(worker_id.empty()) << "Worker ID should not be empty";

    // Verify worker ID is valid base64 and correct size
    auto decoded_id = WorkerIdentity::base64_decode(worker_id);
    EXPECT_EQ(decoded_id.size(), 32) << "Worker ID should decode to 32 bytes";
}

TEST_F(WorkerIdentityTest, GenerateCreatesUniqueIdentities) {
    // When: We generate two identities
    auto identity1 = WorkerIdentity::generate();
    auto identity2 = WorkerIdentity::generate();

    // Then: They should have different worker IDs
    ASSERT_NE(identity1, nullptr);
    ASSERT_NE(identity2, nullptr);

    std::string worker_id1 = identity1->get_worker_id();
    std::string worker_id2 = identity2->get_worker_id();

    EXPECT_NE(worker_id1, worker_id2) << "Each generated identity should be unique";
}

// ============================================================================
// File Save/Load Tests
// ============================================================================

TEST_F(WorkerIdentityTest, SaveAndLoadRoundtrip) {
    // Given: A generated identity
    auto original = WorkerIdentity::generate();
    ASSERT_NE(original, nullptr);

    std::string original_worker_id = original->get_worker_id();
    auto original_pubkey = original->get_public_key();

    // When: We save it to a file and load it back
    std::string keyfile = (test_dir / "test_key.pem").string();
    ASSERT_TRUE(original->save_to_file(keyfile)) << "Should successfully save key";

    auto loaded = WorkerIdentity::from_keyfile(keyfile);
    ASSERT_NE(loaded, nullptr) << "Should successfully load key";

    // Then: The loaded identity should match the original
    EXPECT_EQ(loaded->get_worker_id(), original_worker_id)
        << "Worker ID should match after save/load";
    EXPECT_EQ(loaded->get_public_key(), original_pubkey)
        << "Public key should match after save/load";
}

TEST_F(WorkerIdentityTest, SaveToInvalidPath) {
    // Given: A generated identity
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    // When: We try to save to an invalid path
    std::string invalid_path = "/invalid/directory/that/doesnt/exist/key.pem";
    bool result = identity->save_to_file(invalid_path);

    // Then: Save should fail gracefully
    EXPECT_FALSE(result) << "Should fail to save to invalid path";
}

TEST_F(WorkerIdentityTest, LoadFromNonexistentFile) {
    // Given: A nonexistent file path
    std::string nonexistent = (test_dir / "nonexistent.pem").string();

    // When: We try to load from it
    auto identity = WorkerIdentity::from_keyfile(nonexistent);

    // Then: Load should fail gracefully
    EXPECT_EQ(identity, nullptr) << "Should return nullptr for nonexistent file";
}

TEST_F(WorkerIdentityTest, LoadFromInvalidPEMFile) {
    // Given: A file with invalid PEM content
    std::string invalid_pem = (test_dir / "invalid.pem").string();
    std::ofstream out(invalid_pem);
    out << "This is not a valid PEM file\n";
    out << "Just some random text\n";
    out.close();

    // When: We try to load it
    auto identity = WorkerIdentity::from_keyfile(invalid_pem);

    // Then: Load should fail gracefully
    EXPECT_EQ(identity, nullptr) << "Should return nullptr for invalid PEM file";
}

TEST_F(WorkerIdentityTest, LoadFromWrongKeyType) {
    // Given: A PEM file with a non-Ed25519 key (e.g., RSA)
    // We'll create a minimal invalid PEM structure
    std::string wrong_type_pem = (test_dir / "wrong_type.pem").string();
    std::ofstream out(wrong_type_pem);
    out << "-----BEGIN RSA PRIVATE KEY-----\n";
    out << "AAAA\n";  // Invalid but parseable PEM structure
    out << "-----END RSA PRIVATE KEY-----\n";
    out.close();

    // When: We try to load it
    auto identity = WorkerIdentity::from_keyfile(wrong_type_pem);

    // Then: Load should fail (since it's not Ed25519)
    EXPECT_EQ(identity, nullptr) << "Should reject non-Ed25519 keys";
}

TEST_F(WorkerIdentityTest, SavedKeyFileIsReadable) {
    // Given: A saved key file
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string keyfile = (test_dir / "readable_key.pem").string();
    ASSERT_TRUE(identity->save_to_file(keyfile));

    // When: We read the file content
    std::ifstream in(keyfile);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

    // Then: It should be a valid PEM file
    EXPECT_TRUE(content.find("-----BEGIN PRIVATE KEY-----") != std::string::npos)
        << "Should contain PEM header";
    EXPECT_TRUE(content.find("-----END PRIVATE KEY-----") != std::string::npos)
        << "Should contain PEM footer";
}

// ============================================================================
// Signing Tests
// ============================================================================

TEST_F(WorkerIdentityTest, SignProducesValidSignature) {
    // Given: An identity and some data
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Hello, cryptographic world!";

    // When: We sign the data
    std::string signature = identity->sign(data);

    // Then: Signature should be non-empty and valid base64
    EXPECT_FALSE(signature.empty()) << "Signature should not be empty";

    auto sig_bytes = WorkerIdentity::base64_decode(signature);
    EXPECT_EQ(sig_bytes.size(), 64) << "Ed25519 signature should be 64 bytes";
}

TEST_F(WorkerIdentityTest, SignEmptyData) {
    // Given: An identity and empty data
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    // When: We sign empty data
    std::string signature = identity->sign("");

    // Then: Should still produce a valid signature
    EXPECT_FALSE(signature.empty()) << "Should produce signature even for empty data";

    auto sig_bytes = WorkerIdentity::base64_decode(signature);
    EXPECT_EQ(sig_bytes.size(), 64) << "Signature should still be 64 bytes";
}

TEST_F(WorkerIdentityTest, SignDeterminism) {
    // Given: An identity and some data
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Deterministic test data";

    // When: We sign the same data twice
    std::string sig1 = identity->sign(data);
    std::string sig2 = identity->sign(data);

    // Then: Signatures should be identical (Ed25519 is deterministic)
    EXPECT_EQ(sig1, sig2) << "Same key + data should produce same signature";
}

TEST_F(WorkerIdentityTest, DifferentDataProducesDifferentSignatures) {
    // Given: An identity and two different data strings
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data1 = "First message";
    std::string data2 = "Second message";

    // When: We sign both
    std::string sig1 = identity->sign(data1);
    std::string sig2 = identity->sign(data2);

    // Then: Signatures should be different
    EXPECT_NE(sig1, sig2) << "Different data should produce different signatures";
}

TEST_F(WorkerIdentityTest, DifferentKeysProduceDifferentSignatures) {
    // Given: Two different identities and the same data
    auto identity1 = WorkerIdentity::generate();
    auto identity2 = WorkerIdentity::generate();
    ASSERT_NE(identity1, nullptr);
    ASSERT_NE(identity2, nullptr);

    std::string data = "Same data for both";

    // When: Both sign the same data
    std::string sig1 = identity1->sign(data);
    std::string sig2 = identity2->sign(data);

    // Then: Signatures should be different
    EXPECT_NE(sig1, sig2) << "Different keys should produce different signatures";
}

// ============================================================================
// Verification Tests
// ============================================================================

TEST_F(WorkerIdentityTest, VerifyValidSignature) {
    // Given: An identity, data, and its signature
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Data to verify";
    std::string signature = identity->sign(data);
    std::string worker_id = identity->get_worker_id();

    // When: We verify the signature
    bool valid = WorkerIdentity::verify(data, signature, worker_id);

    // Then: Verification should succeed
    EXPECT_TRUE(valid) << "Valid signature should verify successfully";
}

TEST_F(WorkerIdentityTest, VerifyRejectsModifiedData) {
    // Given: An identity, data, and its signature
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string original_data = "Original data";
    std::string signature = identity->sign(original_data);
    std::string worker_id = identity->get_worker_id();

    // When: We try to verify with modified data
    std::string modified_data = "Modified data";
    bool valid = WorkerIdentity::verify(modified_data, signature, worker_id);

    // Then: Verification should fail
    EXPECT_FALSE(valid) << "Signature should not verify with modified data";
}

TEST_F(WorkerIdentityTest, VerifyRejectsModifiedSignature) {
    // Given: An identity, data, and a modified signature
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Test data";
    std::string signature = identity->sign(data);
    std::string worker_id = identity->get_worker_id();

    // Corrupt the signature by flipping a bit
    auto sig_bytes = WorkerIdentity::base64_decode(signature);
    ASSERT_FALSE(sig_bytes.empty());
    sig_bytes[0] ^= 0x01;  // Flip one bit
    std::string corrupted_sig = WorkerIdentity::base64_encode(sig_bytes.data(), sig_bytes.size());

    // When: We verify with corrupted signature
    bool valid = WorkerIdentity::verify(data, corrupted_sig, worker_id);

    // Then: Verification should fail
    EXPECT_FALSE(valid) << "Corrupted signature should not verify";
}

TEST_F(WorkerIdentityTest, VerifyRejectsWrongKey) {
    // Given: Two identities, data signed by one
    auto identity1 = WorkerIdentity::generate();
    auto identity2 = WorkerIdentity::generate();
    ASSERT_NE(identity1, nullptr);
    ASSERT_NE(identity2, nullptr);

    std::string data = "Test data";
    std::string signature = identity1->sign(data);
    std::string wrong_worker_id = identity2->get_worker_id();

    // When: We try to verify with wrong key
    bool valid = WorkerIdentity::verify(data, signature, wrong_worker_id);

    // Then: Verification should fail
    EXPECT_FALSE(valid) << "Signature should not verify with wrong public key";
}

TEST_F(WorkerIdentityTest, VerifyRejectsInvalidBase64Signature) {
    // Given: An identity and invalid base64 signature
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Test data";
    std::string invalid_sig = "!!!invalid_base64!!!";
    std::string worker_id = identity->get_worker_id();

    // When: We verify with invalid signature
    bool valid = WorkerIdentity::verify(data, invalid_sig, worker_id);

    // Then: Verification should fail gracefully
    EXPECT_FALSE(valid) << "Invalid base64 signature should fail verification";
}

TEST_F(WorkerIdentityTest, VerifyRejectsInvalidBase64PublicKey) {
    // Given: An identity, valid signature, but invalid public key
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Test data";
    std::string signature = identity->sign(data);
    std::string invalid_pubkey = "!!!invalid_base64!!!";

    // When: We verify with invalid public key
    bool valid = WorkerIdentity::verify(data, signature, invalid_pubkey);

    // Then: Verification should fail gracefully
    EXPECT_FALSE(valid) << "Invalid base64 public key should fail verification";
}

TEST_F(WorkerIdentityTest, VerifyRejectsWrongSizeSignature) {
    // Given: An identity and wrong-size signature
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Test data";
    std::string worker_id = identity->get_worker_id();

    // Create a signature with wrong size (32 bytes instead of 64)
    unsigned char wrong_size[32] = {0};
    std::string wrong_sig = WorkerIdentity::base64_encode(wrong_size, 32);

    // When: We verify with wrong-size signature
    bool valid = WorkerIdentity::verify(data, wrong_sig, worker_id);

    // Then: Verification should fail
    EXPECT_FALSE(valid) << "Wrong-size signature should fail verification";
}

TEST_F(WorkerIdentityTest, VerifyRejectsWrongSizePublicKey) {
    // Given: An identity, valid signature, but wrong-size public key
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "Test data";
    std::string signature = identity->sign(data);

    // Create a public key with wrong size (16 bytes instead of 32)
    unsigned char wrong_size[16] = {0};
    std::string wrong_pubkey = WorkerIdentity::base64_encode(wrong_size, 16);

    // When: We verify with wrong-size public key
    bool valid = WorkerIdentity::verify(data, signature, wrong_pubkey);

    // Then: Verification should fail
    EXPECT_FALSE(valid) << "Wrong-size public key should fail verification";
}

// ============================================================================
// Cross-Worker Verification Tests
// ============================================================================

TEST_F(WorkerIdentityTest, CrossWorkerVerification) {
    // Given: Worker A signs some data
    auto worker_a = WorkerIdentity::generate();
    ASSERT_NE(worker_a, nullptr);

    std::string data = "Job results from worker A";
    std::string signature = worker_a->sign(data);
    std::string worker_a_id = worker_a->get_worker_id();

    // When: Anyone (including other workers) can verify
    bool valid = WorkerIdentity::verify(data, signature, worker_a_id);

    // Then: Verification should succeed (public verification)
    EXPECT_TRUE(valid) << "Any party should be able to verify signatures";
}

TEST_F(WorkerIdentityTest, VerifyAfterSaveLoad) {
    // Given: A worker creates and saves a key, then signs data
    auto original = WorkerIdentity::generate();
    ASSERT_NE(original, nullptr);

    std::string keyfile = (test_dir / "verify_key.pem").string();
    ASSERT_TRUE(original->save_to_file(keyfile));

    std::string data = "Data signed before save/load";
    std::string signature = original->sign(data);
    std::string original_id = original->get_worker_id();

    // When: Worker reloads the key and tries to verify
    auto reloaded = WorkerIdentity::from_keyfile(keyfile);
    ASSERT_NE(reloaded, nullptr);

    std::string reloaded_id = reloaded->get_worker_id();

    // Then: Verification should work with both identities
    EXPECT_TRUE(WorkerIdentity::verify(data, signature, original_id))
        << "Should verify with original identity";
    EXPECT_TRUE(WorkerIdentity::verify(data, signature, reloaded_id))
        << "Should verify with reloaded identity";

    // And: Reloaded identity can create new signatures that verify
    std::string new_data = "Data signed after reload";
    std::string new_signature = reloaded->sign(new_data);
    EXPECT_TRUE(WorkerIdentity::verify(new_data, new_signature, reloaded_id))
        << "Reloaded identity should produce valid signatures";
}

// ============================================================================
// Complex Data Signing Tests (Job Result Format)
// ============================================================================

TEST_F(WorkerIdentityTest, SignJobResultFormat) {
    // Given: An identity and job result data in the expected format
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    // Simulate job result format: job_hash|exit_code|cpu|mem|file1:hash1|file2:hash2
    std::string job_hash = "abc123def456";
    int exit_code = 0;
    double cpu_seconds = 1.234;
    size_t memory_mb = 512;
    std::string output1_hash = "hash1234";
    std::string output2_hash = "hash5678";

    std::ostringstream result_data;
    result_data << job_hash << "|"
                << exit_code << "|"
                << cpu_seconds << "|"
                << memory_mb << "|"
                << "output1.txt:" << output1_hash << "|"
                << "output2.txt:" << output2_hash << "|";

    // When: We sign this job result
    std::string signature = identity->sign(result_data.str());
    std::string worker_id = identity->get_worker_id();

    // Then: Signature should verify
    EXPECT_TRUE(WorkerIdentity::verify(result_data.str(), signature, worker_id))
        << "Job result signature should verify";

    // And: Tampering with any field should invalidate signature
    std::ostringstream tampered;
    tampered << job_hash << "|"
             << (exit_code + 1) << "|"  // Modified exit code
             << cpu_seconds << "|"
             << memory_mb << "|"
             << "output1.txt:" << output1_hash << "|"
             << "output2.txt:" << output2_hash << "|";

    EXPECT_FALSE(WorkerIdentity::verify(tampered.str(), signature, worker_id))
        << "Tampered result should fail verification";
}

TEST_F(WorkerIdentityTest, SignLargeData) {
    // Given: An identity and large data (simulate large output hashes)
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::ostringstream large_data;
    for (int i = 0; i < 1000; i++) {
        large_data << "file" << i << ":hash" << i << "|";
    }

    // When: We sign large data
    std::string signature = identity->sign(large_data.str());
    std::string worker_id = identity->get_worker_id();

    // Then: Should handle large data correctly
    EXPECT_FALSE(signature.empty()) << "Should produce signature for large data";
    EXPECT_TRUE(WorkerIdentity::verify(large_data.str(), signature, worker_id))
        << "Large data signature should verify";
}

TEST_F(WorkerIdentityTest, SignSpecialCharacters) {
    // Given: Data with special characters
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string special_data = "job_hash|0|1.5|512|file with spaces.txt:hash|"
                                "file\nwith\nnewlines:hash|file\twith\ttabs:hash|";

    // When: We sign data with special characters
    std::string signature = identity->sign(special_data);
    std::string worker_id = identity->get_worker_id();

    // Then: Should handle special characters correctly
    EXPECT_TRUE(WorkerIdentity::verify(special_data, signature, worker_id))
        << "Should handle special characters in signed data";
}

// ============================================================================
// Malformed Data Handling Tests
// ============================================================================

TEST_F(WorkerIdentityTest, Verify_HandlesCorruptedBase64Gracefully) {
    // Given: A valid signature
    auto worker = WorkerIdentity::generate();
    ASSERT_NE(worker, nullptr);

    std::string data = "test_data";
    std::string valid_signature = worker->sign(data);
    std::string worker_id = worker->get_worker_id();

    // When: Signature is corrupted (invalid base64)
    std::vector<std::string> corrupted_signatures = {
        "not!valid@base64",           // Invalid characters
        "YWJj",                        // Too short
        "",                            // Empty
        "====",                        // Just padding
        valid_signature + "CORRUPTED" // Appended garbage
    };

    // Then: Should return false (not crash or throw)
    for (const auto& corrupted : corrupted_signatures) {
        EXPECT_FALSE(WorkerIdentity::verify(data, corrupted, worker_id))
            << "Should gracefully reject corrupted signature: " << corrupted;
    }
}

TEST_F(WorkerIdentityTest, FromKeyfile_HandlesCorruptedPEM) {
    // Given: A corrupted PEM file
    std::string corrupt_pem = (test_dir / "corrupt.pem").string();
    std::ofstream file(corrupt_pem);
    file << "-----BEGIN PRIVATE KEY-----\n"
         << "CORRUPTED_BASE64_DATA_HERE\n"
         << "-----END PRIVATE KEY-----\n";
    file.close();

    // When: Attempting to load
    auto identity = WorkerIdentity::from_keyfile(corrupt_pem);

    // Then: Should return nullptr (not crash)
    EXPECT_EQ(identity, nullptr)
        << "Should gracefully reject corrupted PEM file";
}

// ============================================================================
// Additional Base64 Edge Cases
// ============================================================================

TEST_F(WorkerIdentityTest, Base64_SingleByte) {
    // Given: A single byte
    unsigned char single_byte[] = {0xAB};

    // When: Encoding and decoding
    std::string encoded = WorkerIdentity::base64_encode(single_byte, 1);
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(encoded);

    // Then: Should roundtrip correctly
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0], 0xAB);
}

TEST_F(WorkerIdentityTest, Base64_AllZeros) {
    // Given: All zero bytes (32 bytes like Ed25519 key)
    unsigned char zeros[32] = {0};

    // When: Encoding and decoding
    std::string encoded = WorkerIdentity::base64_encode(zeros, 32);
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(encoded);

    // Then: Should preserve zeros
    ASSERT_EQ(decoded.size(), 32);
    for (size_t i = 0; i < 32; i++) {
        EXPECT_EQ(decoded[i], 0);
    }
}

TEST_F(WorkerIdentityTest, Base64_AllOnes) {
    // Given: All 0xFF bytes
    unsigned char ones[32];
    std::fill(ones, ones + 32, 0xFF);

    // When: Encoding and decoding
    std::string encoded = WorkerIdentity::base64_encode(ones, 32);
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(encoded);

    // Then: Should preserve 0xFF values
    ASSERT_EQ(decoded.size(), 32);
    for (size_t i = 0; i < 32; i++) {
        EXPECT_EQ(decoded[i], 0xFF);
    }
}

TEST_F(WorkerIdentityTest, Base64_LargeData) {
    // Given: Moderately large data (64KB - fast enough for CI)
    std::vector<unsigned char> large_data(64 * 1024);
    for (size_t i = 0; i < large_data.size(); i++) {
        large_data[i] = static_cast<unsigned char>(i % 256);
    }

    // When: Encoding and decoding
    std::string encoded = WorkerIdentity::base64_encode(large_data.data(), large_data.size());
    std::vector<unsigned char> decoded = WorkerIdentity::base64_decode(encoded);

    // Then: Should roundtrip correctly
    EXPECT_EQ(decoded.size(), large_data.size());
    // Spot check some values
    EXPECT_EQ(decoded[0], 0);
    EXPECT_EQ(decoded[255], 255);
    EXPECT_EQ(decoded[1000], 1000 % 256);
}

// ============================================================================
// Signature Edge Cases
// ============================================================================

TEST_F(WorkerIdentityTest, SignBinaryData) {
    // Given: Binary data with null bytes
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string binary_data;
    binary_data.push_back('\0');
    binary_data.push_back('A');
    binary_data.push_back('\0');
    binary_data.push_back('B');

    // When: Signing binary data
    std::string signature = identity->sign(binary_data);
    std::string worker_id = identity->get_worker_id();

    // Then: Should sign and verify correctly
    EXPECT_FALSE(signature.empty());
    EXPECT_TRUE(WorkerIdentity::verify(binary_data, signature, worker_id));
}

TEST_F(WorkerIdentityTest, SignUnicodeData) {
    // Given: Unicode data
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string unicode_data = "Hello 世界 🌍 Привет";

    // When: Signing unicode data
    std::string signature = identity->sign(unicode_data);
    std::string worker_id = identity->get_worker_id();

    // Then: Should sign and verify correctly
    EXPECT_FALSE(signature.empty());
    EXPECT_TRUE(WorkerIdentity::verify(unicode_data, signature, worker_id));
}

TEST_F(WorkerIdentityTest, SignVeryLongData) {
    // Given: Long data (100KB - fast enough for CI)
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string long_data(100 * 1024, 'X');

    // When: Signing long data
    std::string signature = identity->sign(long_data);
    std::string worker_id = identity->get_worker_id();

    // Then: Should sign and verify correctly
    EXPECT_FALSE(signature.empty());
    EXPECT_TRUE(WorkerIdentity::verify(long_data, signature, worker_id));
}

// ============================================================================
// Verification Edge Cases
// ============================================================================

TEST_F(WorkerIdentityTest, VerifyEmptyPublicKey) {
    // Given: An identity with valid signature
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "test";
    std::string signature = identity->sign(data);

    // When: Verifying with empty public key
    bool valid = WorkerIdentity::verify(data, signature, "");

    // Then: Should reject gracefully
    EXPECT_FALSE(valid);
}

TEST_F(WorkerIdentityTest, VerifyEmptySignature) {
    // Given: An identity
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string data = "test";
    std::string worker_id = identity->get_worker_id();

    // When: Verifying with empty signature
    bool valid = WorkerIdentity::verify(data, "", worker_id);

    // Then: Should reject gracefully
    EXPECT_FALSE(valid);
}

TEST_F(WorkerIdentityTest, VerifyEmptyData) {
    // Given: Signature of empty data
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    std::string empty_data = "";
    std::string signature = identity->sign(empty_data);
    std::string worker_id = identity->get_worker_id();

    // When: Verifying empty data
    bool valid = WorkerIdentity::verify(empty_data, signature, worker_id);

    // Then: Should verify successfully
    EXPECT_TRUE(valid);
}

// ============================================================================
// Worker ID Format Tests
// ============================================================================

TEST_F(WorkerIdentityTest, WorkerIDIsConsistentBase64) {
    // Given: Multiple accesses to the same identity
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    // When: Getting worker ID multiple times
    std::string id1 = identity->get_worker_id();
    std::string id2 = identity->get_worker_id();
    std::string id3 = identity->get_worker_id();

    // Then: Should always return the same value
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(id2, id3);

    // And: Should be valid base64 that decodes to 32 bytes
    auto decoded = WorkerIdentity::base64_decode(id1);
    EXPECT_EQ(decoded.size(), 32);
}

TEST_F(WorkerIdentityTest, PublicKeyMatchesWorkerID) {
    // Given: An identity
    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    // When: Getting public key and worker ID
    auto public_key = identity->get_public_key();
    std::string worker_id = identity->get_worker_id();

    // Then: Worker ID should be base64 of public key
    auto decoded_id = WorkerIdentity::base64_decode(worker_id);

    ASSERT_EQ(decoded_id.size(), public_key.size());
    for (size_t i = 0; i < public_key.size(); i++) {
        EXPECT_EQ(decoded_id[i], public_key[i]);
    }
}

// ============================================================================
// File Operations Edge Cases
// ============================================================================

TEST_F(WorkerIdentityTest, SaveToExistingFile) {
    // Given: An existing file
    std::string existing_file = (test_dir / "existing.pem").string();
    std::ofstream out(existing_file);
    out << "existing content";
    out.close();

    auto identity = WorkerIdentity::generate();
    ASSERT_NE(identity, nullptr);

    // When: Saving to existing file
    bool success = identity->save_to_file(existing_file);

    // Then: Should overwrite successfully
    EXPECT_TRUE(success);

    // And: Should be loadable
    auto loaded = WorkerIdentity::from_keyfile(existing_file);
    EXPECT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->get_worker_id(), identity->get_worker_id());
}

TEST_F(WorkerIdentityTest, LoadEmptyFile) {
    // Given: An empty file
    std::string empty_file = (test_dir / "empty.pem").string();
    std::ofstream out(empty_file);
    out.close();

    // When: Loading empty file
    auto identity = WorkerIdentity::from_keyfile(empty_file);

    // Then: Should fail gracefully
    EXPECT_EQ(identity, nullptr);
}

} // namespace
} // namespace sandrun
