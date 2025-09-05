#!/bin/bash

# Script to fix and verify test issues

set -e

echo "================================"
echo "   Fixing Sandrun Tests"
echo "================================"

cd /home/spinoza/github/repos/sandrun

# First, let's check which tests are actually failing
echo -e "\n1. Running tests to identify failures..."
./build/tests/unit_tests --gtest_brief=1 2>&1 | grep "FAILED" | head -10 || true

# Set capabilities on the test binary for sandbox tests
echo -e "\n2. Setting capabilities for sandbox tests..."
sudo setcap cap_sys_admin+ep ./build/tests/unit_tests 2>/dev/null || {
    echo "Could not set capabilities. Will need to run with sudo."
}

# Run all tests with capabilities
echo -e "\n3. Running all tests with proper permissions..."
./build/tests/unit_tests --gtest_color=yes 2>&1 | tail -5

# If that fails, try with sudo
if [ $? -ne 0 ]; then
    echo -e "\n4. Retrying with sudo..."
    sudo ./build/tests/unit_tests --gtest_brief=1 2>&1 | tail -5
fi

echo -e "\n================================"
echo "   Test Results Summary"
echo "================================"

# Count passing vs failing
./build/tests/unit_tests --gtest_list_tests 2>/dev/null | grep -c "  " | xargs echo "Total tests:"