#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace sandrun {

// Represents a single part in multipart form data
struct MultipartPart {
    std::map<std::string, std::string> headers;
    std::string name;
    std::string filename;
    std::vector<uint8_t> data;
};

// Simple multipart/form-data parser
class MultipartParser {
public:
    static std::vector<MultipartPart> parse(
        const std::string& content_type,
        const std::string& body
    );
    
private:
    static std::string extract_boundary(const std::string& content_type);
    static MultipartPart parse_part(const std::string& part_data);
};

} // namespace sandrun