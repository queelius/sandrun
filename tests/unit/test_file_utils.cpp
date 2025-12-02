#include <gtest/gtest.h>
#include "file_utils.h"
#include <fstream>
#include <filesystem>
#include <vector>

namespace sandrun {
namespace {

class FileUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir = std::filesystem::temp_directory_path() / "file_utils_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // Cleanup test directory
        std::filesystem::remove_all(test_dir);
    }

    // Helper to create a test file with specific content
    std::string create_test_file(const std::string& filename, const std::string& content) {
        std::string filepath = test_dir / filename;
        std::ofstream file(filepath);
        file << content;
        file.close();
        return filepath;
    }

    std::filesystem::path test_dir;
};

// ============================================================================
// SHA256 String Hashing Tests
// ============================================================================

TEST_F(FileUtilsTest, SHA256String_KnownInput) {
    // Given: Known test inputs with expected SHA256 hashes
    // When: Hashing these inputs
    // Then: Should produce the correct SHA256 hash

    // Empty string
    std::string empty_hash = FileUtils::sha256_string("");
    EXPECT_EQ(empty_hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
        << "Empty string should have known SHA256 hash";

    // "hello world"
    std::string hello_hash = FileUtils::sha256_string("hello world");
    EXPECT_EQ(hello_hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9")
        << "Known input should produce known hash";

    // "The quick brown fox jumps over the lazy dog"
    std::string fox_hash = FileUtils::sha256_string("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(fox_hash, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592")
        << "Classic test string should match expected hash";
}

TEST_F(FileUtilsTest, SHA256String_Deterministic) {
    // Given: Same input string
    // When: Hashing multiple times
    // Then: Should produce identical results (determinism)

    std::string input = "determinism test 12345";
    std::string hash1 = FileUtils::sha256_string(input);
    std::string hash2 = FileUtils::sha256_string(input);
    std::string hash3 = FileUtils::sha256_string(input);

    EXPECT_EQ(hash1, hash2) << "Same input should produce same hash";
    EXPECT_EQ(hash2, hash3) << "Hash function should be deterministic";
    EXPECT_EQ(hash1.length(), 64) << "SHA256 hash should be 64 hex characters";
}

TEST_F(FileUtilsTest, SHA256String_Collision_Resistance) {
    // Given: Similar but different inputs
    // When: Hashing these inputs
    // Then: Should produce different hashes (collision resistance)

    std::string hash1 = FileUtils::sha256_string("test");
    std::string hash2 = FileUtils::sha256_string("test ");
    std::string hash3 = FileUtils::sha256_string("Test");
    std::string hash4 = FileUtils::sha256_string("test\n");

    EXPECT_NE(hash1, hash2) << "Different inputs should produce different hashes";
    EXPECT_NE(hash1, hash3) << "Case sensitivity should be preserved";
    EXPECT_NE(hash1, hash4) << "Whitespace should affect hash";

    // All should be different
    EXPECT_NE(hash2, hash3);
    EXPECT_NE(hash2, hash4);
    EXPECT_NE(hash3, hash4);
}

TEST_F(FileUtilsTest, SHA256String_BinaryData) {
    // Given: Binary data (not just text)
    // When: Hashing binary content
    // Then: Should handle binary data correctly

    std::string binary_data;
    for (int i = 0; i < 256; i++) {
        binary_data += static_cast<char>(i);
    }

    std::string hash = FileUtils::sha256_string(binary_data);
    EXPECT_EQ(hash.length(), 64) << "Should handle binary data";

    // Hash of same binary data should be consistent
    std::string hash2 = FileUtils::sha256_string(binary_data);
    EXPECT_EQ(hash, hash2) << "Binary data hashing should be deterministic";
}

// ============================================================================
// SHA256 File Hashing Tests
// ============================================================================

TEST_F(FileUtilsTest, SHA256File_BasicFile) {
    // Given: A file with known content
    // When: Hashing the file
    // Then: Should produce correct hash of file contents

    std::string filepath = create_test_file("test1.txt", "hello world");
    std::string hash = FileUtils::sha256_file(filepath);

    // Should match the hash of the string "hello world"
    EXPECT_EQ(hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
}

TEST_F(FileUtilsTest, SHA256File_EmptyFile) {
    // Given: An empty file
    // When: Hashing the empty file
    // Then: Should produce hash of empty string

    std::string filepath = create_test_file("empty.txt", "");
    std::string hash = FileUtils::sha256_file(filepath);

    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
        << "Empty file should have same hash as empty string";
}

TEST_F(FileUtilsTest, SHA256File_LargeFile) {
    // Given: A large file (tests buffered reading)
    // When: Hashing the large file
    // Then: Should correctly hash entire file

    std::string large_content;
    large_content.reserve(100000);
    for (int i = 0; i < 10000; i++) {
        large_content += "0123456789";
    }

    std::string filepath = create_test_file("large.txt", large_content);
    std::string file_hash = FileUtils::sha256_file(filepath);
    std::string string_hash = FileUtils::sha256_string(large_content);

    EXPECT_EQ(file_hash, string_hash)
        << "Large file hash should match string hash of same content";
}

TEST_F(FileUtilsTest, SHA256File_BinaryFile) {
    // Given: A binary file
    // When: Hashing the binary file
    // Then: Should correctly hash binary content

    std::string filepath = test_dir / "binary.dat";
    std::ofstream file(filepath, std::ios::binary);
    for (int i = 0; i < 256; i++) {
        file.put(static_cast<char>(i));
    }
    file.close();

    std::string hash = FileUtils::sha256_file(filepath);
    EXPECT_EQ(hash.length(), 64) << "Binary file should produce valid hash";
    EXPECT_NE(hash, "") << "Binary file hash should not be empty";
}

TEST_F(FileUtilsTest, SHA256File_NonExistentFile) {
    // Given: A non-existent file path
    // When: Attempting to hash it
    // Then: Should return empty string (error case)

    std::string hash = FileUtils::sha256_file("/nonexistent/file.txt");
    EXPECT_EQ(hash, "") << "Non-existent file should return empty hash";
}

TEST_F(FileUtilsTest, SHA256File_Deterministic) {
    // Given: Same file
    // When: Hashing multiple times
    // Then: Should produce identical results

    std::string filepath = create_test_file("deterministic.txt", "test content");
    std::string hash1 = FileUtils::sha256_file(filepath);
    std::string hash2 = FileUtils::sha256_file(filepath);
    std::string hash3 = FileUtils::sha256_file(filepath);

    EXPECT_EQ(hash1, hash2) << "File hash should be deterministic";
    EXPECT_EQ(hash2, hash3) << "File hash should be consistent";
}

// ============================================================================
// File Metadata Tests
// ============================================================================

TEST_F(FileUtilsTest, GetFileMetadata_BasicFile) {
    // Given: A file with known properties
    // When: Getting its metadata
    // Then: Should return correct size, hash, and type

    std::string content = "test content for metadata";
    std::string filepath = create_test_file("metadata.txt", content);

    FileMetadata metadata = FileUtils::get_file_metadata(filepath);

    EXPECT_EQ(metadata.size_bytes, content.length());
    EXPECT_EQ(metadata.sha256_hash, FileUtils::sha256_string(content));
    EXPECT_EQ(metadata.type, FileType::TEXT) << "Should detect .txt as TEXT type";
}

TEST_F(FileUtilsTest, GetFileMetadata_DetectsCommonFileTypes) {
    // Test representative file types only
    std::vector<std::pair<std::string, FileType>> test_cases = {
        {"image.png", FileType::IMAGE},      // Visual output
        {"model.pt", FileType::MODEL},       // ML model
        {"data.csv", FileType::DATA},        // Structured data
        {"unknown.xyz", FileType::OTHER}     // Fallback
    };

    for (const auto& [filename, expected_type] : test_cases) {
        std::string filepath = create_test_file(filename, "test");
        FileMetadata metadata = FileUtils::get_file_metadata(filepath);

        EXPECT_EQ(metadata.type, expected_type)
            << "File " << filename << " should be detected as "
            << FileUtils::file_type_to_string(expected_type);
    }
}

TEST_F(FileUtilsTest, GetFileMetadata_NonExistentFile) {
    // Given: A non-existent file
    // When: Getting metadata
    // Then: Should return zero size and empty hash

    FileMetadata metadata = FileUtils::get_file_metadata("/nonexistent/file.txt");

    EXPECT_EQ(metadata.size_bytes, 0);
    EXPECT_EQ(metadata.sha256_hash, "");
    EXPECT_EQ(metadata.type, FileType::OTHER);
}

TEST_F(FileUtilsTest, GetFileMetadata_EmptyFile) {
    // Given: An empty file
    // When: Getting metadata
    // Then: Should return zero size but valid hash

    std::string filepath = create_test_file("empty.txt", "");
    FileMetadata metadata = FileUtils::get_file_metadata(filepath);

    EXPECT_EQ(metadata.size_bytes, 0);
    EXPECT_EQ(metadata.sha256_hash,
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(metadata.type, FileType::TEXT);
}

// ============================================================================
// Directory Hashing Tests
// ============================================================================

TEST_F(FileUtilsTest, HashDirectory_AllFiles) {
    // Given: A directory with multiple files
    // When: Hashing directory without patterns (all files)
    // Then: Should hash all files

    create_test_file("file1.txt", "content1");
    create_test_file("file2.py", "content2");
    create_test_file("file3.csv", "content3");

    auto result = FileUtils::hash_directory(test_dir.string());

    EXPECT_EQ(result.size(), 3) << "Should hash all 3 files";
    EXPECT_TRUE(result.count("file1.txt") > 0);
    EXPECT_TRUE(result.count("file2.py") > 0);
    EXPECT_TRUE(result.count("file3.csv") > 0);

    // Verify hashes are correct
    EXPECT_EQ(result["file1.txt"].sha256_hash, FileUtils::sha256_string("content1"));
    EXPECT_EQ(result["file2.py"].sha256_hash, FileUtils::sha256_string("content2"));
}

TEST_F(FileUtilsTest, HashDirectory_WithGlobPattern) {
    // Given: A directory with files of different types
    // When: Hashing with specific glob patterns
    // Then: Should only hash matching files

    create_test_file("file1.txt", "content1");
    create_test_file("file2.txt", "content2");
    create_test_file("script.py", "print('hello')");
    create_test_file("data.csv", "a,b,c");

    // Test: Only .txt files
    auto txt_result = FileUtils::hash_directory(test_dir.string(), {"*.txt"});
    EXPECT_EQ(txt_result.size(), 2) << "Should only match .txt files";
    EXPECT_TRUE(txt_result.count("file1.txt") > 0);
    EXPECT_TRUE(txt_result.count("file2.txt") > 0);
    EXPECT_FALSE(txt_result.count("script.py") > 0);

    // Test: Only .py files
    auto py_result = FileUtils::hash_directory(test_dir.string(), {"*.py"});
    EXPECT_EQ(py_result.size(), 1) << "Should only match .py files";
    EXPECT_TRUE(py_result.count("script.py") > 0);

    // Test: Multiple patterns
    auto multi_result = FileUtils::hash_directory(test_dir.string(), {"*.txt", "*.csv"});
    EXPECT_EQ(multi_result.size(), 3) << "Should match .txt and .csv files";
    EXPECT_TRUE(multi_result.count("file1.txt") > 0);
    EXPECT_TRUE(multi_result.count("data.csv") > 0);
    EXPECT_FALSE(multi_result.count("script.py") > 0);
}

TEST_F(FileUtilsTest, HashDirectory_RecursiveSubdirectories) {
    // Given: A directory with subdirectories
    // When: Hashing the directory
    // Then: Should recursively hash files in subdirectories

    create_test_file("root.txt", "root");

    std::filesystem::create_directories(test_dir / "subdir1");
    create_test_file("subdir1/file1.txt", "sub1");

    std::filesystem::create_directories(test_dir / "subdir2" / "nested");
    create_test_file("subdir2/file2.txt", "sub2");
    create_test_file("subdir2/nested/file3.txt", "nested");

    auto result = FileUtils::hash_directory(test_dir.string());

    EXPECT_EQ(result.size(), 4) << "Should find all 4 files recursively";
    EXPECT_TRUE(result.count("root.txt") > 0);
    EXPECT_TRUE(result.count("subdir1/file1.txt") > 0);
    EXPECT_TRUE(result.count("subdir2/file2.txt") > 0);
    EXPECT_TRUE(result.count("subdir2/nested/file3.txt") > 0);
}

TEST_F(FileUtilsTest, HashDirectory_GlobPatternWithSubdirs) {
    // Given: Files in subdirectories with different extensions
    // When: Using glob patterns
    // Then: Should filter files across all subdirectories

    std::filesystem::create_directories(test_dir / "outputs");
    create_test_file("outputs/result.png", "image");
    create_test_file("outputs/result.json", "data");
    create_test_file("outputs/log.txt", "log");

    auto result = FileUtils::hash_directory(test_dir.string(), {"*.png", "*.json"});

    EXPECT_EQ(result.size(), 2) << "Should match .png and .json in subdirs";
    EXPECT_TRUE(result.count("outputs/result.png") > 0);
    EXPECT_TRUE(result.count("outputs/result.json") > 0);
    EXPECT_FALSE(result.count("outputs/log.txt") > 0);
}

TEST_F(FileUtilsTest, HashDirectory_EmptyDirectory) {
    // Given: An empty directory
    // When: Hashing the directory
    // Then: Should return empty map

    auto result = FileUtils::hash_directory(test_dir.string());
    EXPECT_TRUE(result.empty()) << "Empty directory should return empty map";
}

TEST_F(FileUtilsTest, HashDirectory_NonExistentDirectory) {
    // Given: A non-existent directory
    // When: Attempting to hash it
    // Then: Should return empty map

    auto result = FileUtils::hash_directory("/nonexistent/directory");
    EXPECT_TRUE(result.empty()) << "Non-existent directory should return empty map";
}

TEST_F(FileUtilsTest, HashDirectory_PrefixPattern) {
    // Given: Files with specific prefixes
    // When: Using prefix* pattern
    // Then: Should match files with that prefix

    create_test_file("result_1.txt", "r1");
    create_test_file("result_2.txt", "r2");
    create_test_file("output.txt", "out");
    create_test_file("log.txt", "log");

    auto result = FileUtils::hash_directory(test_dir.string(), {"result_*"});

    EXPECT_EQ(result.size(), 2) << "Should match files starting with 'result_'";
    EXPECT_TRUE(result.count("result_1.txt") > 0);
    EXPECT_TRUE(result.count("result_2.txt") > 0);
    EXPECT_FALSE(result.count("output.txt") > 0);
}

TEST_F(FileUtilsTest, HashDirectory_WildcardAll) {
    // Given: Various files
    // When: Using "*" pattern
    // Then: Should match all files

    create_test_file("file1.txt", "1");
    create_test_file("file2.py", "2");
    create_test_file("file3.csv", "3");

    auto result = FileUtils::hash_directory(test_dir.string(), {"*"});

    EXPECT_EQ(result.size(), 3) << "Wildcard should match all files";
}

// ============================================================================
// Pattern Matching Tests
// ============================================================================

TEST_F(FileUtilsTest, MatchesPattern_ExactMatch) {
    EXPECT_TRUE(FileUtils::matches_pattern("file.txt", "file.txt"));
    EXPECT_FALSE(FileUtils::matches_pattern("file.txt", "other.txt"));
}

TEST_F(FileUtilsTest, MatchesPattern_ExtensionWildcard) {
    EXPECT_TRUE(FileUtils::matches_pattern("file.txt", "*.txt"));
    EXPECT_TRUE(FileUtils::matches_pattern("data.csv", "*.csv"));
    EXPECT_FALSE(FileUtils::matches_pattern("file.txt", "*.csv"));
}

TEST_F(FileUtilsTest, MatchesPattern_PrefixWildcard) {
    EXPECT_TRUE(FileUtils::matches_pattern("output_1.txt", "output_*"));
    EXPECT_TRUE(FileUtils::matches_pattern("output_data.csv", "output_*"));
    EXPECT_FALSE(FileUtils::matches_pattern("input_1.txt", "output_*"));
}

TEST_F(FileUtilsTest, MatchesPattern_MatchAll) {
    EXPECT_TRUE(FileUtils::matches_pattern("anything.txt", "*"));
    EXPECT_TRUE(FileUtils::matches_pattern("data/file.csv", "*"));
    EXPECT_TRUE(FileUtils::matches_pattern("", "*"));
}

TEST_F(FileUtilsTest, MatchesPattern_PathWithDirectory) {
    EXPECT_TRUE(FileUtils::matches_pattern("outputs/result.png", "*.png"));
    EXPECT_TRUE(FileUtils::matches_pattern("data/outputs/file.csv", "*.csv"));
    EXPECT_FALSE(FileUtils::matches_pattern("outputs/result.png", "*.txt"));
}

// ============================================================================
// Hex Conversion Tests
// ============================================================================

TEST_F(FileUtilsTest, BytesToHex_BasicConversion) {
    unsigned char data[] = {0x00, 0x01, 0x0F, 0x10, 0xFF};
    std::string hex = FileUtils::bytes_to_hex(data, 5);

    EXPECT_EQ(hex, "00010f10ff") << "Should convert bytes to lowercase hex";
}

TEST_F(FileUtilsTest, BytesToHex_EmptyInput) {
    unsigned char data[1];  // Dummy array, but we'll use length 0
    std::string hex = FileUtils::bytes_to_hex(data, 0);

    EXPECT_EQ(hex, "") << "Empty input should produce empty string";
}

// ============================================================================
// File Type Detection Tests
// ============================================================================

TEST_F(FileUtilsTest, DetectFileType_Images) {
    EXPECT_EQ(FileUtils::detect_file_type("image.png"), FileType::IMAGE);
    EXPECT_EQ(FileUtils::detect_file_type("photo.jpg"), FileType::IMAGE);
    EXPECT_EQ(FileUtils::detect_file_type("pic.jpeg"), FileType::IMAGE);
    EXPECT_EQ(FileUtils::detect_file_type("icon.gif"), FileType::IMAGE);
}

TEST_F(FileUtilsTest, DetectFileType_Models) {
    EXPECT_EQ(FileUtils::detect_file_type("model.pt"), FileType::MODEL);
    EXPECT_EQ(FileUtils::detect_file_type("weights.pth"), FileType::MODEL);
    EXPECT_EQ(FileUtils::detect_file_type("model.onnx"), FileType::MODEL);
    EXPECT_EQ(FileUtils::detect_file_type("weights.h5"), FileType::MODEL);
}

TEST_F(FileUtilsTest, DetectFileType_CaseInsensitive) {
    EXPECT_EQ(FileUtils::detect_file_type("FILE.PNG"), FileType::IMAGE);
    EXPECT_EQ(FileUtils::detect_file_type("Data.CSV"), FileType::DATA);
    EXPECT_EQ(FileUtils::detect_file_type("Script.PY"), FileType::CODE);
}

TEST_F(FileUtilsTest, FileTypeToString_AllTypes) {
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::IMAGE), "image");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::MODEL), "model");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::VIDEO), "video");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::AUDIO), "audio");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::DATA), "data");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::TEXT), "text");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::ARCHIVE), "archive");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::CODE), "code");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::DOCUMENT), "document");
    EXPECT_EQ(FileUtils::file_type_to_string(FileType::OTHER), "other");
}

// ============================================================================
// MIME Type Tests
// ============================================================================

TEST_F(FileUtilsTest, GetMimeType_Images) {
    // Given: Image file extensions
    // When: Getting MIME type
    // Then: Should return correct image MIME types
    EXPECT_EQ(FileUtils::get_mime_type("photo.png"), "image/png");
    EXPECT_EQ(FileUtils::get_mime_type("photo.jpg"), "image/jpeg");
    EXPECT_EQ(FileUtils::get_mime_type("photo.jpeg"), "image/jpeg");
    EXPECT_EQ(FileUtils::get_mime_type("animation.gif"), "image/gif");
    EXPECT_EQ(FileUtils::get_mime_type("image.bmp"), "image/bmp");
    EXPECT_EQ(FileUtils::get_mime_type("image.webp"), "image/webp");
    EXPECT_EQ(FileUtils::get_mime_type("vector.svg"), "image/svg+xml");
}

TEST_F(FileUtilsTest, GetMimeType_Videos) {
    EXPECT_EQ(FileUtils::get_mime_type("video.mp4"), "video/mp4");
    EXPECT_EQ(FileUtils::get_mime_type("video.avi"), "video/x-msvideo");
    EXPECT_EQ(FileUtils::get_mime_type("video.mov"), "video/quicktime");
    EXPECT_EQ(FileUtils::get_mime_type("video.mkv"), "video/x-matroska");
    EXPECT_EQ(FileUtils::get_mime_type("video.webm"), "video/webm");
}

TEST_F(FileUtilsTest, GetMimeType_Audio) {
    EXPECT_EQ(FileUtils::get_mime_type("audio.mp3"), "audio/mpeg");
    EXPECT_EQ(FileUtils::get_mime_type("audio.wav"), "audio/wav");
    EXPECT_EQ(FileUtils::get_mime_type("audio.ogg"), "audio/ogg");
}

TEST_F(FileUtilsTest, GetMimeType_DataAndText) {
    EXPECT_EQ(FileUtils::get_mime_type("data.csv"), "text/csv");
    EXPECT_EQ(FileUtils::get_mime_type("data.json"), "application/json");
    EXPECT_EQ(FileUtils::get_mime_type("readme.txt"), "text/plain");
    EXPECT_EQ(FileUtils::get_mime_type("readme.md"), "text/markdown");
    EXPECT_EQ(FileUtils::get_mime_type("app.log"), "text/plain");
}

TEST_F(FileUtilsTest, GetMimeType_Archives) {
    EXPECT_EQ(FileUtils::get_mime_type("archive.zip"), "application/zip");
    EXPECT_EQ(FileUtils::get_mime_type("archive.tar"), "application/x-tar");
    EXPECT_EQ(FileUtils::get_mime_type("archive.gz"), "application/gzip");
}

TEST_F(FileUtilsTest, GetMimeType_Code) {
    EXPECT_EQ(FileUtils::get_mime_type("script.py"), "text/x-python");
    EXPECT_EQ(FileUtils::get_mime_type("script.js"), "application/javascript");
    EXPECT_EQ(FileUtils::get_mime_type("code.cpp"), "text/x-c++");
    EXPECT_EQ(FileUtils::get_mime_type("code.c"), "text/x-c");
    EXPECT_EQ(FileUtils::get_mime_type("header.h"), "text/x-c");
}

TEST_F(FileUtilsTest, GetMimeType_Documents) {
    EXPECT_EQ(FileUtils::get_mime_type("doc.pdf"), "application/pdf");
}

TEST_F(FileUtilsTest, GetMimeType_Models) {
    EXPECT_EQ(FileUtils::get_mime_type("model.pt"), "application/octet-stream");
    EXPECT_EQ(FileUtils::get_mime_type("model.pth"), "application/octet-stream");
    EXPECT_EQ(FileUtils::get_mime_type("model.onnx"), "application/octet-stream");
}

TEST_F(FileUtilsTest, GetMimeType_UnknownExtension) {
    // Given: Unknown file extension
    // When: Getting MIME type
    // Then: Should return octet-stream fallback
    EXPECT_EQ(FileUtils::get_mime_type("file.xyz"), "application/octet-stream");
    EXPECT_EQ(FileUtils::get_mime_type("file.unknown"), "application/octet-stream");
    EXPECT_EQ(FileUtils::get_mime_type("noextension"), "application/octet-stream");
}

TEST_F(FileUtilsTest, GetMimeType_CaseInsensitive) {
    // Given: Extension with different cases
    // When: Getting MIME type
    // Then: Should handle case-insensitively
    EXPECT_EQ(FileUtils::get_mime_type("photo.PNG"), "image/png");
    EXPECT_EQ(FileUtils::get_mime_type("photo.Png"), "image/png");
    EXPECT_EQ(FileUtils::get_mime_type("data.JSON"), "application/json");
}

// ============================================================================
// Format File Size Tests
// ============================================================================

TEST_F(FileUtilsTest, FormatFileSize_Bytes) {
    // Given: Small file sizes (< 1KB)
    // When: Formatting size
    // Then: Should show in bytes
    EXPECT_EQ(FileUtils::format_file_size(0), "0.0 B");
    EXPECT_EQ(FileUtils::format_file_size(1), "1.0 B");
    EXPECT_EQ(FileUtils::format_file_size(512), "512.0 B");
    EXPECT_EQ(FileUtils::format_file_size(1023), "1023.0 B");
}

TEST_F(FileUtilsTest, FormatFileSize_Kilobytes) {
    // Given: File sizes 1KB - 1MB
    // When: Formatting size
    // Then: Should show in KB
    EXPECT_EQ(FileUtils::format_file_size(1024), "1.0 KB");
    EXPECT_EQ(FileUtils::format_file_size(1536), "1.5 KB");
    EXPECT_EQ(FileUtils::format_file_size(1024 * 100), "100.0 KB");
    EXPECT_EQ(FileUtils::format_file_size(1024 * 1023), "1023.0 KB");
}

TEST_F(FileUtilsTest, FormatFileSize_Megabytes) {
    // Given: File sizes 1MB - 1GB
    // When: Formatting size
    // Then: Should show in MB
    EXPECT_EQ(FileUtils::format_file_size(1024 * 1024), "1.0 MB");
    EXPECT_EQ(FileUtils::format_file_size(1024 * 1024 * 500), "500.0 MB");
}

TEST_F(FileUtilsTest, FormatFileSize_Gigabytes) {
    // Given: File sizes 1GB - 1TB
    // When: Formatting size
    // Then: Should show in GB
    size_t gb = 1024ULL * 1024 * 1024;
    EXPECT_EQ(FileUtils::format_file_size(gb), "1.0 GB");
    EXPECT_EQ(FileUtils::format_file_size(gb * 5), "5.0 GB");
}

TEST_F(FileUtilsTest, FormatFileSize_Terabytes) {
    // Given: File sizes >= 1TB
    // When: Formatting size
    // Then: Should show in TB
    size_t tb = 1024ULL * 1024 * 1024 * 1024;
    EXPECT_EQ(FileUtils::format_file_size(tb), "1.0 TB");
    EXPECT_EQ(FileUtils::format_file_size(tb * 2), "2.0 TB");
}

// ============================================================================
// Advanced Pattern Matching Tests
// ============================================================================

TEST_F(FileUtilsTest, MatchesPattern_ComplexPatterns) {
    // Given: Pattern with both prefix and suffix (prefix*suffix)
    // When: Matching files
    // Then: Should correctly match prefix AND suffix
    EXPECT_TRUE(FileUtils::matches_pattern("output.txt", "output*.txt"));
    EXPECT_TRUE(FileUtils::matches_pattern("output_data.txt", "output*.txt"));
    EXPECT_FALSE(FileUtils::matches_pattern("output.csv", "output*.txt"));
    EXPECT_FALSE(FileUtils::matches_pattern("input.txt", "output*.txt"));
}

TEST_F(FileUtilsTest, MatchesPattern_SuffixMatch) {
    // Given: Pattern *suffix (star at beginning with single star)
    // When: Matching files
    // Then: Should match paths that END with suffix
    EXPECT_TRUE(FileUtils::matches_pattern("my_file_result", "*result"));
    EXPECT_TRUE(FileUtils::matches_pattern("result", "*result"));
    EXPECT_FALSE(FileUtils::matches_pattern("data_result.csv", "*result"));  // ends with .csv not result
    EXPECT_FALSE(FileUtils::matches_pattern("results", "*result"));  // ends with "s" not "result"
}

TEST_F(FileUtilsTest, MatchesPattern_EmptyString) {
    // Given: Empty path string
    // When: Matching against patterns
    // Then: Should handle correctly
    EXPECT_TRUE(FileUtils::matches_pattern("", "*"));
    EXPECT_FALSE(FileUtils::matches_pattern("", "*.txt"));
    EXPECT_TRUE(FileUtils::matches_pattern("", ""));  // Exact empty match
}

// ============================================================================
// File Type Detection Edge Cases
// ============================================================================

TEST_F(FileUtilsTest, DetectFileType_NoExtension) {
    // Given: Filename without extension
    // When: Detecting file type
    // Then: Should return OTHER
    EXPECT_EQ(FileUtils::detect_file_type("README"), FileType::OTHER);
    EXPECT_EQ(FileUtils::detect_file_type("Makefile"), FileType::OTHER);
    EXPECT_EQ(FileUtils::detect_file_type(".gitignore"), FileType::OTHER);
}

TEST_F(FileUtilsTest, DetectFileType_MultipleExtensions) {
    // Given: Filename with multiple dots
    // When: Detecting file type
    // Then: Should use last extension
    EXPECT_EQ(FileUtils::detect_file_type("data.backup.csv"), FileType::DATA);
    EXPECT_EQ(FileUtils::detect_file_type("model.v2.pt"), FileType::MODEL);
    EXPECT_EQ(FileUtils::detect_file_type("archive.tar.gz"), FileType::ARCHIVE);
}

TEST_F(FileUtilsTest, DetectFileType_AllCategories) {
    // Test all major categories have at least one working extension
    EXPECT_EQ(FileUtils::detect_file_type("file.png"), FileType::IMAGE);
    EXPECT_EQ(FileUtils::detect_file_type("file.pt"), FileType::MODEL);
    EXPECT_EQ(FileUtils::detect_file_type("file.mp4"), FileType::VIDEO);
    EXPECT_EQ(FileUtils::detect_file_type("file.mp3"), FileType::AUDIO);
    EXPECT_EQ(FileUtils::detect_file_type("file.csv"), FileType::DATA);
    EXPECT_EQ(FileUtils::detect_file_type("file.txt"), FileType::TEXT);
    EXPECT_EQ(FileUtils::detect_file_type("file.zip"), FileType::ARCHIVE);
    EXPECT_EQ(FileUtils::detect_file_type("file.py"), FileType::CODE);
    EXPECT_EQ(FileUtils::detect_file_type("file.pdf"), FileType::DOCUMENT);
}

// ============================================================================
// Get File Metadata Edge Cases
// ============================================================================

TEST_F(FileUtilsTest, GetFileMetadata_Directory) {
    // Given: A directory path (not a file)
    // When: Getting metadata
    // Then: Should return empty metadata (handles gracefully)
    std::filesystem::create_directories(test_dir / "subdir");
    FileMetadata metadata = FileUtils::get_file_metadata((test_dir / "subdir").string());

    EXPECT_EQ(metadata.size_bytes, 0);
    EXPECT_EQ(metadata.sha256_hash, "");
    EXPECT_EQ(metadata.type, FileType::OTHER);
}

} // namespace
} // namespace sandrun
