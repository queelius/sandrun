# 🏃 Sandrun

> Anonymous, ephemeral, sandboxed code execution service

Sandrun provides secure, isolated code execution without user accounts or data persistence. Submit code, get results, everything auto-deletes. Simple, private, secure.

## ✨ Features

- **🔒 Secure Sandbox** - Linux namespaces, seccomp-BPF, resource limits
- **🎭 Anonymous** - No accounts, no tracking, no logs
- **💨 Ephemeral** - All data in tmpfs (RAM), auto-deletes after use
- **🚀 Fast** - Native C++ implementation, minimal overhead
- **🌐 Multi-language** - Python, Node.js, Bash, and more
- **📦 Directory Upload** - Submit entire projects with dependencies
- **⚡ Simple API** - RESTful endpoints, multipart uploads
- **🎯 Resource Limits** - CPU quotas, memory limits, timeouts

## 🚀 Quick Start

### Build and Run

```bash
# Install dependencies
sudo apt-get install libseccomp-dev libcap-dev

# Build
cmake -B build
cmake --build build

# Run server (requires root for namespaces)
sudo ./build/sandrun --port 8443
```

### Submit a Job

```bash
# Quick Python code
curl -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F 'manifest={"entrypoint":"main.py","interpreter":"python3"}'
```

## 🏗️ Architecture

```
┌─────────────┐     HTTP/REST     ┌──────────────┐
│   Client    │ ◄────────────────► │  HTTP Server │
└─────────────┘                    └──────┬───────┘
                                          │
                                    ┌─────▼──────┐
                                    │ Job Queue  │
                                    └─────┬──────┘
                                          │
                                    ┌─────▼──────────┐
                                    │ Sandbox        │
                                    │ ┌────────────┐ │
                                    │ │ Namespaces │ │
                                    │ │ Seccomp    │ │
                                    │ │ Cgroups    │ │
                                    │ │ tmpfs      │ │
                                    │ └────────────┘ │
                                    └────────────────┘
```

### Security Layers

1. **Namespace Isolation** - PID, network, mount, IPC, UTS
2. **Syscall Filtering** - Seccomp-BPF whitelist (~60 safe syscalls)
3. **Resource Limits** - Memory, CPU, processes, file descriptors
4. **Network Isolation** - Complete network namespace isolation
5. **Filesystem Isolation** - tmpfs only, no persistent storage
6. **Capability Dropping** - All Linux capabilities dropped

## 📡 API Reference

### Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/` | Server info and status |
| `POST` | `/submit` | Submit new job |
| `GET` | `/status/{job_id}` | Get job status |
| `GET` | `/logs/{job_id}` | Get stdout/stderr |
| `GET` | `/outputs/{job_id}` | List output files |
| `GET` | `/download/{job_id}/{file}` | Download output file |

### Job Manifest

```json
{
  "entrypoint": "main.py",           // Required: Entry file
  "interpreter": "python3",          // Required: Interpreter
  "args": ["--input", "data.csv"],  // Optional: Arguments
  "outputs": ["*.png", "results/"], // Optional: Output patterns
  "timeout": 300,                    // Optional: Timeout (seconds)
  "memory_mb": 512                  // Optional: Memory limit (MB)
}
```

### Rate Limits

- **10 CPU-seconds per minute** per IP
- **2 concurrent jobs** per IP
- **20 jobs per hour** per IP
- **512MB RAM** per job
- **5 minute** maximum execution time

## 🎨 Integrations

### Web Frontend

Simple, elegant web interface with configurable server endpoint:

```bash
cd integrations/web-frontend
python3 -m http.server 8000
# Open http://localhost:8000
```

### Python Client

```python
from integrations.python_client.sandrun_client import SandrunClient

client = SandrunClient("http://localhost:8443")
result = client.run_and_wait(
    code="print('Hello, Sandrun!')",
    interpreter="python3"
)
print(result['logs']['stdout'])
```

### Command Line

```bash
# See examples
./integrations/examples/curl_examples.sh
```

## 🔧 Configuration

### Build Options

```bash
# Debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Release build with optimizations
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Custom port
./build/sandrun --port 9000
```

### Resource Limits (constants.h)

```cpp
constexpr size_t DEFAULT_MEMORY_LIMIT = 512 * 1024 * 1024;  // 512MB
constexpr size_t MAX_OUTPUT_SIZE = 10 * 1024 * 1024;        // 10MB
constexpr int DEFAULT_TIMEOUT_SECONDS = 300;                // 5 minutes
constexpr int MAX_PROCESSES_PER_JOB = 32;                   // Thread limit
```

## 🛡️ Security Considerations

Sandrun is designed for **defensive security** only:

- ✅ Run untrusted code safely
- ✅ Educational sandboxing
- ✅ CI/CD testing
- ✅ Code evaluation
- ❌ NOT for cryptocurrency mining
- ❌ NOT for network attacks
- ❌ NOT for system exploitation

### Threat Model

Sandrun protects against:
- Code execution exploits
- Container escapes
- Resource exhaustion
- Data persistence
- Network access
- System file access

## 📊 Performance

- **Startup overhead**: ~100ms per job
- **Memory overhead**: ~10MB base + job requirements
- **Concurrent jobs**: Limited by system resources
- **Throughput**: ~10-50 jobs/second (depends on job complexity)

## 🤝 Contributing

Contributions are welcome! Key principles:

1. **Simplicity** - Avoid unnecessary complexity
2. **Security** - Every change must maintain security guarantees
3. **Privacy** - No tracking, logging, or data retention
4. **Elegance** - Clean, readable, maintainable code

## 📜 License

MIT License - See [LICENSE](LICENSE) file for details.

## ⚠️ Disclaimer

Sandrun is provided as-is for educational and defensive security purposes. Users are responsible for compliance with applicable laws and regulations. The authors assume no liability for misuse.

## 🙏 Acknowledgments

Built with:
- Linux kernel namespaces
- libseccomp for syscall filtering
- Modern C++17
- Security best practices from container runtimes

---

**Remember**: Sandrun is about providing secure, anonymous code execution. No accounts, no tracking, no persistence. Just code in, results out, then everything disappears. Simple as that.
