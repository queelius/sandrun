# Getting Started

This guide will help you set up and start using Sandrun.

## Prerequisites

- **OS**: Linux (Ubuntu 20.04+ or Debian 11+ recommended)
- **Root Access**: Required for namespace creation
- **Dependencies**: Build tools and libraries

## Installation

### 1. Install Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  libseccomp-dev \
  libcap-dev \
  libssl-dev \
  pkg-config
```

### 2. Clone and Build

```bash
# Clone repository
git clone https://github.com/yourusername/sandrun.git
cd sandrun

# Build
cmake -B build
cmake --build build

# Verify build
./build/sandrun --help
```

### 3. Run Server

```bash
# Start server (requires sudo for namespace creation)
sudo ./build/sandrun --port 8443

# Server output:
# Worker ID: <if using --worker-key>
# Server listening on port 8443
```

## Your First Job

### Simple Python Script

```bash
# Create script
cat > hello.py <<'EOF'
print("Hello from Sandrun!")
import sys
print(f"Python version: {sys.version}")
EOF

# Create manifest
cat > job.json <<'EOF'
{
  "entrypoint": "hello.py",
  "interpreter": "python3"
}
EOF

# Package files
tar czf job.tar.gz hello.py

# Submit job
curl -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F "manifest=$(cat job.json)"

# Response:
# {
#   "job_id": "job-abc123def456",
#   "status": "queued"
# }
```

### Check Job Status

```bash
# Get status
curl http://localhost:8443/status/job-abc123def456

# Response:
# {
#   "job_id": "job-abc123def456",
#   "status": "completed",
#   "execution_metadata": {
#     "cpu_seconds": 0.05,
#     "memory_peak_bytes": 12582912,
#     "exit_code": 0
#   },
#   "output_files": {
#     ...
#   }
# }
```

### Get Logs

```bash
# Get stdout
curl http://localhost:8443/logs/job-abc123def456

# Response:
# Hello from Sandrun!
# Python version: 3.10.12 (main, ...)
```

## Multi-File Projects

### Upload Entire Directory

```bash
# Create project
mkdir my-project
cd my-project

cat > main.py <<'EOF'
from utils import greet

greet("World")
EOF

cat > utils.py <<'EOF'
def greet(name):
    print(f"Hello, {name}!")
EOF

cat > job.json <<'EOF'
{
  "entrypoint": "main.py",
  "interpreter": "python3",
  "outputs": ["*.txt", "results/"]
}
EOF

# Package and submit
tar czf ../my-project.tar.gz .
cd ..
curl -X POST http://localhost:8443/submit \
  -F "files=@my-project.tar.gz" \
  -F "manifest=$(cat my-project/job.json)"
```

## Using Environments

Sandrun supports pre-built environments with common packages:

```bash
cat > job.json <<'EOF'
{
  "entrypoint": "ml_script.py",
  "interpreter": "python3",
  "environment": "ml-basic"
}
EOF
```

Available environments:

- `ml-basic` - NumPy, SciPy, pandas
- `vision` - OpenCV, Pillow, scikit-image
- `nlp` - NLTK, spaCy, transformers
- `data-science` - Jupyter, matplotlib, seaborn
- `scientific` - SymPy, NetworkX, statsmodels

List available environments:

```bash
curl http://localhost:8443/environments
```

## Worker Identity (Optional)

Generate a worker identity for signed results:

```bash
# Generate key
sudo ./build/sandrun --generate-key /etc/sandrun/worker.pem

# Output:
# ✅ Saved worker key to: /etc/sandrun/worker.pem
#    Worker ID: <base64-encoded-public-key>

# Start with identity
sudo ./build/sandrun --port 8443 --worker-key /etc/sandrun/worker.pem
```

Jobs executed by this worker will include:

```json
{
  "worker_metadata": {
    "worker_id": "base64-encoded-public-key",
    "signature": "base64-encoded-signature"
  }
}
```

## Rate Limits

Sandrun enforces IP-based rate limits:

- **10 CPU-seconds per minute** per IP
- **512MB RAM** per job
- **5 minute timeout** per job
- **2 concurrent jobs** per IP

Check your quota:

```bash
curl http://localhost:8443/stats

# Response:
# {
#   "your_quota": {
#     "used": 2.5,
#     "limit": 10.0,
#     "available": 7.5,
#     "active_jobs": 1,
#     "can_submit": true
#   },
#   "system": {
#     "queue_length": 3,
#     "active_jobs": 5
#   }
# }
```

## Troubleshooting

### Permission Denied

```bash
# Error: Permission denied creating namespace
# Solution: Run with sudo
sudo ./build/sandrun --port 8443
```

### Port Already in Use

```bash
# Error: Failed to bind to port
# Solution: Use different port
sudo ./build/sandrun --port 9000
```

### Job Failed

```bash
# Check logs for errors
curl http://localhost:8443/logs/job-abc123

# Check status for exit code
curl http://localhost:8443/status/job-abc123
```

### Quota Exceeded

```bash
# Error: Rate limit exceeded
# Solution: Wait for quota to refresh (1 hour of inactivity)
# Or use different IP address
```

## Next Steps

- [API Reference](api-reference.md) - Complete API documentation
- [Job Manifest](job-manifest.md) - Advanced job configuration
- [Integrations](integrations/trusted-pool.md) - Pool coordinator, MCP server
- [Architecture](architecture.md) - Understand the internals
