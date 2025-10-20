# Troubleshooting Guide

Solutions to common issues and error messages.

## Quick Diagnosis

Start here to quickly identify your issue:

```bash
# Check if Sandrun is running
ps aux | grep sandrun

# Check if port is accessible
curl http://localhost:8443/

# Check system resources
free -h
df -h

# Check kernel support
cat /proc/sys/kernel/seccomp  # Should be 2
ls /proc/self/ns/               # Should list namespaces

# Check recent logs
journalctl -u sandrun -n 50
```

## Installation Issues

### CMake Cannot Find Dependencies

**Symptom:**
```
CMake Error: Could not find libseccomp
```

**Solution:**

=== "Ubuntu/Debian"
    ```bash
    sudo apt-get install -y \
      libseccomp-dev \
      libcap-dev \
      libssl-dev \
      pkg-config
    ```

=== "Fedora/RHEL"
    ```bash
    sudo dnf install -y \
      libseccomp-devel \
      libcap-devel \
      openssl-devel
    ```

=== "Build from source"
    ```bash
    # libseccomp
    git clone https://github.com/seccomp/libseccomp
    cd libseccomp
    ./autogen.sh
    ./configure
    make
    sudo make install
    ```

### Build Fails with Compiler Errors

**Symptom:**
```
error: 'optional' is not a member of 'std'
```

**Solution:**

```bash
# Check GCC version (need 7.0+)
gcc --version

# Update if needed (Ubuntu)
sudo apt-get install g++-9
export CXX=g++-9

# Clean and rebuild
rm -rf build
cmake -B build -DCMAKE_CXX_COMPILER=g++-9
cmake --build build
```

### CMake Version Too Old

**Symptom:**
```
CMake 3.10 or higher is required. You are running version 2.8
```

**Solution:**

```bash
# Install latest CMake
wget https://github.com/Kitware/CMake/releases/download/v3.25.0/cmake-3.25.0-linux-x86_64.sh
chmod +x cmake-3.25.0-linux-x86_64.sh
sudo ./cmake-3.25.0-linux-x86_64.sh --prefix=/usr/local --skip-license

# Verify
cmake --version
```

## Server Startup Issues

### Permission Denied Creating Namespace

**Symptom:**
```
[ERROR] Failed to create namespace: Operation not permitted
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

=== "Check namespace support"
    ```bash
    # Check if unprivileged user namespaces are enabled
    cat /proc/sys/kernel/unprivileged_userns_clone

    # Enable if disabled
    sudo sysctl -w kernel.unprivileged_userns_clone=1

    # Make permanent
    echo 'kernel.unprivileged_userns_clone=1' | \
      sudo tee -a /etc/sysctl.conf
    sudo sysctl -p
    ```

### Port Already in Use

**Symptom:**
```
[ERROR] Failed to bind to port 8443: Address already in use
```

**Solutions:**

```bash
# Option 1: Find what's using the port
sudo lsof -i :8443
sudo netstat -tulpn | grep 8443

# Option 2: Kill the process
sudo kill <PID>

# Option 3: Use different port
sudo ./build/sandrun --port 9000

# Option 4: Stop existing Sandrun instance
sudo systemctl stop sandrun
# or
sudo pkill -f sandrun
```

### Seccomp Not Supported

**Symptom:**
```
[ERROR] Seccomp not supported on this kernel
```

**Solution:**

```bash
# Check seccomp support
cat /proc/sys/kernel/seccomp
# Should output: 2

# If 0, rebuild kernel with CONFIG_SECCOMP=y
# Or use a distribution with seccomp enabled

# Check kernel version (need 4.6+)
uname -r

# Update kernel if needed
sudo apt-get install linux-generic-hwe-20.04
sudo reboot
```

### Cannot Open Worker Key File

**Symptom:**
```
[ERROR] Failed to open worker key: /etc/sandrun/worker.pem: No such file or directory
```

**Solution:**

```bash
# Generate new worker key
sudo mkdir -p /etc/sandrun
sudo ./build/sandrun --generate-key /etc/sandrun/worker.pem

# Or skip worker key if not using pools
sudo ./build/sandrun --port 8443  # No --worker-key flag
```

## Job Submission Issues

### Invalid Manifest

**Symptom:**
```json
{
  "error": "Invalid manifest",
  "details": "Missing required field: entrypoint"
}
```

**Solution:**

```bash
# Validate JSON syntax
echo '{"entrypoint":"main.py"}' | jq .

# Minimum valid manifest
cat > manifest.json <<EOF
{
  "entrypoint": "main.py",
  "interpreter": "python3"
}
EOF

# Submit with manifest
curl -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F "manifest=$(cat manifest.json)"
```

### Tarball Too Large

**Symptom:**
```json
{
  "error": "Upload too large",
  "details": "Maximum size: 100MB"
}
```

**Solutions:**

```bash
# Check tarball size
ls -lh job.tar.gz

# Compress better
tar czf job.tar.gz --best my_project/

# Remove unnecessary files
tar czf job.tar.gz \
  --exclude='*.pyc' \
  --exclude='__pycache__' \
  --exclude='.git' \
  --exclude='node_modules' \
  my_project/

# Split into multiple jobs if needed
```

### Files Missing in Tarball

**Symptom:**
Job fails with `FileNotFoundError: main.py`

**Solution:**

```bash
# List tarball contents
tar -tzf job.tar.gz

# Ensure entrypoint is included
tar -tzf job.tar.gz | grep main.py

# Create tarball correctly
cd project_directory
tar czf ../job.tar.gz .
# Not: tar czf job.tar.gz project_directory
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

```bash
# Check your quota
curl http://localhost:8443/stats

# Wait for quota to reset
sleep 60

# Optimize code to use less CPU
# Split long jobs into smaller chunks
# Use more efficient algorithms
```

## Job Execution Issues

### Job Failed with Exit Code 1

**Symptom:**
```json
{
  "status": "failed",
  "exit_code": 1
}
```

**Diagnosis:**

```bash
# Get full logs
curl http://localhost:8443/logs/job-abc123

# Common causes:
# - Syntax error in code
# - Missing dependency
# - File not found
# - Permission error
```

**Solutions:**

```bash
# Test locally first
cd project_directory
python3 main.py  # Test before submitting

# Add debugging
cat > main.py <<EOF
import sys
print("Python version:", sys.version)
print("Working directory:", os.getcwd())
print("Files:", os.listdir('.'))
# ... your code ...
EOF
```

### Job Killed (Exit Code 137)

**Symptom:**
```json
{
  "status": "failed",
  "exit_code": 137,
  "details": "Killed by signal 9"
}
```

**Causes:**

- **Out of memory** (exceeded 512MB limit)
- **Timeout** (exceeded 5 minute limit)

**Solutions:**

```bash
# Increase memory limit in manifest
cat > manifest.json <<EOF
{
  "entrypoint": "main.py",
  "memory_mb": 1024
}
EOF

# Increase timeout
cat > manifest.json <<EOF
{
  "entrypoint": "main.py",
  "timeout": 600
}
EOF

# Optimize memory usage
# - Use generators instead of lists
# - Process data in chunks
# - Delete large objects when done
```

### Permission Denied Inside Sandbox

**Symptom:**
```
PermissionError: [Errno 13] Permission denied: '/etc/passwd'
```

**Explanation:**

This is **expected behavior**. The sandbox restricts access to:

- Host filesystem (only job directory accessible)
- Network (completely blocked)
- System files (`/etc`, `/proc`, `/sys` read-only)

**Solutions:**

```bash
# Copy needed files into job directory
cp /etc/hosts my_project/hosts
tar czf job.tar.gz my_project/

# In your code, use relative paths
with open('hosts', 'r') as f:  # Not /etc/hosts
    data = f.read()
```

### Import Errors (Missing Dependencies)

**Symptom:**
```
ModuleNotFoundError: No module named 'numpy'
```

**Solutions:**

=== "Use requirements.txt"
    ```bash
    # Create requirements.txt
    cat > requirements.txt <<EOF
    numpy==1.24.0
    pandas==1.5.0
    EOF

    # Add to manifest
    cat > manifest.json <<EOF
    {
      "entrypoint": "main.py",
      "requirements": "requirements.txt"
    }
    EOF
    ```

=== "Use pre-built environment"
    ```bash
    # List available environments
    curl http://localhost:8443/environments

    # Use in manifest
    cat > manifest.json <<EOF
    {
      "entrypoint": "main.py",
      "environment": "ml-basic"
    }
    EOF
    ```

=== "Include dependencies in tarball"
    ```bash
    # Install to local directory
    pip install -t ./libs numpy pandas

    # In your code
    import sys
    sys.path.insert(0, './libs')
    import numpy
    ```

### Job Stuck in "queued" Status

**Symptom:**
Job never starts executing.

**Diagnosis:**

```bash
# Check system stats
curl http://localhost:8443/stats

# Response shows:
# "queue_length": 10  # Many queued jobs
# "active_jobs": 2    # System busy
```

**Causes:**

- Too many concurrent jobs (2 per IP limit)
- System overloaded
- No available workers (if using pool)

**Solutions:**

```bash
# Wait for active jobs to complete
# Or cancel queued jobs if needed

# Check worker health (pool deployments)
curl http://pool:9000/pool
```

### Cannot Download Output Files

**Symptom:**
```json
{
  "error": "Job not found or expired"
}
```

**Causes:**

- Job auto-deleted after 1 hour
- Already downloaded (immediate deletion)
- Job failed (deleted after 5 minutes)

**Solutions:**

```bash
# Download immediately after completion
# Check status first
STATUS=$(curl -s http://localhost:8443/status/job-abc123 | jq -r '.status')

if [ "$STATUS" = "completed" ]; then
  curl http://localhost:8443/download/job-abc123/output.txt -o output.txt
fi

# Use WebSocket streaming to monitor completion
```

## Pool Coordinator Issues

### No Available Workers

**Symptom:**
```
WARNING:__main__:No available workers for job pool-xxx
```

**Diagnosis:**

```bash
# Check pool status
curl http://pool:9000/pool

# Response shows:
# "healthy_workers": 0
```

**Causes:**

- All workers offline
- Workers failed health check
- Workers at max capacity

**Solutions:**

```bash
# Check worker health directly
curl http://worker1:8443/health

# Start more workers
sudo ./build/sandrun --port 8443 --worker-key /etc/sandrun/worker.pem

# Check worker logs
journalctl -u sandrun -f

# Verify workers.json configuration
cat workers.json
# Ensure worker IDs and endpoints are correct
```

### Worker Authentication Failed

**Symptom:**
```
ERROR:__main__:Health check failed for worker: Invalid worker_id
```

**Solution:**

```bash
# Regenerate worker key
sudo ./build/sandrun --generate-key /etc/sandrun/worker.pem

# Copy output Worker ID
# Update workers.json with new worker_id

# Restart worker
sudo systemctl restart sandrun
```

### Jobs Stuck in Pool Queue

**Symptom:**
Jobs never dispatched to workers.

**Diagnosis:**

```bash
# Check pool logs
journalctl -u pool-coordinator -f

# Check worker endpoints
for worker in worker1 worker2 worker3; do
  echo "Testing $worker:"
  curl http://$worker:8443/health
done
```

**Solutions:**

```bash
# Verify network connectivity
ping worker1
telnet worker1 8443

# Check firewall rules
sudo iptables -L -n

# Ensure workers are reachable from coordinator
# Update workers.json with correct endpoints
```

## Performance Issues

### Slow Job Execution

**Diagnosis:**

```bash
# Check system load
uptime
top

# Check I/O wait
iostat -x 1

# Check memory pressure
free -h
vmstat 1
```

**Solutions:**

```bash
# Increase system resources
# Add more RAM
# Use faster CPU
# Add more workers for horizontal scaling

# Optimize job code
# Use compiled languages for CPU-intensive tasks
# Minimize disk I/O
# Use efficient algorithms
```

### High Memory Usage

**Diagnosis:**

```bash
# Check memory usage
free -h

# Check tmpfs usage
df -h /dev/shm
mount | grep tmpfs

# Check per-process memory
ps aux --sort=-%mem | head -20
```

**Solutions:**

```bash
# Increase system RAM
# Reduce concurrent job limit
# Reduce per-job memory limit
# Optimize job code for memory efficiency

# Clean up old jobs manually if needed
sudo systemctl restart sandrun
```

## Debugging Tools

### Enable Debug Logging

```bash
# Build with debug symbols
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with verbose logging
sudo ./build/sandrun --port 8443 --verbose

# Or set environment variable
export SANDRUN_LOG_LEVEL=debug
sudo -E ./build/sandrun --port 8443
```

### Use GDB for Crashes

```bash
# Build with debug symbols
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run under GDB
sudo gdb ./build/sandrun
(gdb) run --port 8443
# Wait for crash
(gdb) backtrace
(gdb) info locals
```

### Memory Leak Detection

```bash
# Run with Valgrind
sudo valgrind \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --verbose \
  --log-file=valgrind.log \
  ./build/sandrun --port 8443

# Analyze results
cat valgrind.log
```

### Network Debugging

```bash
# Monitor HTTP traffic
sudo tcpdump -i any -nn -A port 8443

# Or use Wireshark
sudo wireshark

# Test with verbose curl
curl -v http://localhost:8443/
```

## Getting Help

If you're still stuck:

1. **Check logs:**
   ```bash
   journalctl -u sandrun -n 100
   dmesg | tail -50
   ```

2. **Gather system info:**
   ```bash
   ./build/sandrun --version
   uname -a
   cat /etc/os-release
   ```

3. **Search existing issues:**
   [GitHub Issues](https://github.com/yourusername/sandrun/issues)

4. **Ask for help:**
   - [GitHub Discussions](https://github.com/yourusername/sandrun/discussions)
   - Include: OS, kernel version, error messages, logs

5. **Report bugs:**
   - [File an issue](https://github.com/yourusername/sandrun/issues/new)
   - Include: steps to reproduce, expected vs actual behavior

---

**Still need help?** [Open an issue →](https://github.com/yourusername/sandrun/issues/new)
