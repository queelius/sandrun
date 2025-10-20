# Sandrun

> Anonymous, ephemeral, sandboxed code execution service

Sandrun provides secure, isolated code execution without user accounts or data persistence. Submit code, get results, everything auto-deletes. Simple, private, secure.

## Features

- **🔒 Secure Sandbox** - Linux namespaces, seccomp-BPF, resource limits
- **🎭 Anonymous** - No accounts, no tracking, no logs
- **💨 Ephemeral** - All data in tmpfs (RAM), auto-deletes after use
- **🚀 Fast** - Native C++ implementation, minimal overhead
- **🌐 Multi-language** - Python, Node.js, Bash, and more
- **📦 Directory Upload** - Submit entire projects with dependencies
- **⚡ Simple API** - RESTful endpoints, multipart uploads
- **🎯 Resource Limits** - CPU quotas, memory limits, timeouts
- **🔐 Worker Identity** - Ed25519 signatures for result verification
- **🔗 Pool Support** - Distribute jobs across multiple workers

## Quick Start

### Installation

```bash
# Install dependencies
sudo apt-get install libseccomp-dev libcap-dev libssl-dev

# Build
cmake -B build
cmake --build build

# Run server (requires root for namespaces)
sudo ./build/sandrun --port 8443
```

### Submit Your First Job

```bash
# Create a simple Python script
echo 'print("Hello, Sandrun!")' > hello.py

# Package and submit
tar czf job.tar.gz hello.py
curl -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F 'manifest={"entrypoint":"hello.py","interpreter":"python3"}'

# Response:
# {"job_id":"job-abc123","status":"queued"}

# Check status
curl http://localhost:8443/status/job-abc123

# Get logs
curl http://localhost:8443/logs/job-abc123
```

## Use Cases

- **Anonymous Code Execution** - No signup, no tracking
- **Ephemeral Compute** - Process data without persistence
- **Batch Processing** - Submit jobs, retrieve results
- **Privacy-First Compute** - Data auto-deletes after use
- **Distributed Computing** - Scale with pool coordinator

## Next Steps

- [Getting Started Guide](getting-started.md) - Detailed setup and usage
- [API Reference](api-reference.md) - Complete REST API documentation
- [Job Manifest](job-manifest.md) - Configure job execution
- [Integrations](integrations/trusted-pool.md) - Extend sandrun capabilities
- [Architecture](architecture.md) - Understand the design

## Community

- **GitHub**: [sandrun](https://github.com/yourusername/sandrun)
- **Issues**: Report bugs or request features
- **Contributions**: Pull requests welcome!

## License

MIT License - See [LICENSE](https://github.com/yourusername/sandrun/blob/master/LICENSE) for details.
