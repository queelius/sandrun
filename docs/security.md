# Security Best Practices

Comprehensive security guide for deploying and using Sandrun safely.

## Overview

Sandrun is designed for **untrusted code execution** with strong isolation. However, security is a shared responsibility between the Sandrun software and the deployment environment.

!!! warning "Important Security Notice"
    While Sandrun provides robust isolation, it relies on the Linux kernel. **Always keep your kernel and system updated** with the latest security patches.

## Isolation Layers

Sandrun uses defense-in-depth with multiple isolation layers:

```
┌─────────────────────────────────────┐
│  Application Code (Untrusted)      │
├─────────────────────────────────────┤
│  Seccomp-BPF (~60 safe syscalls)   │ ← Syscall filtering
├─────────────────────────────────────┤
│  Linux Namespaces                   │ ← Process/network/mount isolation
│  - PID, Network, Mount, IPC, UTS   │
├─────────────────────────────────────┤
│  Capability Dropping                │ ← No privileged operations
├─────────────────────────────────────┤
│  Cgroups                           │ ← Resource limits
│  - CPU quota, Memory limit         │
├─────────────────────────────────────┤
│  tmpfs (RAM-only storage)          │ ← No persistent storage
├─────────────────────────────────────┤
│  Linux Kernel                       │
├─────────────────────────────────────┤
│  Hardware                           │
└─────────────────────────────────────┘
```

### 1. Linux Namespaces

Each job runs in isolated namespaces:

- **PID namespace**: Job cannot see other processes
- **Network namespace**: No network access (airgapped)
- **Mount namespace**: Private filesystem view
- **IPC namespace**: No inter-process communication
- **UTS namespace**: Isolated hostname

### 2. Seccomp-BPF Syscall Filtering

Sandrun uses a **whitelist of ~60 safe syscalls** including:

```c
// Allowed syscalls (examples)
read, write, open, close, stat, fstat
mmap, munmap, brk
exit, exit_group
getpid, getuid, getgid
futex, nanosleep
```

**Blocked syscalls** (dangerous operations):

```c
// Blocked syscalls
socket, connect, bind, listen  // Network access
ptrace, perf_event_open        // Process debugging
reboot, kexec_load             // System control
module_init, module_finit      // Kernel modules
```

### 3. Capability Dropping

All Linux capabilities are dropped before execution:

```c
CAP_SYS_ADMIN    // System administration
CAP_NET_ADMIN    // Network administration
CAP_SYS_MODULE   // Load kernel modules
// ... and 35+ more capabilities
```

### 4. Resource Limits

Cgroups enforce hard limits:

- **CPU**: 10 seconds per minute per IP
- **Memory**: 512MB per job
- **Timeout**: 5 minutes maximum
- **Processes**: 100 processes per job

### 5. tmpfs-only Storage

All job data stored in RAM (tmpfs):

- **No disk writes**: Data never touches persistent storage
- **Automatic cleanup**: Memory released on job completion
- **No swap**: Memory pages locked to prevent disk swap

## Threat Model

### What Sandrun Protects Against

#### ✅ Filesystem Escape
Jobs cannot access files outside their sandbox directory.

```python
# These will fail in sandbox:
open('/etc/passwd', 'r')           # Permission denied
open('/home/user/private.txt', 'r') # Not visible
```

#### ✅ Network Access
Jobs cannot make network connections.

```python
# These will fail in sandbox:
import socket
socket.socket()  # Operation not permitted

import requests
requests.get('http://evil.com')  # No network
```

#### ✅ Process Interference
Jobs cannot see or interact with other processes.

```python
# These will fail in sandbox:
import os
os.system('ps aux')  # Only sees own processes
os.kill(1, 9)        # Cannot kill PID 1
```

#### ✅ Resource Exhaustion
Jobs cannot consume unlimited resources.

```python
# These will be killed:
while True:
    data.append([0] * 1000000)  # Exceeds memory limit

import os
os.fork()  # Exceeds process limit
```

#### ✅ Data Persistence
Job data automatically deleted after use.

### What Sandrun Does NOT Protect Against

#### ❌ Kernel Vulnerabilities
If the Linux kernel has a vulnerability, sandbox escape is possible.

**Mitigation**: Keep kernel updated with security patches.

#### ❌ Timing Attacks
Jobs sharing CPU cores could leak information via timing.

**Mitigation**: Use dedicated hardware for sensitive workloads.

#### ❌ Covert Channels
Advanced attacks (cache timing, speculative execution) could leak data.

**Mitigation**: Not a concern for most use cases. Use dedicated hardware if needed.

#### ❌ Physical Access
Attacker with physical access can bypass all software security.

**Mitigation**: Standard physical security measures.

## Deployment Best Practices

### System Hardening

#### 1. Keep System Updated

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get upgrade -y

# Check kernel version
uname -r  # Should be recent version

# Check for security updates
sudo unattended-upgrade --dry-run
```

#### 2. Use Security Modules

Enable AppArmor or SELinux for additional protection:

```bash
# AppArmor (Ubuntu/Debian)
sudo apt-get install apparmor apparmor-utils
sudo aa-status

# SELinux (RHEL/Fedora)
sestatus
# Should show: SELinux status: enabled
```

#### 3. Configure Firewall

```bash
# Allow only necessary ports
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 8443/tcp  # Sandrun API
sudo ufw enable
```

#### 4. Disable Unnecessary Services

```bash
# List running services
systemctl list-units --type=service --state=running

# Disable unnecessary services
sudo systemctl disable bluetooth
sudo systemctl disable cups
```

### Network Security

#### Use TLS in Production

```bash
# Generate self-signed certificate (testing)
openssl req -x509 -newkey rsa:4096 -nodes \
  -keyout key.pem -out cert.pem -days 365

# Start Sandrun with TLS
sudo ./build/sandrun --port 8443 \
  --cert cert.pem --key key.pem
```

!!! tip "Production TLS"
    Use Let's Encrypt or a commercial CA for production certificates.
    ```bash
    sudo certbot certonly --standalone -d sandrun.example.com
    sudo ./build/sandrun --port 8443 \
      --cert /etc/letsencrypt/live/sandrun.example.com/fullchain.pem \
      --key /etc/letsencrypt/live/sandrun.example.com/privkey.pem
    ```

#### Reverse Proxy Setup

Use nginx or Caddy for TLS termination:

```nginx
# /etc/nginx/sites-available/sandrun
server {
    listen 443 ssl http2;
    server_name sandrun.example.com;

    ssl_certificate /etc/letsencrypt/live/sandrun.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/sandrun.example.com/privkey.pem;

    # Security headers
    add_header Strict-Transport-Security "max-age=31536000" always;
    add_header X-Frame-Options DENY always;
    add_header X-Content-Type-Options nosniff always;

    location / {
        proxy_pass http://localhost:8443;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # WebSocket support
    location /logs/ {
        proxy_pass http://localhost:8443;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
```

### Access Control

#### IP Allowlisting

```nginx
# Restrict access to specific IPs
location /submit {
    allow 192.168.1.0/24;
    allow 10.0.0.0/8;
    deny all;

    proxy_pass http://localhost:8443;
}
```

#### API Authentication (Custom)

While Sandrun doesn't include built-in authentication, you can add it via reverse proxy:

```nginx
# Basic auth with nginx
location / {
    auth_basic "Sandrun API";
    auth_basic_user_file /etc/nginx/.htpasswd;
    proxy_pass http://localhost:8443;
}
```

Or use OAuth2 proxy:
```bash
# OAuth2 Proxy for Google/GitHub auth
docker run -p 4180:4180 \
  -e OAUTH2_PROXY_CLIENT_ID=xxx \
  -e OAUTH2_PROXY_CLIENT_SECRET=yyy \
  quay.io/oauth2-proxy/oauth2-proxy
```

### Monitoring & Logging

#### System Monitoring

```bash
# Monitor resource usage
htop

# Check Sandrun logs
journalctl -u sandrun -f

# Monitor failed jobs
tail -f /var/log/sandrun/errors.log
```

#### Intrusion Detection

```bash
# Install AIDE (file integrity monitoring)
sudo apt-get install aide
sudo aideinit
sudo aide --check

# Install fail2ban (rate limit enforcement)
sudo apt-get install fail2ban
```

#### Prometheus Metrics (Custom Integration)

Add metrics endpoint to track:

- Jobs submitted per minute
- CPU quota usage per IP
- Memory usage per job
- Job failure rates
- Queue depth

### Worker Identity & Verification

For pool deployments, use worker identity for result verification:

#### 1. Generate Worker Keypair

```bash
sudo ./build/sandrun --generate-key /etc/sandrun/worker.pem

# Output:
# ✅ Saved worker key to: /etc/sandrun/worker.pem
#    Worker ID: <base64-public-key>
```

#### 2. Secure Private Key

```bash
# Restrict permissions
sudo chmod 600 /etc/sandrun/worker.pem
sudo chown root:root /etc/sandrun/worker.pem

# Use dedicated key storage
# - Hardware Security Module (HSM)
# - AWS KMS
# - HashiCorp Vault
```

#### 3. Verify Results

```python
import base64
import nacl.signing

# Get job result
result = requests.get(f'http://pool:9000/status/{job_id}').json()

worker_id = result['worker_metadata']['worker_id']
signature = result['worker_metadata']['signature']
job_hash = result['job_hash']

# Verify signature
verify_key = nacl.signing.VerifyKey(base64.b64decode(worker_id))
try:
    verify_key.verify(job_hash.encode(), base64.b64decode(signature))
    print("✅ Result verified!")
except nacl.exceptions.BadSignatureError:
    print("❌ Invalid signature - result tampered!")
```

## Code Execution Safety

### Input Validation

Always validate user inputs before submitting to Sandrun:

```python
import json
import tarfile

def validate_manifest(manifest_json):
    """Validate manifest before submission"""
    try:
        manifest = json.loads(manifest_json)
    except json.JSONDecodeError:
        raise ValueError("Invalid JSON")

    # Required fields
    if 'entrypoint' not in manifest:
        raise ValueError("Missing entrypoint")

    # Validate timeout
    timeout = manifest.get('timeout', 300)
    if timeout > 3600 or timeout < 1:
        raise ValueError("Invalid timeout")

    # Validate memory
    memory_mb = manifest.get('memory_mb', 512)
    if memory_mb > 2048 or memory_mb < 64:
        raise ValueError("Invalid memory limit")

    return manifest

def validate_tarball(tarball_path):
    """Validate tarball before submission"""
    try:
        with tarfile.open(tarball_path, 'r:gz') as tar:
            # Check file count
            members = tar.getmembers()
            if len(members) > 1000:
                raise ValueError("Too many files")

            # Check total size
            total_size = sum(m.size for m in members)
            if total_size > 100 * 1024 * 1024:  # 100MB
                raise ValueError("Tarball too large")

            # Check for path traversal
            for member in members:
                if member.name.startswith('/') or '..' in member.name:
                    raise ValueError("Invalid file path")

    except tarfile.TarError:
        raise ValueError("Invalid tarball")
```

### Sanitize Outputs

Filter sensitive information from job outputs:

```python
import re

def sanitize_logs(logs):
    """Remove sensitive data from logs"""
    # Remove potential secrets
    logs = re.sub(r'API_KEY=\S+', 'API_KEY=***', logs)
    logs = re.sub(r'password=\S+', 'password=***', logs)
    logs = re.sub(r'token:\s*\S+', 'token: ***', logs)

    # Remove IP addresses
    logs = re.sub(r'\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b', 'x.x.x.x', logs)

    return logs
```

## Incident Response

### Detecting Compromises

Warning signs of potential compromise:

1. **Unusual resource usage**
   ```bash
   # Check CPU usage
   top -bn1 | head -20

   # Check memory
   free -h

   # Check network connections (should be minimal)
   netstat -tulpn
   ```

2. **Suspicious job submissions**
   - Very large tarballs
   - Jobs with unusual interpreters
   - Rapid job submissions from single IP

3. **System logs**
   ```bash
   # Check kernel logs
   dmesg | tail -100

   # Check auth logs
   tail -f /var/log/auth.log

   # Check for failed namespace creation
   journalctl -k | grep namespace
   ```

### Response Procedures

If you suspect compromise:

1. **Immediate Actions**
   ```bash
   # Stop Sandrun
   sudo systemctl stop sandrun

   # Kill all sandbox processes
   sudo pkill -9 -f sandrun

   # Check for running jobs
   ps aux | grep sandbox
   ```

2. **Investigation**
   ```bash
   # Collect system state
   ps auxf > /tmp/processes.txt
   netstat -tulpn > /tmp/netstat.txt
   lsof > /tmp/open_files.txt

   # Check for kernel exploits
   dmesg > /tmp/kernel.log
   ```

3. **Recovery**
   ```bash
   # Update system
   sudo apt-get update
   sudo apt-get upgrade -y

   # Rebuild Sandrun from source
   git pull
   rm -rf build
   cmake -B build
   cmake --build build

   # Restart with fresh configuration
   sudo systemctl start sandrun
   ```

## Security Checklist

Use this checklist for production deployments:

- [ ] System fully updated (kernel, packages)
- [ ] Firewall configured (only necessary ports open)
- [ ] TLS enabled with valid certificate
- [ ] Reverse proxy configured with security headers
- [ ] Monitoring and alerting configured
- [ ] Backup and recovery procedures documented
- [ ] Worker keys secured (if using pool)
- [ ] Access logs enabled and reviewed regularly
- [ ] Incident response plan documented
- [ ] Regular security audits scheduled

## Reporting Security Issues

Found a security vulnerability? **Do not open a public issue.**

1. Email security@sandrun.example.com
2. Include:
   - Description of vulnerability
   - Steps to reproduce
   - Impact assessment
   - Suggested fix (if any)

3. Wait for response before public disclosure

We follow responsible disclosure and will:

- Acknowledge within 48 hours
- Provide fix timeline
- Credit reporters (if desired)
- Coordinate disclosure

---

**Questions about security?** [Contact us →](https://github.com/yourusername/sandrun/security)
