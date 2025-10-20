#!/bin/bash

# Sandrun Test Runner Script

set -e

echo "============================="
echo "   Sandrun Test Suite"
echo "============================="

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if build directory exists
if [ ! -d "build" ]; then
    echo -e "${YELLOW}Build directory not found. Creating and configuring...${NC}"
    cmake -B build -DCMAKE_BUILD_TYPE=Debug
fi

# Build the project and tests
echo -e "\n${YELLOW}Building sandrun and tests...${NC}"
cmake --build build

# Check if tests were built
if [ ! -f "build/tests/unit_tests" ]; then
    echo -e "${RED}Unit tests not found! Ensure GoogleTest is available.${NC}"
    echo "To install GoogleTest on Ubuntu/Debian: sudo apt-get install libgtest-dev"
    exit 1
fi

# Run unit tests
echo -e "\n${YELLOW}Running unit tests...${NC}"
if ./build/tests/unit_tests --gtest_color=yes; then
    echo -e "${GREEN}✓ Unit tests passed${NC}"
else
    echo -e "${RED}✗ Unit tests failed${NC}"
    exit 1
fi

# Run integration tests (may require sudo for sandbox)
echo -e "\n${YELLOW}Running integration tests...${NC}"
if [ "$EUID" -ne 0 ]; then 
    echo "Note: Some integration tests may require sudo for sandbox features"
    echo "Run with: sudo ./run_tests.sh"
fi

if [ -f "build/tests/integration_tests" ]; then
    if ./build/tests/integration_tests --gtest_color=yes; then
        echo -e "${GREEN}✓ Integration tests passed${NC}"
    else
        echo -e "${RED}✗ Integration tests failed${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}Integration tests not built (may need additional dependencies)${NC}"
fi

# Run coverage if lcov is available
if command -v lcov &> /dev/null; then
    echo -e "\n${YELLOW}Generating test coverage report...${NC}"
    
    # Clean previous coverage
    lcov --directory build --zerocounters
    
    # Run tests with coverage
    ./build/tests/unit_tests
    
    # Capture coverage
    lcov --directory build --capture --output-file coverage.info
    
    # Remove external files
    lcov --remove coverage.info '/usr/*' --output-file coverage.info
    lcov --remove coverage.info '*/tests/*' --output-file coverage.info
    
    # Generate HTML report
    genhtml coverage.info --output-directory coverage_report
    
    echo -e "${GREEN}Coverage report generated in coverage_report/index.html${NC}"
else
    echo -e "${YELLOW}Install lcov for test coverage reports: sudo apt-get install lcov${NC}"
fi

# Summary
echo -e "\n============================="
echo -e "${GREEN}   All tests completed!${NC}"
echo -e "============================="

# Test the running instance if available
if pgrep -f "sandrun --port" > /dev/null; then
    echo -e "\n${YELLOW}Testing running sandrun instance...${NC}"
    
    # Create test job
    cat > /tmp/test_job.py << EOF
print("Test job from test suite")
import sys
sys.exit(0)
EOF

    # Submit test job
    response=$(curl -s -X POST http://localhost:8081/submit \
        -F "files=@/tmp/test_job.py" \
        -F 'manifest={"entrypoint":"test_job.py","interpreter":"python3"}')
    
    if echo "$response" | grep -q "job_id"; then
        echo -e "${GREEN}✓ API test successful${NC}"
    else
        echo -e "${RED}✗ API test failed${NC}"
    fi
    
    rm /tmp/test_job.py
fi

echo -e "\nTo run specific tests:"
echo "  ./build/tests/unit_tests --gtest_filter=SandboxTest.*"
echo "  ./build/tests/unit_tests --gtest_filter=ProofTest.*"
echo "  ./build/tests/integration_tests --gtest_filter=GPUSupportTest.*"