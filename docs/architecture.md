# Sandrun: Anonymous Ephemeral Code Execution

## Motivation

### The Problem

Modern computing is increasingly locked down. Running arbitrary code requires:
- Creating accounts on cloud platforms (identity exposure)
- Installing complex development environments (system contamination)  
- Trusting third-party services with your code (privacy violation)
- Managing dependencies and credentials (operational burden)

Meanwhile, there's a legitimate need for ephemeral computation:
- Testing code snippets without contaminating your system
- Running untrusted scripts safely
- Sharing computational resources anonymously
- Quick data processing without infrastructure

### The Vision

Sandrun provides **anonymous, ephemeral, sandboxed code execution** with these principles:

1. **No Identity**: No accounts, no tracking, no persistence beyond job lifetime
2. **True Isolation**: Hardware-enforced sandboxing via Linux security primitives
3. **Privacy by Design**: Code and data exist only in memory, auto-destroyed after use
4. **Fair Access**: Rate limiting by IP ensures equitable resource sharing
5. **Simplicity**: Single binary, minimal dependencies, clear security model

## Theory

### Privacy Model

**Threat Model:**
- Server admin should learn minimal information about executed code
- Network observers should see only encrypted traffic
- Other users should have zero visibility into your jobs
- System compromise shouldn't leak historical job data

**Privacy Guarantees:**
- Jobs execute in isolated namespaces (PID, network, mount, IPC, UTS)
- Memory is never swapped to disk (mlocked pages)
- All job data stored in tmpfs (RAM only)
- Automatic destruction after job completion
- No logging of job contents, only resource metrics

### Security Architecture

**Defense in Depth:**
1. **Network Layer**: TLS only, no HTTP fallback
2. **Application Layer**: Minimal HTTP server, no dynamic content
3. **Execution Layer**: Separate sandboxed process per job
4. **System Layer**: Seccomp-BPF syscall filtering
5. **Hardware Layer**: CPU/memory quotas via cgroups

**Sandbox Constraints (Per Job):**
```
- New PID namespace (job can't see other processes)
- New network namespace (isolated network stack)
- New mount namespace (private filesystem view)
- Seccomp filter (whitelist of ~50 safe syscalls)
- No capability privileges (drops all capabilities)
- Read-only root filesystem bind mount
- tmpfs for /tmp and working directory
- CPU quota: 10 seconds per minute
- Memory limit: 512MB
- No network access (airgapped execution)
```

### Rate Limiting Theory

**Fair Queuing with Time-Based Quotas:**

Instead of traditional rate limiting (requests/second), we implement CPU-time fairness:
- Each IP gets 10 CPU-seconds per minute
- Maximum 2 concurrent jobs per IP
- Jobs queued when quota exhausted

This ensures:
- Long-running jobs don't block others
- Burst capacity for quick scripts
- Natural DOS protection
- Fair resource distribution

## Practice

### Implementation Stack

**Why C++:**
- Direct syscall access for sandboxing
- Predictable memory management (no GC pauses)
- Small binary size (~500KB vs 50MB+ for Python)
- Native seccomp-bpf and namespace support
- Compile-time security checks

**Minimal Dependencies:**
```
System Libraries Only:
- libseccomp: Syscall filtering
- libcap: Capability management  
- pthread: Threading
- Standard C++ library

No External Dependencies:
- HTTP parsing: Hand-rolled minimal parser
- JSON: Simple struct serialization
- Crypto: Use kernel's /dev/urandom
```

### API Design

**Batch Job Submission:**
```
POST /submit
  Content-Type: multipart/form-data
  Body: Files (tar.gz, zip, or individual files) + manifest
  Returns: {"job_id": "uuid", "status": "queued"}

GET /status/{job_id}
  Returns: {
    "status": "queued|running|completed|failed",
    "queue_position": 3,
    "metrics": {"cpu_seconds": 1.23, "memory_mb": 128}
  }

GET /logs/{job_id}
  Returns: {"stdout": "...", "stderr": "..."}

GET /outputs/{job_id}
  Returns: {"files": ["results/data.csv", "plot.png"]}

GET /download/{job_id}
  Returns: tar.gz of output files matching manifest patterns
  Auto-deletes after retrieval
```

**No Complexity:**
- No authentication/authorization
- No persistent storage
- No configuration
- Simple multipart parsing (minimal code)
- No external dependencies

### Operational Model

**Single Binary Deployment:**
```bash
# Build
cmake -B build && cmake --build build

# Run (needs CAP_SYS_ADMIN for namespaces)
sudo setcap cap_sys_admin+ep ./sandrun
./sandrun --port 8443 --cert cert.pem --key key.pem
```

**Auto-Cleanup:**
- Jobs older than 5 minutes: Killed
- Completed jobs: Deleted after 1 minute
- Failed jobs: Deleted immediately
- Memory usage > 80%: Oldest jobs evicted

### Performance Targets

**Design Goals:**
- Startup time: <10ms per job
- Overhead: <1% CPU for orchestration
- Memory: <10MB for service, 512MB per job
- Latency: <50ms to start execution
- Throughput: 100+ jobs/second on modern hardware

### Security Considerations

**What We Explicitly Don't Protect Against:**
- Timing attacks between jobs (shared CPU)
- Resource exhaustion within quotas
- Covert channels via cache timing
- Kernel vulnerabilities (requires kernel hardening)

**What We Do Protect Against:**
- Code injection/execution outside sandbox
- Filesystem access beyond tmpfs
- Network access from jobs
- Process visibility across jobs
- Memory disclosure between jobs
- Persistence of any job data

## Architecture: Frontend/Backend Separation

### Backend (C++ Sandbox Service)

**Responsibilities:**
- Secure code execution
- Resource management
- Rate limiting
- Minimal HTTP API

**Why C++:**
- Direct syscall control for sandboxing
- Predictable memory management
- Minimal attack surface
- No interpreter overhead

### Frontend (Separate Application)

**Responsibilities:**
- User interface
- Code editing
- Result visualization
- API interaction

**Why Separate:**
- Security isolation from sandbox
- Independent deployment/updates
- Technology freedom (Python/JS/etc)
- CDN hosting possible
- Multiple frontends can coexist

### Frontend Options

**1. Python + Streamlit (Recommended for beauty)**
```python
# frontend.py - Beautiful UI in minutes
import streamlit as st
import requests

st.title("🏃 Sandrun - Anonymous Code Execution")

code = st.text_area("Code", height=300)
lang = st.selectbox("Language", ["python", "javascript", "bash"])

if st.button("Run", type="primary"):
    with st.spinner("Executing..."):
        res = requests.post(API_URL, json={
            "code": code,
            "interpreter": lang
        })
        st.code(res.json()["output"])
```

**2. Static HTML + JS (Recommended for simplicity)**
```html
<!-- index.html - Single file, no build needed -->
<!DOCTYPE html>
<html>
<head>
    <style>/* Minimal CSS */</style>
</head>
<body>
    <textarea id="code"></textarea>
    <button onclick="runCode()">Run</button>
    <pre id="output"></pre>
    <script>
        async function runCode() {
            const res = await fetch('/run', {
                method: 'POST',
                body: document.getElementById('code').value
            });
            document.getElementById('output').textContent = 
                await res.text();
        }
    </script>
</body>
</html>
```

**3. CLI Tool (For developers)**
```bash
# sandrun CLI
echo 'print("hello")' | sandrun --lang python
sandrun script.py
sandrun --watch script.js  # Re-run on changes
```

## Usage Examples

```bash
# Direct API Usage
curl -X POST https://api.sandrun.io/run \
  -H "Content-Type: text/plain" \
  -d 'print("Hello from sandbox")'

# Via Streamlit Frontend
streamlit run frontend.py

# Via Static HTML
python -m http.server 8080  # Serve index.html

# Via CLI
sandrun my_script.py
```

## Future Considerations

**Possible Enhancements (Keeping Simplicity):**
- WebAssembly runtime for additional isolation
- Encrypted job storage with ephemeral keys
- Distributed execution across multiple nodes
- Optional result encryption with client-provided key

**Explicitly Not Planned:**
- User accounts or authentication
- Persistent storage
- Complex job dependencies
- File upload/download beyond code
- Rich API with many endpoints

## Conclusion

Sandrun represents a return to Unix philosophy: do one thing well. It executes code anonymously and ephemerally with strong isolation. No more, no less.

The combination of C++ implementation, Linux security primitives, and aggressive ephemeral design provides a unique sweet spot: practical privacy without cryptographic complexity, strong isolation without virtualization overhead, and fair access without user management.

This is computing as a utility: anonymous, ephemeral, and secure.