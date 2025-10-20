#pragma once

#include <string>
#include <filesystem>
#include <map>
#include <vector>

namespace sandrun {

// File type categories for rich output classification
enum class FileType {
    IMAGE,      // .png, .jpg, .jpeg, .gif, .bmp, .webp
    MODEL,      // .pt, .pth, .safetensors, .onnx, .h5, .pb
    VIDEO,      // .mp4, .avi, .mov, .mkv, .webm
    AUDIO,      // .mp3, .wav, .flac, .ogg
    DATA,       // .csv, .json, .parquet, .npy, .npz
    TEXT,       // .txt, .log, .md
    ARCHIVE,    // .zip, .tar, .tar.gz, .tgz
    CODE,       // .py, .cpp, .js, .rs, .go
    DOCUMENT,   // .pdf, .docx, .xlsx
    OTHER       // Unknown/other types
};

// File metadata with hash (for verification)
struct FileMetadata {
    std::string path;
    size_t size_bytes;
    std::string sha256_hash;
    FileType type;
};

class FileUtils {
public:
    // Detect file type based on extension
    static FileType detect_file_type(const std::string& filename);

    // Get human-readable file type name
    static std::string file_type_to_string(FileType type);

    // Get MIME type for file
    static std::string get_mime_type(const std::string& filename);

    // Format file size as human-readable string
    static std::string format_file_size(size_t bytes);

    // Check if path matches glob pattern (e.g., "*.png")
    static bool matches_pattern(const std::string& path, const std::string& pattern);

    // Hash utilities (for verification in trustless pools)
    static std::string sha256_file(const std::string& filepath);
    static std::string sha256_string(const std::string& data);
    static std::string bytes_to_hex(const unsigned char* data, size_t len);

    // Get file metadata with hash
    static FileMetadata get_file_metadata(const std::string& filepath);

    // Get metadata for all files in directory (recursive)
    static std::map<std::string, FileMetadata> hash_directory(
        const std::string& dirpath,
        const std::vector<std::string>& patterns = {}  // e.g., {"*.png", "*.json"}
    );

private:
    static const std::map<std::string, FileType> extension_map_;
    static const std::map<FileType, std::string> type_name_map_;
    static const std::map<std::string, std::string> mime_type_map_;
};

} // namespace sandrun
