# Building Sandrun

Guide for building and developing Sandrun.

## Prerequisites

### System Requirements

- **OS**: Linux (Ubuntu 20.04+, Debian 11+)
- **Kernel**: 4.6+ (for namespace support)
- **RAM**: 2GB minimum
- **Disk**: 500MB for build artifacts

### Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  libseccomp-dev \
  libcap-dev \
  libssl-dev \
  pkg-config \
  git

# Optional: for testing
sudo apt-get install -y \
  libgtest-dev \
  lcov \
  python3-pip

# Optional: for documentation
pip3 install mkdocs-material
```

## Building

### Standard Build

```bash
# Configure
cmake -B build

# Build
cmake --build build

# Output: build/sandrun
```

### Debug Build

```bash
# Configure with debug symbols
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run with GDB
sudo gdb ./build/sandrun
```

### Release Build

```bash
# Configure with optimizations
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Output is optimized and stripped
```

### Build Options

```bash
# Enable all warnings
cmake -B build -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"

# Enable address sanitizer (debugging memory issues)
cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=address"

# Static analysis
cmake -B build -DCMAKE_CXX_CLANG_TIDY=clang-tidy
```

## Running Tests

### Unit Tests

```bash
# Build tests
cmake -B build
cmake --build build

# Run unit tests
./build/tests/unit_tests

# Run with verbose output
./build/tests/unit_tests --gtest_color=yes --gtest_output=xml:test_results.xml
```

### Integration Tests

```bash
# Integration tests may require sudo
sudo ./build/tests/integration_tests
```

### Test Coverage

```bash
# Build with coverage
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build-coverage

# Run tests
./build-coverage/tests/unit_tests

# Generate coverage report
lcov --directory build-coverage --capture --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report

# View report
firefox coverage_report/index.html
```

### Test Script

```bash
# Run all tests and generate coverage
./scripts/run_tests.sh
```

## Development Workflow

### Code Style

Follow Google C++ Style Guide:

- 2-space indentation
- Snake_case for variables
- PascalCase for classes
- UPPER_CASE for constants
- `#pragma once` for header guards

### Static Analysis

```bash
# clang-tidy
clang-tidy src/*.cpp -- -std=c++17

# cppcheck
cppcheck --enable=all src/
```

### Format Code

```bash
# clang-format
find src/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

## Project Structure

```
sandrun/
├── CMakeLists.txt           Build configuration
├── src/                     C++ source code
│   ├── main.cpp            Entry point
│   ├── sandbox.{h,cpp}     Sandbox implementation
│   ├── http_server.{h,cpp} HTTP server
│   ├── job_executor.{h,cpp} Job execution
│   ├── worker_identity.{h,cpp} Ed25519 signing
│   └── ...
├── tests/                   Test suite
│   ├── unit/               Unit tests
│   └── integration/        Integration tests
├── integrations/            Integrations
├── docs/                    Documentation
└── scripts/                 Helper scripts
```

## Debugging

### Common Issues

**Permission Denied:**

```bash
# Namespaces require root
sudo ./build/sandrun --port 8443
```

**Seccomp Errors:**

```bash
# Check kernel support
cat /proc/sys/kernel/seccomp
# Should output: 2

# Run without seccomp (debugging only)
# Edit src/sandbox.cpp to disable seccomp
```

**Memory Leaks:**

```bash
# Run with valgrind
sudo valgrind --leak-check=full ./build/sandrun --port 8443
```

### GDB Debugging

```bash
# Build with debug symbols
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with GDB
sudo gdb ./build/sandrun

# GDB commands:
(gdb) break main
(gdb) run --port 8443
(gdb) backtrace
(gdb) print variable_name
```

## Contributing

### Development Cycle

1. **Create Feature Branch**

```bash
git checkout -b feature/my-feature
```

2. **Write Tests First (TDD)**

```bash
# Add test to tests/unit/test_myfeature.cpp
# Run and verify it fails
./build/tests/unit_tests --gtest_filter=MyFeatureTest.*
```

3. **Implement Feature**

```bash
# Implement in src/
# Run tests again
./build/tests/unit_tests --gtest_filter=MyFeatureTest.*
```

4. **Run All Tests**

```bash
./scripts/run_tests.sh
```

5. **Commit**

```bash
git add -A
git commit -m "feat: Add my feature

- Implemented X
- Added tests
- Updated docs"
```

6. **Submit PR**

```bash
git push origin feature/my-feature
# Create pull request on GitHub
```

### Code Review Checklist

- ✅ Tests pass
- ✅ Code follows style guide
- ✅ No memory leaks (valgrind clean)
- ✅ Documentation updated
- ✅ No compiler warnings

## Release Process

### Version Bumping

1. Update version in `CMakeLists.txt`
2. Update `CHANGELOG.md`
3. Tag release:

```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

### Building Release

```bash
# Clean build
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Create tarball
tar czf sandrun-v1.0.0-linux-x86_64.tar.gz \
  build/sandrun \
  README.md \
  LICENSE
```

## CI/CD

### GitHub Actions

See `.github/workflows/` for CI configuration.

Tests run automatically on:
- Every push
- Every pull request
- Release tags

## Next Steps

- [Testing Guide](testing.md)
- [Architecture](../architecture.md)
- [Contributing Guidelines](https://github.com/yourusername/sandrun/CONTRIBUTING.md)
