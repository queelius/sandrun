# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

Sandrun (formerly jobd) is an anonymous batch job execution system with two components:
- **C++ Backend**: Secure sandboxed execution with Linux namespaces
- **Vue Frontend**: Directory upload, job monitoring, and output retrieval

## Development Commands

### Building C++ Backend

```bash
# Create build directory
cmake -B build

# Build the project
cmake --build build

# Run the backend
sudo ./build/sandrun --port 8443
```

### Running Vue Frontend

```bash
# Serve the frontend (development)
cd frontend
python3 -m http.server 8080

# Or use any static server
npx serve frontend
```

### Testing

```bash
# Submit a job
curl -X POST http://localhost:8443/submit \
  -F "files=@project.tar.gz" \
  -F 'manifest={"entrypoint":"main.py"}'

# Check status
curl http://localhost:8443/status/{job_id}

# Get logs
curl http://localhost:8443/logs/{job_id}
```

## Architecture

### Core Components

1. **C++ Sandbox (`src/sandbox.cpp`)**:
   - Linux namespace isolation (PID, network, mount, IPC, UTS)
   - Seccomp-BPF syscall filtering
   - Memory-only execution in tmpfs
   - Resource limits via cgroups
   - Automatic cleanup and privacy protection

2. **Job Queue (`src/job_queue.cpp`)**:
   - Anonymous job submission (no accounts)
   - IP-based rate limiting
   - Manifest-driven execution
   - Output streaming and selective download

3. **HTTP Server (`src/http_server.cpp`)**:
   - Minimal HTTP/1.1 implementation
   - Multipart form data handling
   - WebSocket support for log streaming
   - TLS-only communication

4. **Vue Frontend (`frontend/index.html`)**:
   - Single-file application (no build needed)
   - Directory upload with drag & drop
   - Real-time job monitoring
   - Selective output download

### Security Model

- **No User Accounts**: Completely anonymous, only IP-based rate limiting
- **Namespace Isolation**: Each job in separate PID, network, mount namespaces
- **Seccomp Filtering**: Whitelist of ~50 safe syscalls only
- **Memory-Only**: All execution in tmpfs, nothing touches disk
- **Auto-Deletion**: All job data destroyed after download or timeout

### Job Execution Flow

1. Client uploads project directory via `POST /submit`
2. System parses manifest (`job.json`) for execution parameters
3. Job enters queue with extracted metadata
4. When resources available, job starts in sandbox:
   - New namespaces created
   - Files extracted to tmpfs
   - Dependencies installed if specified
   - Entrypoint executed with args
5. Logs streamed to client via WebSocket or polling
6. On completion, outputs filtered by manifest patterns
7. Client downloads outputs (auto-deleted after)

## Job Manifest

Jobs are configured via `job.json` manifest:

```json
{
  "entrypoint": "main.py",
  "interpreter": "python3",
  "args": ["--input", "data.csv"],
  "outputs": ["results/", "*.png"],
  "timeout": 300,
  "memory_mb": 512
}
```

See `MANIFEST.md` for full specification.

## Privacy Features

- **No Persistence**: Everything runs in RAM (tmpfs)
- **No Logging**: Only metrics logged, never job contents
- **IP Rotation**: Rate limits reset after 1 hour of inactivity
- **Secure Deletion**: Files overwritten before removal
- **Output Filtering**: Only requested files leave sandbox

## Resource Limits

- **Per IP**: 10 CPU-seconds per minute
- **Per Job**: 512MB RAM, 5 minute timeout
- **Queue**: Max 2 concurrent jobs per IP
- **Storage**: 100MB per job in tmpfs