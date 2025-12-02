# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

Sandrun is an anonymous batch job execution system with two components:
- **C++ Backend**: Secure sandboxed execution with Linux namespaces, seccomp-BPF, and cgroups
- **Integrations**: Web frontend, Python client, trusted pool coordinator, MCP server

## Development Commands

### Building

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y build-essential cmake libseccomp-dev libcap-dev libssl-dev

# Standard build
cmake -B build && cmake --build build

# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# Run server (requires root for namespaces)
sudo ./build/sandrun --port 8443
```

### Testing

```bash
# Run all unit tests
./build/tests/unit_tests

# Run specific test suite
./build/tests/unit_tests --gtest_filter=SandboxTest.*

# Run single test
./build/tests/unit_tests --gtest_filter=FileUtilsTest.SHA256File

# Integration tests (may require sudo)
sudo ./build/tests/integration_tests

# Trusted pool integration tests
cd integrations/trusted-pool && ./test_pool.sh
```

### Test Coverage

```bash
# Build with coverage
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build-coverage

# Run tests
./build-coverage/tests/unit_tests

# Generate report (use gcovr for clean output)
gcovr --root . --filter 'src/.*' --gcov-ignore-parse-errors=negative_hits.warn_once_per_file ./build-coverage
```

## Architecture

### Core Data Flow

```
Client → HTTP Server → Job Queue → Sandbox (namespaces + seccomp) → Output
                         ↓
                    Rate Limiter (IP-based CPU quotas)
```

### Key Source Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry point, HTTP routing, job orchestration |
| `src/sandbox.cpp` | Linux namespace/seccomp isolation (security-critical) |
| `src/http_server.cpp` | Minimal HTTP/1.1 server with multipart parsing |
| `src/websocket.cpp` | WebSocket for log streaming, OutputBroadcaster |
| `src/rate_limiter.cpp` | IP-based CPU quota enforcement |
| `src/worker_identity.cpp` | Ed25519 key generation and job signing |
| `src/job_executor.cpp` | Process spawning and output capture |
| `src/file_utils.cpp` | File operations, MIME types, SHA-256, pattern matching |
| `src/multipart.cpp` | HTTP multipart form-data parsing |
| `src/job_hash.cpp` | Deterministic job hashing (JobDefinition) |
| `src/environment_manager.cpp` | Python environment caching and templates |
| `src/proof.cpp` | Proof-of-compute generation and verification |
| `src/constants.h` | All resource limits and defaults |

### Security Model

Jobs execute in isolated namespaces with:
- **PID namespace**: Can't see other processes
- **Network namespace**: No network access
- **Mount namespace**: tmpfs only (RAM)
- **Seccomp-BPF**: Whitelist of ~60 safe syscalls
- **Cgroup limits**: CPU seconds, memory, process count

### Integrations

| Integration | Location | Purpose |
|-------------|----------|---------|
| Trusted Pool | `integrations/trusted-pool/` | Distribute jobs across multiple workers |
| Web Frontend | `integrations/web-frontend/` | Browser-based job submission |
| Python Client | `integrations/python-client/` | Programmatic job submission |
| MCP Server | `integrations/mcp-server/` | Claude Desktop integration |

## Testing Guidelines

- Test files: `tests/unit/test_*.cpp` and `tests/integration/test_*.cpp`
- Framework: Google Test (gtest_main)
- Pattern: Given/When/Then comments with behavioral assertions
- Coverage targets: Critical paths 100%, core features 90%+

### Running Trusted Pool Tests

```bash
cd integrations/trusted-pool
pip install -r requirements.txt
pytest tests/ -v                    # Unit tests
./test_pool.sh                      # Integration tests (starts workers)
```

## Code Style

- C++17 standard
- 2-space indentation
- snake_case for variables, PascalCase for classes
- `#pragma once` for header guards
- All code in `sandrun` namespace

## Coverage Notes

### Fork() Limitation
Code executed in child processes after `fork()` cannot be tracked by coverage tools running in the parent process. This affects:
- `src/sandbox.cpp` - Child process setup code inside namespaces
- `src/job_executor.cpp` - Process spawning and execution code

To improve coverage in these areas, test the helper functions that don't require fork() rather than the full sandbox execution paths.

### Slow Coverage Builds
Coverage-instrumented builds run significantly slower. For large data tests (e.g., testing with 1MB+ data), use smaller data sizes (64KB-100KB) to avoid timeout issues during CI.

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/` | Server info and status |
| `POST` | `/submit` | Submit new job (multipart form-data) |
| `GET` | `/status/{job_id}` | Get job status |
| `GET` | `/logs/{job_id}` | Get stdout/stderr |
| `GET` | `/outputs/{job_id}` | List output files |
| `GET` | `/download/{job_id}/{file}` | Download output file |
| `WS` | `/ws/{job_id}` | WebSocket for real-time log streaming |
