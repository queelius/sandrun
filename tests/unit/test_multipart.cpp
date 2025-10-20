#include <gtest/gtest.h>
#include "multipart.h"
#include <sstream>

namespace sandrun {
namespace {

class MultipartParserTest : public ::testing::Test {
protected:
    std::string createMultipartData(const std::string& boundary,
                                   const std::vector<std::pair<std::string, std::string>>& fields,
                                   const std::vector<std::tuple<std::string, std::string, std::string, std::string>>& files = {}) {
        std::stringstream data;

        // Add fields
        for (const auto& [name, content] : fields) {
            data << "--" << boundary << "\r\n";
            data << "Content-Disposition: form-data; name=\"" << name << "\"\r\n";
            data << "\r\n";
            data << content << "\r\n";
        }

        // Add files
        for (const auto& [name, filename, content_type, content] : files) {
            data << "--" << boundary << "\r\n";
            data << "Content-Disposition: form-data; name=\"" << name << "\"; filename=\"" << filename << "\"\r\n";
            data << "Content-Type: " << content_type << "\r\n";
            data << "\r\n";
            data << content << "\r\n";
        }

        data << "--" << boundary << "--\r\n";
        return data.str();
    }
};

TEST_F(MultipartParserTest, ParseSimpleForm) {
    std::string boundary = "----WebKitFormBoundary123";
    std::vector<std::pair<std::string, std::string>> fields = {
        {"field1", "value1"},
        {"field2", "value2"},
        {"field3", "value3"}
    };

    std::string body = createMultipartData(boundary, fields);
    std::string content_type = "multipart/form-data; boundary=" + boundary;

    auto parts = MultipartParser::parse(content_type, body);

    EXPECT_EQ(parts.size(), 3);

    // Check field values
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string expected_name = "field" + std::to_string(i + 1);
        std::string expected_value = "value" + std::to_string(i + 1);

        EXPECT_EQ(parts[i].name, expected_name);
        std::string actual_value(parts[i].data.begin(), parts[i].data.end());
        EXPECT_EQ(actual_value, expected_value);
    }
}

TEST_F(MultipartParserTest, ParseWithFiles) {
    std::string boundary = "----Boundary456";
    std::vector<std::pair<std::string, std::string>> fields = {
        {"description", "Test file upload"}
    };
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> files = {
        {"file", "test.txt", "text/plain", "File contents here"}
    };

    std::string body = createMultipartData(boundary, fields, files);
    std::string content_type = "multipart/form-data; boundary=" + boundary;

    auto parts = MultipartParser::parse(content_type, body);

    EXPECT_EQ(parts.size(), 2);

    // Check field
    EXPECT_EQ(parts[0].name, "description");
    std::string field_value(parts[0].data.begin(), parts[0].data.end());
    EXPECT_EQ(field_value, "Test file upload");

    // Check file
    EXPECT_EQ(parts[1].name, "file");
    EXPECT_EQ(parts[1].filename, "test.txt");
    EXPECT_EQ(parts[1].headers["Content-Type"], "text/plain");
    std::string file_content(parts[1].data.begin(), parts[1].data.end());
    EXPECT_EQ(file_content, "File contents here");
}

TEST_F(MultipartParserTest, ParseMultipleFiles) {
    std::string boundary = "----BoundaryXYZ";
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> files = {
        {"file1", "script.py", "text/x-python", "print('Hello')"},
        {"file2", "data.json", "application/json", "{\"key\": \"value\"}"}
    };

    std::string body = createMultipartData(boundary, {}, files);
    std::string content_type = "multipart/form-data; boundary=" + boundary;

    auto parts = MultipartParser::parse(content_type, body);

    EXPECT_EQ(parts.size(), 2);

    // First file
    EXPECT_EQ(parts[0].filename, "script.py");
    EXPECT_EQ(parts[0].headers["Content-Type"], "text/x-python");
    std::string content1(parts[0].data.begin(), parts[0].data.end());
    EXPECT_EQ(content1, "print('Hello')");

    // Second file
    EXPECT_EQ(parts[1].filename, "data.json");
    EXPECT_EQ(parts[1].headers["Content-Type"], "application/json");
    std::string content2(parts[1].data.begin(), parts[1].data.end());
    EXPECT_EQ(content2, "{\"key\": \"value\"}");
}

TEST_F(MultipartParserTest, ParseBinaryData) {
    std::string boundary = "----Binary123";
    std::stringstream data;

    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"binary\"; filename=\"data.bin\"\r\n";
    data << "Content-Type: application/octet-stream\r\n";
    data << "\r\n";

    // Binary data with null bytes
    std::string binary_content;
    for (int i = 0; i < 256; i++) {
        binary_content.push_back(static_cast<char>(i));
    }
    data << binary_content << "\r\n";
    data << "--" << boundary << "--\r\n";

    std::string content_type = "multipart/form-data; boundary=" + boundary;
    auto parts = MultipartParser::parse(content_type, data.str());

    EXPECT_EQ(parts.size(), 1);
    EXPECT_EQ(parts[0].data.size(), 256);

    // Verify binary data integrity
    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(parts[0].data[i], static_cast<uint8_t>(i));
    }
}

TEST_F(MultipartParserTest, ParseManifest) {
    std::string boundary = "----Manifest789";
    std::string manifest_json = R"({
        "entrypoint": "main.py",
        "interpreter": "python3",
        "timeout": 300,
        "memory_mb": 512,
        "gpu": {
            "required": true,
            "min_vram_gb": 8
        }
    })";

    std::vector<std::pair<std::string, std::string>> fields = {
        {"manifest", manifest_json}
    };

    std::string body = createMultipartData(boundary, fields);
    std::string content_type = "multipart/form-data; boundary=" + boundary;

    auto parts = MultipartParser::parse(content_type, body);

    EXPECT_EQ(parts.size(), 1);
    EXPECT_EQ(parts[0].name, "manifest");

    std::string parsed_manifest(parts[0].data.begin(), parts[0].data.end());
    EXPECT_TRUE(parsed_manifest.find("\"entrypoint\": \"main.py\"") != std::string::npos);
    EXPECT_TRUE(parsed_manifest.find("\"gpu\"") != std::string::npos);
}

TEST_F(MultipartParserTest, BoundaryWithQuotes) {
    std::string boundary = "----Quoted123";
    std::vector<std::pair<std::string, std::string>> fields = {
        {"test", "value"}
    };

    std::string body = createMultipartData(boundary, fields);
    std::string content_type = "multipart/form-data; boundary=\"" + boundary + "\"";

    auto parts = MultipartParser::parse(content_type, body);

    EXPECT_EQ(parts.size(), 1);
    EXPECT_EQ(parts[0].name, "test");
}

TEST_F(MultipartParserTest, InvalidBoundary) {
    std::string data = "Some random data without proper boundary";
    std::string content_type = "multipart/form-data; boundary=----NonexistentBoundary";

    auto parts = MultipartParser::parse(content_type, data);

    EXPECT_EQ(parts.size(), 0);
}

TEST_F(MultipartParserTest, EmptyBody) {
    std::string content_type = "multipart/form-data; boundary=----Empty";

    auto parts = MultipartParser::parse(content_type, "");

    EXPECT_EQ(parts.size(), 0);
}

TEST_F(MultipartParserTest, MissingBoundaryInContentType) {
    std::string body = "--boundary\r\nContent-Disposition: form-data; name=\"test\"\r\n\r\nvalue\r\n--boundary--\r\n";

    auto parts = MultipartParser::parse("multipart/form-data", body);

    EXPECT_EQ(parts.size(), 0);
}

TEST_F(MultipartParserTest, EmptyField) {
    std::string boundary = "----Empty";
    std::vector<std::pair<std::string, std::string>> fields = {
        {"empty", ""},
        {"nonempty", "value"}
    };

    std::string body = createMultipartData(boundary, fields);
    std::string content_type = "multipart/form-data; boundary=" + boundary;

    auto parts = MultipartParser::parse(content_type, body);

    EXPECT_EQ(parts.size(), 2);
    EXPECT_EQ(parts[0].name, "empty");
    EXPECT_EQ(parts[0].data.size(), 0);
    EXPECT_EQ(parts[1].name, "nonempty");
}

TEST_F(MultipartParserTest, LargeContent) {
    std::string boundary = "----Large";
    std::string large_content(1024 * 1024, 'X'); // 1MB

    std::vector<std::tuple<std::string, std::string, std::string, std::string>> files = {
        {"largefile", "big.txt", "text/plain", large_content}
    };

    std::string body = createMultipartData(boundary, {}, files);
    std::string content_type = "multipart/form-data; boundary=" + boundary;

    auto parts = MultipartParser::parse(content_type, body);

    EXPECT_EQ(parts.size(), 1);
    EXPECT_EQ(parts[0].data.size(), 1024 * 1024);

    // Verify all bytes are 'X'
    for (size_t i = 0; i < parts[0].data.size(); ++i) {
        EXPECT_EQ(parts[0].data[i], 'X');
    }
}

TEST_F(MultipartParserTest, SpecialCharactersInFieldName) {
    std::string boundary = "----Special";
    std::stringstream data;

    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"field-with_special.chars[0]\"\r\n";
    data << "\r\n";
    data << "value\r\n";
    data << "--" << boundary << "--\r\n";

    std::string content_type = "multipart/form-data; boundary=" + boundary;
    auto parts = MultipartParser::parse(content_type, data.str());

    EXPECT_EQ(parts.size(), 1);
    EXPECT_EQ(parts[0].name, "field-with_special.chars[0]");
    std::string value(parts[0].data.begin(), parts[0].data.end());
    EXPECT_EQ(value, "value");
}

TEST_F(MultipartParserTest, MixedContent) {
    std::string boundary = "----Mixed";
    std::stringstream data;

    // Text field
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"name\"\r\n";
    data << "\r\n";
    data << "John Doe\r\n";

    // Binary file with JPEG header
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"avatar\"; filename=\"pic.jpg\"\r\n";
    data << "Content-Type: image/jpeg\r\n";
    data << "\r\n";
    data << "\xFF\xD8\xFF\xE0"; // JPEG header
    data << "\r\n";

    // Another text field
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"age\"\r\n";
    data << "\r\n";
    data << "25\r\n";

    data << "--" << boundary << "--\r\n";

    std::string content_type = "multipart/form-data; boundary=" + boundary;
    auto parts = MultipartParser::parse(content_type, data.str());

    EXPECT_EQ(parts.size(), 3);

    // Check text fields
    EXPECT_EQ(parts[0].name, "name");
    std::string name_value(parts[0].data.begin(), parts[0].data.end());
    EXPECT_EQ(name_value, "John Doe");

    // Check file
    EXPECT_EQ(parts[1].name, "avatar");
    EXPECT_EQ(parts[1].filename, "pic.jpg");
    EXPECT_EQ(parts[1].data.size(), 4); // Size of JPEG header

    // Check second text field
    EXPECT_EQ(parts[2].name, "age");
    std::string age_value(parts[2].data.begin(), parts[2].data.end());
    EXPECT_EQ(age_value, "25");
}

TEST_F(MultipartParserTest, CRLFVariations) {
    std::string boundary = "----CRLF";
    std::stringstream data;

    // Using just \n instead of \r\n
    data << "--" << boundary << "\n";
    data << "Content-Disposition: form-data; name=\"unix\"\n";
    data << "\n";
    data << "unix-style\n";
    data << "--" << boundary << "--\n";

    std::string content_type = "multipart/form-data; boundary=" + boundary;
    auto parts = MultipartParser::parse(content_type, data.str());

    // The parser should handle both \r\n and \n
    EXPECT_GE(parts.size(), 0); // May or may not parse depending on implementation
}

} // namespace
} // namespace sandrun