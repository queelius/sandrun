# Getting Started

This guide will walk you through installing Sandrun, running your first job, and understanding the core workflow.

!!! info "Estimated Time"
    5-10 minutes to install and run your first job

## Prerequisites

Before installing Sandrun, ensure your system meets these requirements:

### System Requirements

| Requirement | Specification |
|-------------|---------------|
| **Operating System** | Linux (Ubuntu 20.04+, Debian 11+, or equivalent) |
| **Kernel Version** | 4.6+ (for namespace support) |
| **RAM** | 2GB minimum (4GB+ recommended) |
| **Disk Space** | 500MB for build artifacts |
| **Root Access** | Required for namespace creation |

### Check Your System

```bash
# Verify kernel version (should be 4.6+)
uname -r

# Check if namespaces are supported
ls /proc/self/ns/

# Verify seccomp support
cat /proc/sys/kernel/seccomp  # Should output: 2
```

!!! warning "Root Permissions Required"
    Sandrun requires root privileges to create Linux namespaces. You'll need to run it with `sudo` or grant the CAP_SYS_ADMIN capability.

## Installation

### 1. Install Dependencies

=== "Ubuntu/Debian"
    ```bash
    sudo apt-get update
    sudo apt-get install -y \
      build-essential \
      cmake \
      libseccomp-dev \
      libcap-dev \
      libssl-dev \
      pkg-config \
      git
    ```

=== "Fedora/RHEL"
    ```bash
    sudo dnf install -y \
      gcc-c++ \
      cmake \
      libseccomp-devel \
      libcap-devel \
      openssl-devel \
      pkgconfig \
      git
    ```

=== "Arch Linux"
    ```bash
    sudo pacman -S \
      base-devel \
      cmake \
      libseccomp \
      libcap \
      openssl \
      pkgconf \
      git
    ```

### 2. Clone and Build

```bash
# Clone repository
git clone https://github.com/yourusername/sandrun.git
cd sandrun

# Configure build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (use -j for parallel compilation)
cmake --build build -j$(nproc)

# Verify build
./build/sandrun --help
```

!!! success "Build Output"
    If successful, you should see:
    ```
    Usage: sandrun [options]
    Options:
      --port PORT          Server port (default: 8443)
      --worker-key FILE    Worker private key for signing
      --generate-key FILE  Generate new worker keypair
      --help              Show this help message
    ```

### 3. Run Server

```bash
# Start server (requires sudo for namespace creation)
sudo ./build/sandrun --port 8443
```

**Expected Output:**
```
[INFO] Sandrun v1.0.0 starting...
[INFO] Initializing sandbox environment
[INFO] Loading environment templates
[INFO] Server listening on http://0.0.0.0:8443
[INFO] Press Ctrl+C to stop
```

!!! tip "Running Without Sudo"
    For production deployments, you can grant specific capabilities instead of full root:
    ```bash
    sudo setcap cap_sys_admin,cap_sys_chroot,cap_setuid,cap_setgid+ep ./build/sandrun
    ./build/sandrun --port 8443  # No sudo needed
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

## Understanding Rate Limits

Sandrun uses IP-based rate limiting to ensure fair resource sharing:

### Default Limits

| Limit Type | Value | Window |
|------------|-------|--------|
| **CPU Quota** | 10 CPU-seconds | Per minute |
| **Concurrent Jobs** | 2 jobs | At a time |
| **Memory per Job** | 512MB | Per job |
| **Timeout** | 5 minutes | Per job |

### Check Your Quota

```bash
curl http://localhost:8443/stats
```

**Response:**
```json
{
  "your_quota": {
    "used": 2.5,
    "limit": 10.0,
    "available": 7.5,
    "active_jobs": 1,
    "can_submit": true
  },
  "system": {
    "queue_length": 3,
    "active_jobs": 5
  }
}
```

!!! tip "Quota Management"
    - Quota resets after 1 hour of inactivity
    - CPU time is measured in actual CPU seconds, not wall-clock time
    - Use different IP addresses for separate quotas (if needed for testing)

## Troubleshooting

### Permission Denied

**Symptom:**
```
Error: Permission denied creating namespace
```

**Solutions:**

=== "Use sudo"
    ```bash
    sudo ./build/sandrun --port 8443
    ```

=== "Grant capabilities"
    ```bash
    sudo setcap cap_sys_admin,cap_sys_chroot,cap_setuid,cap_setgid+ep ./build/sandrun
    ./build/sandrun --port 8443
    ```

=== "Check user namespaces"
    ```bash
    # Some systems disable user namespaces for security
    cat /proc/sys/kernel/unprivileged_userns_clone
    # Should output: 1 (enabled)

    # If disabled, enable it:
    sudo sysctl -w kernel.unprivileged_userns_clone=1
    ```

### Port Already in Use

**Symptom:**
```
Error: Failed to bind to port 8443: Address already in use
```

**Solutions:**

```bash
# Option 1: Use a different port
sudo ./build/sandrun --port 9000

# Option 2: Find and kill the process using the port
sudo lsof -i :8443
sudo kill <PID>

# Option 3: Let the OS assign a port
sudo ./build/sandrun --port 0  # Uses random available port
```

### Build Failures

**Symptom:**
```
CMake Error: Could not find libseccomp
```

**Solution:**
```bash
# Ensure all dependencies are installed
sudo apt-get install -y libseccomp-dev libcap-dev libssl-dev

# Clean and rebuild
rm -rf build
cmake -B build
cmake --build build
```

### Job Failed to Execute

**Symptom:**
Job status shows `"status": "failed"` with non-zero exit code.

**Debugging Steps:**

1. **Check logs for errors:**
   ```bash
   curl http://localhost:8443/logs/job-abc123
   ```

2. **Verify manifest syntax:**
   ```bash
   echo '{"entrypoint":"main.py","interpreter":"python3"}' | jq .
   ```

3. **Check file permissions:**
   ```bash
   # Ensure entrypoint is included in tarball
   tar -tzf job.tar.gz
   ```

4. **Test locally first:**
   ```bash
   python3 main.py  # Test before submitting
   ```

### Rate Limit Exceeded

**Symptom:**
```json
{
  "error": "Rate limit exceeded",
  "reason": "CPU quota exhausted (10.2/10.0 seconds used)",
  "retry_after": 45
}
```

**Solutions:**

- **Wait for quota refresh** (shown in `retry_after` field)
- **Optimize your code** to use less CPU time
- **Split jobs** into smaller chunks
- **Use different IP** (for testing only)

### Cannot Access Server

**Symptom:**
```
curl: (7) Failed to connect to localhost port 8443: Connection refused
```

**Checklist:**

1. **Verify server is running:**
   ```bash
   ps aux | grep sandrun
   ```

2. **Check if port is listening:**
   ```bash
   sudo netstat -tlnp | grep 8443
   ```

3. **Check firewall rules:**
   ```bash
   sudo ufw status
   sudo iptables -L -n | grep 8443
   ```

4. **Verify server logs:**
   ```bash
   # If running in terminal, check stdout
   # Or check system logs if running as service
   journalctl -u sandrun -f
   ```

## Using Pre-built Environments

Sandrun includes pre-built Python environments with common packages for faster execution.

### Example: ML Job with ml-basic Environment

```bash
# Create training script
cat > train.py <<'EOF'
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier

print("Packages loaded successfully!")
print(f"NumPy version: {np.__version__}")
print(f"Pandas version: {pd.__version__}")

# Create sample data
X = np.random.rand(100, 5)
y = np.random.randint(0, 2, 100)

# Train model
model = RandomForestClassifier()
model.fit(X, y)

print(f"Model trained! Accuracy: {model.score(X, y):.2f}")
EOF

# Create manifest with environment
cat > job.json <<'EOF'
{
  "entrypoint": "train.py",
  "interpreter": "python3",
  "environment": "ml-basic"
}
EOF

# Package and submit
tar czf ml-job.tar.gz train.py
curl -X POST http://localhost:8443/submit \
  -F "files=@ml-job.tar.gz" \
  -F "manifest=$(cat job.json)"
```

**Benefits:**
- ✅ No pip install time (packages pre-installed)
- ✅ Reproducible versions
- ✅ Cached across jobs for efficiency

### Available Environments

| Environment | Packages | Use Case |
|-------------|----------|----------|
| `ml-basic` | NumPy, Pandas, Scikit-learn, Matplotlib | Traditional ML |
| `vision` | PyTorch, Torchvision, OpenCV, Pillow | Computer vision |
| `nlp` | PyTorch, Transformers, Tokenizers | NLP/language models |
| `data-science` | NumPy, Pandas, Matplotlib, Seaborn, Jupyter | Data analysis |
| `scientific` | NumPy, SciPy, SymPy, Matplotlib | Scientific computing |

**Check available environments:**
```bash
curl http://localhost:8443/environments
```

## Next Steps

- [API Reference](api-reference.md) - Complete API documentation
- [Job Manifest](job-manifest.md) - Advanced job configuration
- [Integrations](integrations/trusted-pool.md) - Pool coordinator, MCP server
- [Architecture](architecture.md) - Understand the internals
