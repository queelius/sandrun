#include "worker_identity.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <fstream>
#include <sstream>
#include <cstring>

namespace sandrun {

std::string WorkerIdentity::base64_encode(const unsigned char* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data, len);
    BIO_flush(bio);

    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    return result;
}

std::vector<unsigned char> WorkerIdentity::base64_decode(const std::string& encoded) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new_mem_buf(encoded.c_str(), encoded.length());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    std::vector<unsigned char> result(encoded.length());
    int decoded_len = BIO_read(bio, result.data(), encoded.length());

    BIO_free_all(bio);

    if (decoded_len > 0) {
        result.resize(decoded_len);
    } else {
        result.clear();
    }

    return result;
}

std::unique_ptr<WorkerIdentity> WorkerIdentity::generate() {
    auto identity = std::unique_ptr<WorkerIdentity>(new WorkerIdentity());

    // Generate Ed25519 keypair
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) {
        return nullptr;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }

    EVP_PKEY_CTX_free(ctx);

    // Extract public key (32 bytes for Ed25519)
    size_t pubkey_len = 32;
    identity->public_key_.resize(pubkey_len);
    if (EVP_PKEY_get_raw_public_key(pkey, identity->public_key_.data(), &pubkey_len) <= 0) {
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    // Extract private key (32 bytes for Ed25519)
    size_t privkey_len = 32;
    identity->private_key_.resize(privkey_len);
    if (EVP_PKEY_get_raw_private_key(pkey, identity->private_key_.data(), &privkey_len) <= 0) {
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    EVP_PKEY_free(pkey);

    return identity;
}

std::unique_ptr<WorkerIdentity> WorkerIdentity::from_keyfile(const std::string& keyfile_path) {
    FILE* fp = fopen(keyfile_path.c_str(), "r");
    if (!fp) {
        return nullptr;
    }

    EVP_PKEY* pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!pkey) {
        return nullptr;
    }

    // Check if it's Ed25519
    if (EVP_PKEY_id(pkey) != EVP_PKEY_ED25519) {
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    auto identity = std::unique_ptr<WorkerIdentity>(new WorkerIdentity());

    // Extract keys
    size_t pubkey_len = 32;
    identity->public_key_.resize(pubkey_len);
    if (EVP_PKEY_get_raw_public_key(pkey, identity->public_key_.data(), &pubkey_len) <= 0) {
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    size_t privkey_len = 32;
    identity->private_key_.resize(privkey_len);
    if (EVP_PKEY_get_raw_private_key(pkey, identity->private_key_.data(), &privkey_len) <= 0) {
        EVP_PKEY_free(pkey);
        return nullptr;
    }

    EVP_PKEY_free(pkey);

    return identity;
}

bool WorkerIdentity::save_to_file(const std::string& filepath) const {
    // Create EVP_PKEY from raw keys
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        nullptr,
        private_key_.data(),
        private_key_.size()
    );

    if (!pkey) {
        return false;
    }

    FILE* fp = fopen(filepath.c_str(), "w");
    if (!fp) {
        EVP_PKEY_free(pkey);
        return false;
    }

    int result = PEM_write_PrivateKey(fp, pkey, nullptr, nullptr, 0, nullptr, nullptr);

    fclose(fp);
    EVP_PKEY_free(pkey);

    return result > 0;
}

std::string WorkerIdentity::get_worker_id() const {
    return base64_encode(public_key_.data(), public_key_.size());
}

std::vector<unsigned char> WorkerIdentity::get_public_key() const {
    return public_key_;
}

std::string WorkerIdentity::sign(const std::string& data) const {
    // Create EVP_PKEY from private key
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        nullptr,
        private_key_.data(),
        private_key_.size()
    );

    if (!pkey) {
        return "";
    }

    // Create signing context
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        EVP_PKEY_free(pkey);
        return "";
    }

    // Initialize signing
    if (EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, pkey) <= 0) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        return "";
    }

    // Sign the data
    size_t sig_len = 64;  // Ed25519 signatures are always 64 bytes
    unsigned char signature[64];

    if (EVP_DigestSign(md_ctx, signature, &sig_len,
                      reinterpret_cast<const unsigned char*>(data.c_str()),
                      data.size()) <= 0) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        return "";
    }

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);

    return base64_encode(signature, sig_len);
}

bool WorkerIdentity::verify(
    const std::string& data,
    const std::string& signature_b64,
    const std::string& public_key_b64
) {
    // Decode public key
    auto public_key_bytes = base64_decode(public_key_b64);
    if (public_key_bytes.size() != 32) {
        return false;
    }

    // Decode signature
    auto signature_bytes = base64_decode(signature_b64);
    if (signature_bytes.size() != 64) {
        return false;
    }

    // Create EVP_PKEY from public key
    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519,
        nullptr,
        public_key_bytes.data(),
        public_key_bytes.size()
    );

    if (!pkey) {
        return false;
    }

    // Create verification context
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // Initialize verification
    if (EVP_DigestVerifyInit(md_ctx, nullptr, nullptr, nullptr, pkey) <= 0) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    // Verify signature
    int result = EVP_DigestVerify(
        md_ctx,
        signature_bytes.data(),
        signature_bytes.size(),
        reinterpret_cast<const unsigned char*>(data.c_str()),
        data.size()
    );

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);

    return result == 1;
}

} // namespace sandrun
