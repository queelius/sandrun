# Test Coverage Report

Generated: 2025-10-20

## Overall Coverage

| Metric | Coverage | Lines |
|--------|----------|-------|
| **Lines** | **30.6%** | 627 / 2049 |
| **Functions** | **44.2%** | 57 / 129 |
| **Branches** | **13.7%** | 554 / 4049 |

## Coverage by File

### High Coverage (>70%)

| File | Line Coverage | Status |
|------|---------------|--------|
| `multipart.cpp` | 100% (69/69) | ✅ Excellent |
| `proof.cpp` | 92% (121/131) | ✅ Excellent |
| `rate_limiter.cpp` | 81% (84/103) | ✅ Good |
| `worker_identity.cpp` | 76% (107/140) | ✅ Good |
| `file_utils.cpp` | 71% (88/123) | ✅ Good |

### Medium Coverage (40-70%)

| File | Line Coverage | Status |
|------|---------------|--------|
| `sandbox.cpp` | 50% (110/219) | ⚠️ Needs Improvement |
| `job_executor.cpp` | 43% (35/81) | ⚠️ Needs Improvement |

### Low Coverage (<40%)

| File | Line Coverage | Status |
|------|---------------|--------|
| `job_hash.cpp` | 0% (0/10) | ❌ Critical - New File |
| `environment_manager.cpp` | 0% (0/204) | ❌ Critical |
| `http_server.cpp` | 0% (0/172) | ❌ Critical |
| `main.cpp` | 0% (0/618) | ❌ Expected (entry point) |
| `websocket.cpp` | 0% (0/154) | ❌ Critical |

## Analysis

### Strengths

1. **Core Utilities Well-Tested**
   - Multipart parsing: 100%
   - Proof generation: 92%
   - Rate limiting: 81%
   - Worker identity: 76%
   - File utilities: 71%

2. **Test Quality**
   - 130 unit tests passing
   - 43 integration tests passing
   - Good behavioral test coverage (after Phase 1 improvements)

### Critical Gaps

1. **`job_hash.cpp` - 0% coverage** ⚠️ **URGENT**
   - NEW file added in Phase 1
   - Contains JobDefinition::calculate_hash()
   - Used by tests but not covered itself
   - **Action**: Add unit tests for JobDefinition

2. **`environment_manager.cpp` - 0% coverage**
   - 204 lines of untested code
   - Critical for Phase 3 (Persistent Environments)
   - **Action**: Add comprehensive tests before continuing Phase 3

3. **`http_server.cpp` - 0% coverage**
   - 172 lines of HTTP handling logic
   - **Action**: Add integration tests for HTTP endpoints

4. **`websocket.cpp` - 0% coverage**
   - 154 lines of WebSocket streaming
   - **Action**: Add WebSocket connection tests

### Medium Priority

1. **`sandbox.cpp` - 50% coverage**
   - Core isolation logic partially tested
   - Missing: namespace setup error paths, cleanup edge cases
   - **Action**: Add tests for failure scenarios

2. **`job_executor.cpp` - 43% coverage**
   - Job execution logic partially tested
   - Missing: timeout handling, resource limit enforcement
   - **Action**: Add tests for edge cases and error paths

## Recommendations

### Immediate Actions (Before Next Commit)

1. **Add tests for `job_hash.cpp`**
   - File was created in Phase 1 but has no coverage
   - Should be quick - only 10 lines

2. **Add tests for `environment_manager.cpp`**
   - Required before continuing Phase 3
   - Test template loading, caching, cleanup

### Short Term (This Week)

3. **Add HTTP endpoint tests**
   - Test `/submit`, `/status`, `/logs`, `/health`
   - Can use mock sandrun instance

4. **Add WebSocket tests**
   - Test connection, streaming, disconnection
   - Mock WebSocket frames

### Long Term (Ongoing)

5. **Improve branch coverage (13.7% → 50%+)**
   - Add tests for error paths
   - Test edge cases and boundary conditions

6. **Target Coverage Goals**
   - Critical components: 90%+ (crypto, verification, sandbox)
   - Core features: 80%+ (job execution, HTTP server)
   - Overall: 70%+

## Test Coverage Target

Based on the TEST_FIXES_ACTION_PLAN.md:

> **Coverage Targets:**
> - Critical paths: 100% (crypto, verification, sandbox)
> - Core features: 90%+ (job execution, HTTP server)
> - Overall: 80%+

**Current Status vs Goals:**

| Component | Current | Target | Gap |
|-----------|---------|--------|-----|
| Crypto/Verification | 76-92% | 100% | -8% to -24% |
| Job Execution | 43% | 90% | -47% |
| HTTP Server | 0% | 90% | -90% |
| **Overall** | **30.6%** | **80%** | **-49.4%** |

## Next Steps

1. Run: `gcovr --root . --filter 'src/.*' --html --html-details -o coverage-report.html build-coverage`
2. Open: `firefox build-coverage/coverage-report.html`
3. Identify uncovered lines in critical files
4. Add missing tests

## How to Generate This Report

```bash
# Build with coverage
cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build-coverage

# Run tests
./build-coverage/tests/unit_tests

# Generate report
gcovr --root . --filter 'src/.*' --exclude 'tests/.*' --print-summary build-coverage

# Generate HTML
gcovr --root . --filter 'src/.*' --html --html-details -o coverage-report.html build-coverage
```

## See Also

- [TEST_FIXES_ACTION_PLAN.md](TEST_FIXES_ACTION_PLAN.md) - Test improvement plan
- [Testing Guide](docs/development/testing.md) - Testing documentation
- [HTML Coverage Report](build-coverage/coverage-report.html) - Detailed line-by-line coverage
