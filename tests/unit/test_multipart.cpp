#include <gtest/gtest.h>
#include "multipart.h"
#include <sstream>

namespace sandrun {
namespace {

class MultipartParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser = std::make_unique<MultipartParser>();
    }

    std::unique_ptr<MultipartParser> parser;
    
    std::string createMultipartData(const std::string& boundary,
                                   const std::vector<std::pair<std::string, std::string>>& parts) {
        std::stringstream data;
        for (const auto& [name, content] : parts) {
            data << "--" << boundary << "\r\n";
            data << "Content-Disposition: form-data; name=\"" << name << "\"\r\n";
            data << "\r\n";
            data << content << "\r\n";
        }
        data << "--" << boundary << "--\r\n";
        return data.str();
    }
};

TEST_F(MultipartParserTest, ParseSimpleForm) {
    std::string boundary = "----WebKitFormBoundary123";
    std::vector<std::pair<std::string, std::string>> parts = {
        {"field1", "value1"},
        {"field2", "value2"},
        {"field3", "value3"}
    };
    
    std::string data = createMultipartData(boundary, parts);
    
    auto result = parser->parse(data, boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.fields.size(), 3);
    EXPECT_EQ(result.fields["field1"], "value1");
    EXPECT_EQ(result.fields["field2"], "value2");
    EXPECT_EQ(result.fields["field3"], "value3");
}

TEST_F(MultipartParserTest, ParseWithFiles) {
    std::string boundary = "----Boundary456";
    std::stringstream data;
    
    // Add a text field
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"description\"\r\n";
    data << "\r\n";
    data << "Test file upload\r\n";
    
    // Add a file
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"file\"; filename=\"test.txt\"\r\n";
    data << "Content-Type: text/plain\r\n";
    data << "\r\n";
    data << "File contents here\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.fields["description"], "Test file upload");
    EXPECT_EQ(result.files.size(), 1);
    EXPECT_EQ(result.files[0].field_name, "file");
    EXPECT_EQ(result.files[0].filename, "test.txt");
    EXPECT_EQ(result.files[0].content_type, "text/plain");
    EXPECT_EQ(result.files[0].data, "File contents here");
}

TEST_F(MultipartParserTest, ParseMultipleFiles) {
    std::string boundary = "----BoundaryXYZ";
    std::stringstream data;
    
    // First file
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"file1\"; filename=\"script.py\"\r\n";
    data << "Content-Type: text/x-python\r\n";
    data << "\r\n";
    data << "print('Hello')\r\n";
    
    // Second file
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"file2\"; filename=\"data.json\"\r\n";
    data << "Content-Type: application/json\r\n";
    data << "\r\n";
    data << "{\"key\": \"value\"}\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.files.size(), 2);
    
    EXPECT_EQ(result.files[0].filename, "script.py");
    EXPECT_EQ(result.files[0].content_type, "text/x-python");
    EXPECT_TRUE(result.files[0].data.find("print('Hello')") != std::string::npos);
    
    EXPECT_EQ(result.files[1].filename, "data.json");
    EXPECT_EQ(result.files[1].content_type, "application/json");
}

TEST_F(MultipartParserTest, ParseBinaryFile) {
    std::string boundary = "----Binary123";
    std::stringstream data;
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"binary\"; filename=\"data.bin\"\r\n";
    data << "Content-Type: application/octet-stream\r\n";
    data << "\r\n";
    
    // Binary data
    std::string binary_content;
    for (int i = 0; i < 256; i++) {
        binary_content.push_back(static_cast<char>(i));
    }
    data << binary_content << "\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.files.size(), 1);
    EXPECT_EQ(result.files[0].data.size(), 256);
    
    // Verify binary data integrity
    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(static_cast<unsigned char>(result.files[0].data[i]), i);
    }
}

TEST_F(MultipartParserTest, ParseManifest) {
    std::string boundary = "----Manifest789";
    std::stringstream data;
    
    // Manifest JSON
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"manifest\"\r\n";
    data << "\r\n";
    data << R"({
        "entrypoint": "main.py",
        "interpreter": "python3",
        "timeout": 300,
        "memory_mb": 512,
        "gpu": {
            "required": true,
            "min_vram_gb": 8
        }
    })" << "\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.fields["manifest"].find("\"entrypoint\": \"main.py\"") != std::string::npos);
    EXPECT_TRUE(result.fields["manifest"].find("\"gpu\"") != std::string::npos);
}

TEST_F(MultipartParserTest, InvalidBoundary) {
    std::string data = "Some random data without proper boundary";
    
    auto result = parser->parse(data, "----NonexistentBoundary");
    
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(MultipartParserTest, MissingFinalBoundary) {
    std::string boundary = "----Incomplete";
    std::stringstream data;
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"field\"\r\n";
    data << "\r\n";
    data << "value\r\n";
    // Missing final boundary
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_FALSE(result.success);
}

TEST_F(MultipartParserTest, EmptyParts) {
    std::string boundary = "----Empty";
    std::stringstream data;
    
    // Empty field
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"empty\"\r\n";
    data << "\r\n";
    data << "\r\n"; // Empty content
    
    data << "--" << boundary << "--\r\n";
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.fields["empty"], "");
}

TEST_F(MultipartParserTest, LargeFile) {
    std::string boundary = "----Large";
    std::stringstream data;
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"largefile\"; filename=\"big.txt\"\r\n";
    data << "\r\n";
    
    // Generate 1MB of data
    std::string large_content(1024 * 1024, 'X');
    data << large_content << "\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.files[0].data.size(), 1024 * 1024);
}

TEST_F(MultipartParserTest, SpecialCharactersInFieldName) {
    std::string boundary = "----Special";
    std::stringstream data;
    
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"field-with_special.chars[0]\"\r\n";
    data << "\r\n";
    data << "value\r\n";
    
    data << "--" << boundary << "--\r\n";
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.fields["field-with_special.chars[0]"], "value");
}

TEST_F(MultipartParserTest, MixedContent) {
    std::string boundary = "----Mixed";
    std::stringstream data;
    
    // Text field
    data << "--" << boundary << "\r\n";
    data << "Content-Disposition: form-data; name=\"name\"\r\n";
    data << "\r\n";
    data << "John Doe\r\n";
    
    // File
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
    
    auto result = parser->parse(data.str(), boundary);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.fields.size(), 2);
    EXPECT_EQ(result.files.size(), 1);
    EXPECT_EQ(result.fields["name"], "John Doe");
    EXPECT_EQ(result.fields["age"], "25");
    EXPECT_EQ(result.files[0].filename, "pic.jpg");
}

} // namespace
} // namespace sandrun