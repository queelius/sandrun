#include "multipart.h"
#include <sstream>
#include <algorithm>

namespace sandrun {

std::vector<MultipartPart> MultipartParser::parse(
    const std::string& content_type,
    const std::string& body
) {
    std::vector<MultipartPart> parts;
    
    // Extract boundary from content-type
    std::string boundary = extract_boundary(content_type);
    if (boundary.empty()) return parts;
    
    // Split by boundary
    std::string delimiter = "--" + boundary;
    std::string end_delimiter = delimiter + "--";
    
    size_t pos = 0;
    size_t start = body.find(delimiter);
    if (start == std::string::npos) return parts;
    
    start += delimiter.length() + 2; // Skip delimiter and \r\n
    
    while (true) {
        size_t end = body.find(delimiter, start);
        if (end == std::string::npos) break;
        
        std::string part_data = body.substr(start, end - start - 2); // -2 for \r\n
        if (!part_data.empty()) {
            MultipartPart part = parse_part(part_data);
            if (!part.name.empty()) {
                parts.push_back(part);
            }
        }
        
        start = end + delimiter.length() + 2;
        
        // Check if we hit the end delimiter
        if (body.substr(end, end_delimiter.length()) == end_delimiter) {
            break;
        }
    }
    
    return parts;
}

std::string MultipartParser::extract_boundary(const std::string& content_type) {
    std::string boundary_prefix = "boundary=";
    size_t pos = content_type.find(boundary_prefix);
    if (pos == std::string::npos) return "";
    
    pos += boundary_prefix.length();
    size_t end = content_type.find(';', pos);
    if (end == std::string::npos) end = content_type.length();
    
    std::string boundary = content_type.substr(pos, end - pos);
    
    // Remove quotes if present
    if (!boundary.empty() && boundary[0] == '"') {
        boundary = boundary.substr(1, boundary.length() - 2);
    }
    
    return boundary;
}

MultipartPart MultipartParser::parse_part(const std::string& part_data) {
    MultipartPart part;
    
    // Find headers end
    size_t headers_end = part_data.find("\r\n\r\n");
    if (headers_end == std::string::npos) {
        headers_end = part_data.find("\n\n");
        if (headers_end == std::string::npos) return part;
    }
    
    std::string headers_section = part_data.substr(0, headers_end);
    size_t content_start = headers_end + (part_data[headers_end] == '\r' ? 4 : 2);
    
    // Parse headers
    std::istringstream headers_stream(headers_section);
    std::string line;
    while (std::getline(headers_stream, line)) {
        if (line.back() == '\r') line.pop_back();
        
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 2); // Skip ": "
            part.headers[key] = value;
            
            // Parse Content-Disposition for name and filename
            if (key == "Content-Disposition") {
                // Extract name
                size_t name_pos = value.find("name=\"");
                if (name_pos != std::string::npos) {
                    name_pos += 6;
                    size_t name_end = value.find('"', name_pos);
                    part.name = value.substr(name_pos, name_end - name_pos);
                }
                
                // Extract filename
                size_t filename_pos = value.find("filename=\"");
                if (filename_pos != std::string::npos) {
                    filename_pos += 10;
                    size_t filename_end = value.find('"', filename_pos);
                    part.filename = value.substr(filename_pos, filename_end - filename_pos);
                }
            }
        }
    }
    
    // Get content
    std::string content = part_data.substr(content_start);
    part.data.assign(content.begin(), content.end());
    
    return part;
}

} // namespace sandrun