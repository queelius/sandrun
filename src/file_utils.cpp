#include "file_utils.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <openssl/sha.h>

namespace sandrun {

// Extension to FileType mapping
const std::map<std::string, FileType> FileUtils::extension_map_ = {
    // Images
    {".png", FileType::IMAGE},
    {".jpg", FileType::IMAGE},
    {".jpeg", FileType::IMAGE},
    {".gif", FileType::IMAGE},
    {".bmp", FileType::IMAGE},
    {".webp", FileType::IMAGE},
    {".svg", FileType::IMAGE},
    {".tiff", FileType::IMAGE},
    {".ico", FileType::IMAGE},

    // Models
    {".pt", FileType::MODEL},
    {".pth", FileType::MODEL},
    {".safetensors", FileType::MODEL},
    {".onnx", FileType::MODEL},
    {".h5", FileType::MODEL},
    {".pb", FileType::MODEL},
    {".ckpt", FileType::MODEL},
    {".pkl", FileType::MODEL},
    {".joblib", FileType::MODEL},

    // Videos
    {".mp4", FileType::VIDEO},
    {".avi", FileType::VIDEO},
    {".mov", FileType::VIDEO},
    {".mkv", FileType::VIDEO},
    {".webm", FileType::VIDEO},
    {".flv", FileType::VIDEO},
    {".wmv", FileType::VIDEO},

    // Audio
    {".mp3", FileType::AUDIO},
    {".wav", FileType::AUDIO},
    {".flac", FileType::AUDIO},
    {".ogg", FileType::AUDIO},
    {".m4a", FileType::AUDIO},
    {".aac", FileType::AUDIO},

    // Data
    {".csv", FileType::DATA},
    {".json", FileType::DATA},
    {".parquet", FileType::DATA},
    {".npy", FileType::DATA},
    {".npz", FileType::DATA},
    {".hdf5", FileType::DATA},
    {".h5", FileType::DATA},
    {".feather", FileType::DATA},
    {".arrow", FileType::DATA},

    // Text
    {".txt", FileType::TEXT},
    {".log", FileType::TEXT},
    {".md", FileType::TEXT},
    {".rst", FileType::TEXT},

    // Archives
    {".zip", FileType::ARCHIVE},
    {".tar", FileType::ARCHIVE},
    {".gz", FileType::ARCHIVE},
    {".tgz", FileType::ARCHIVE},
    {".bz2", FileType::ARCHIVE},
    {".7z", FileType::ARCHIVE},

    // Code
    {".py", FileType::CODE},
    {".cpp", FileType::CODE},
    {".c", FileType::CODE},
    {".h", FileType::CODE},
    {".hpp", FileType::CODE},
    {".js", FileType::CODE},
    {".ts", FileType::CODE},
    {".rs", FileType::CODE},
    {".go", FileType::CODE},
    {".java", FileType::CODE},
    {".sh", FileType::CODE},

    // Documents
    {".pdf", FileType::DOCUMENT},
    {".docx", FileType::DOCUMENT},
    {".xlsx", FileType::DOCUMENT},
    {".pptx", FileType::DOCUMENT},
};

const std::map<FileType, std::string> FileUtils::type_name_map_ = {
    {FileType::IMAGE, "image"},
    {FileType::MODEL, "model"},
    {FileType::VIDEO, "video"},
    {FileType::AUDIO, "audio"},
    {FileType::DATA, "data"},
    {FileType::TEXT, "text"},
    {FileType::ARCHIVE, "archive"},
    {FileType::CODE, "code"},
    {FileType::DOCUMENT, "document"},
    {FileType::OTHER, "other"},
};

const std::map<std::string, std::string> FileUtils::mime_type_map_ = {
    // Images
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".webp", "image/webp"},
    {".svg", "image/svg+xml"},

    // Videos
    {".mp4", "video/mp4"},
    {".avi", "video/x-msvideo"},
    {".mov", "video/quicktime"},
    {".mkv", "video/x-matroska"},
    {".webm", "video/webm"},

    // Audio
    {".mp3", "audio/mpeg"},
    {".wav", "audio/wav"},
    {".ogg", "audio/ogg"},

    // Data
    {".csv", "text/csv"},
    {".json", "application/json"},

    // Text
    {".txt", "text/plain"},
    {".md", "text/markdown"},
    {".log", "text/plain"},

    // Archives
    {".zip", "application/zip"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},

    // Code
    {".py", "text/x-python"},
    {".js", "application/javascript"},
    {".cpp", "text/x-c++"},
    {".c", "text/x-c"},
    {".h", "text/x-c"},

    // Documents
    {".pdf", "application/pdf"},

    // Models and other
    {".pt", "application/octet-stream"},
    {".pth", "application/octet-stream"},
    {".onnx", "application/octet-stream"},
};

FileType FileUtils::detect_file_type(const std::string& filename) {
    // Get extension (lowercase)
    std::string ext;
    size_t dot_pos = filename.rfind('.');
    if (dot_pos != std::string::npos) {
        ext = filename.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // Look up extension
    auto it = extension_map_.find(ext);
    if (it != extension_map_.end()) {
        return it->second;
    }

    return FileType::OTHER;
}

std::string FileUtils::file_type_to_string(FileType type) {
    auto it = type_name_map_.find(type);
    if (it != type_name_map_.end()) {
        return it->second;
    }
    return "other";
}

std::string FileUtils::get_mime_type(const std::string& filename) {
    // Get extension (lowercase)
    std::string ext;
    size_t dot_pos = filename.rfind('.');
    if (dot_pos != std::string::npos) {
        ext = filename.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // Look up MIME type
    auto it = mime_type_map_.find(ext);
    if (it != mime_type_map_.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

std::string FileUtils::format_file_size(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << size << " " << units[unit_index];
    return oss.str();
}

bool FileUtils::matches_pattern(const std::string& path, const std::string& pattern) {
    // Simple glob pattern matching
    // Supports: *.ext, prefix*, *suffix, dir/*.ext

    if (pattern == "*") {
        return true;  // Match all
    }

    // Check if pattern has wildcard
    size_t star_pos = pattern.find('*');
    if (star_pos == std::string::npos) {
        // No wildcard - exact match
        return path == pattern;
    }

    // Pattern: *.ext
    if (star_pos == 0 && pattern.find('*', 1) == std::string::npos) {
        std::string suffix = pattern.substr(1);
        return path.size() >= suffix.size() &&
               path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    // Pattern: prefix*
    if (star_pos == pattern.size() - 1) {
        std::string prefix = pattern.substr(0, star_pos);
        return path.size() >= prefix.size() &&
               path.compare(0, prefix.size(), prefix) == 0;
    }

    // Pattern: *suffix
    if (star_pos == 0) {
        std::string suffix = pattern.substr(1);
        return path.size() >= suffix.size() &&
               path.find(suffix) != std::string::npos;
    }

    // More complex patterns - simple substring match
    std::string before_star = pattern.substr(0, star_pos);
    std::string after_star = pattern.substr(star_pos + 1);

    return path.find(before_star) == 0 &&
           path.rfind(after_star) == path.size() - after_star.size();
}

// Hash utilities implementation

std::string FileUtils::bytes_to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string FileUtils::sha256_string(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);
    return bytes_to_hex(hash, SHA256_DIGEST_LENGTH);
}

std::string FileUtils::sha256_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";  // Return empty string on error
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        SHA256_Update(&ctx, buffer, file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    return bytes_to_hex(hash, SHA256_DIGEST_LENGTH);
}

FileMetadata FileUtils::get_file_metadata(const std::string& filepath) {
    FileMetadata metadata;
    metadata.path = filepath;

    namespace fs = std::filesystem;

    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        metadata.size_bytes = 0;
        metadata.sha256_hash = "";
        metadata.type = FileType::OTHER;
        return metadata;
    }

    metadata.size_bytes = fs::file_size(filepath);
    metadata.sha256_hash = sha256_file(filepath);
    metadata.type = detect_file_type(filepath);

    return metadata;
}

std::map<std::string, FileMetadata> FileUtils::hash_directory(
    const std::string& dirpath,
    const std::vector<std::string>& patterns
) {
    std::map<std::string, FileMetadata> result;
    namespace fs = std::filesystem;

    if (!fs::exists(dirpath) || !fs::is_directory(dirpath)) {
        return result;
    }

    // If no patterns specified, match all files
    bool match_all = patterns.empty();

    for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string filepath = entry.path().string();

        // Get relative path from dirpath
        std::string relpath = filepath.substr(dirpath.length());
        if (!relpath.empty() && relpath[0] == '/') {
            relpath = relpath.substr(1);
        }

        // Check if file matches any pattern
        bool matches = match_all;
        if (!match_all) {
            for (const auto& pattern : patterns) {
                if (matches_pattern(relpath, pattern)) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            FileMetadata metadata = get_file_metadata(filepath);
            metadata.path = relpath;  // Store relative path
            result[relpath] = metadata;
        }
    }

    return result;
}

} // namespace sandrun
