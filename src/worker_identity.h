#pragma once

#include <string>
#include <vector>
#include <memory>

namespace sandrun {

// Worker identity and cryptographic signing
class WorkerIdentity {
public:
    // Load identity from private key file (PEM format)
    static std::unique_ptr<WorkerIdentity> from_keyfile(const std::string& keyfile_path);

    // Generate new identity (creates new Ed25519 keypair)
    static std::unique_ptr<WorkerIdentity> generate();

    // Get worker ID (base64-encoded public key)
    std::string get_worker_id() const;

    // Get public key (raw bytes)
    std::vector<unsigned char> get_public_key() const;

    // Sign data and return base64-encoded signature
    std::string sign(const std::string& data) const;

    // Verify signature (static utility)
    static bool verify(
        const std::string& data,
        const std::string& signature_b64,
        const std::string& public_key_b64
    );

    // Save private key to file (PEM format)
    bool save_to_file(const std::string& filepath) const;

    // Helper: Encode bytes to base64
    static std::string base64_encode(const unsigned char* data, size_t len);

    // Helper: Decode base64 to bytes
    static std::vector<unsigned char> base64_decode(const std::string& encoded);

private:
    WorkerIdentity() = default;

    std::vector<unsigned char> private_key_;  // 32 bytes for Ed25519
    std::vector<unsigned char> public_key_;   // 32 bytes for Ed25519
};

} // namespace sandrun
