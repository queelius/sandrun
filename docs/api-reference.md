# API Reference

Complete REST API documentation for Sandrun's HTTP interface.

## Overview

Sandrun provides a simple REST API for anonymous code execution. All endpoints return JSON responses (except for file downloads and logs).

### Base URL

```
http://localhost:8443
```

!!! tip "Protocol Support"
    - Production deployments should use HTTPS with valid certificates
    - CORS is enabled by default for web frontend support
    - WebSocket support available for log streaming

### Authentication

**None required!** Sandrun is anonymous by design. Rate limiting is based on client IP address only.

### Response Format

All successful API responses follow this structure:

```json
{
  "job_id": "unique-identifier",
  "status": "queued|running|completed|failed",
  "...": "additional fields"
}
```

Error responses:

```json
{
  "error": "Human-readable error message",
  "details": "Additional context (optional)",
  "retry_after": 30  // For rate limiting (optional)
}
```

## Quick Reference

| Method | Endpoint | Purpose |
|--------|----------|---------|
| GET | `/` | Server info and status |
| POST | `/submit` | Submit new job |
| GET | `/status/{job_id}` | Get job status and metadata |
| GET | `/logs/{job_id}` | Get job stdout/stderr |
| WS | `/logs/{job_id}/stream` | Stream logs in real-time |
| GET | `/outputs/{job_id}` | List output files |
| GET | `/download/{job_id}/{path}` | Download output file |
| GET | `/stats` | Check quota and system stats |
| GET | `/environments` | List available environments |
| GET | `/health` | Health check (for pools) |

## Endpoints

### GET /

Get server information and status.

**Response:**

```json
{
  "service": "sandrun",
  "status": "running",
  "description": "Batch job execution with directory upload",
  "privacy": "Jobs auto-delete after download",
  "limits": "10 CPU-sec/min, 512MB RAM, 5 min timeout"
}
```

### POST /submit

Submit a new job for execution.

**Request:**

Multipart form data:
- `files`: Tarball containing job files (`.tar.gz`)
- `manifest`: JSON manifest (see [Job Manifest](job-manifest.md))

**Example:**

```bash
curl -X POST http://localhost:8443/submit \
  -F "files=@project.tar.gz" \
  -F 'manifest={"entrypoint":"main.py","interpreter":"python3"}'
```

**Response:**

```json
{
  "job_id": "job-abc123def456",
  "status": "queued",
  "position": 3
}
```

**Status Codes:**

- `200 OK` - Job accepted
- `400 Bad Request` - Invalid manifest or files
- `429 Too Many Requests` - Rate limit exceeded

### GET /status/{job_id}

Get job execution status and metadata.

**Response:**

```json
{
  "job_id": "job-abc123def456",
  "status": "completed",
  "execution_metadata": {
    "cpu_seconds": 1.23,
    "memory_peak_bytes": 52428800,
    "exit_code": 0,
    "environment": "default"
  },
  "job_hash": "sha256-hash-of-inputs",
  "output_files": {
    "result.txt": {
      "path": "result.txt",
      "size_bytes": 1024,
      "sha256_hash": "abc123...",
      "type": "file"
    }
  },
  "worker_metadata": {
    "worker_id": "base64-encoded-public-key",
    "signature": "base64-encoded-signature"
  }
}
```

**Job Status Values:**

- `queued` - Waiting in queue
- `running` - Currently executing
- `completed` - Finished successfully
- `failed` - Execution failed

### GET /logs/{job_id}

Get job stdout and stderr logs.

**Response:**

Plain text output from job execution.

**Example:**

```bash
curl http://localhost:8443/logs/job-abc123def456
```

```
Hello from Sandrun!
Processing data...
Results saved to result.txt
```

### WebSocket /logs/{job_id}/stream

Stream logs in real-time via WebSocket.

**Example (JavaScript):**

```javascript
const ws = new WebSocket('ws://localhost:8443/logs/job-abc123def456/stream');

ws.onmessage = (event) => {
  console.log(event.data);
};
```

### GET /outputs/{job_id}

List available output files for download.

**Response:**

```json
{
  "job_id": "job-abc123def456",
  "outputs": [
    {
      "path": "result.txt",
      "size_bytes": 1024,
      "type": "file"
    },
    {
      "path": "images/",
      "type": "directory",
      "files": ["plot1.png", "plot2.png"]
    }
  ]
}
```

### GET /download/{job_id}/{filepath}

Download a specific output file.

**Example:**

```bash
curl http://localhost:8443/download/job-abc123/result.txt -o result.txt
```

**Response:**

Binary file content with appropriate `Content-Type` header.

### GET /stats

Get quota information and system statistics.

**Response:**

```json
{
  "your_quota": {
    "used": 2.5,
    "limit": 10.0,
    "available": 7.5,
    "active_jobs": 1,
    "can_submit": true,
    "reason": ""
  },
  "system": {
    "queue_length": 3,
    "active_jobs": 5
  }
}
```

### GET /environments

List available pre-built environments.

**Response:**

```json
{
  "templates": [
    "default",
    "ml-basic",
    "vision",
    "nlp",
    "data-science",
    "scientific"
  ],
  "stats": {
    "total_templates": 6,
    "cached_environments": 3,
    "total_uses": 42,
    "disk_usage_mb": 512
  }
}
```

### GET /health

Health check endpoint (for pool coordinators).

**Response:**

```json
{
  "status": "healthy",
  "worker_id": "base64-encoded-public-key"
}
```

## Rate Limits

### Per-IP Limits

- **CPU Quota**: 10 CPU-seconds per minute
- **Concurrent Jobs**: 2 jobs maximum
- **Hourly Jobs**: 20 jobs per hour

### Per-Job Limits

- **Memory**: 512MB RAM maximum
- **Timeout**: 5 minutes maximum
- **Storage**: 100MB in tmpfs

### Rate Limit Response

When rate limited:

```json
{
  "error": "Rate limit exceeded",
  "reason": "CPU quota exhausted (10.2/10.0 seconds used)",
  "retry_after": 45
}
```

## Error Responses

All errors follow this format:

```json
{
  "error": "Error message",
  "details": "Additional information"
}
```

### Common Errors

**400 Bad Request:**

```json
{
  "error": "Invalid manifest",
  "details": "Missing required field: entrypoint"
}
```

**404 Not Found:**

```json
{
  "error": "Job not found",
  "details": "job-xyz789"
}
```

**429 Too Many Requests:**

```json
{
  "error": "Rate limit exceeded",
  "reason": "Too many concurrent jobs (2/2)",
  "retry_after": 60
}
```

**500 Internal Server Error:**

```json
{
  "error": "Internal server error",
  "details": "Failed to create sandbox"
}
```

## Authentication

Sandrun is **anonymous** by default. No API keys or authentication required.

For trusted pool deployments, workers authenticate via Ed25519 public keys.

## CORS

CORS is enabled for all origins to support web frontends.

## Content Types

**Request:**
- `multipart/form-data` for `/submit`

**Response:**
- `application/json` for most endpoints
- `text/plain` for `/logs/{job_id}`
- `application/octet-stream` for `/download/{job_id}/{file}`

## Examples

### Submit and Wait for Completion

```bash
#!/bin/bash

# Submit job
RESPONSE=$(curl -s -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F 'manifest={"entrypoint":"main.py","interpreter":"python3"}')

JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
echo "Job ID: $JOB_ID"

# Poll until complete
while true; do
  STATUS=$(curl -s http://localhost:8443/status/$JOB_ID | jq -r '.status')
  echo "Status: $STATUS"

  if [ "$STATUS" = "completed" ] || [ "$STATUS" = "failed" ]; then
    break
  fi

  sleep 2
done

# Get logs
curl http://localhost:8443/logs/$JOB_ID
```

### Stream Logs in Real-Time

```python
import websocket

def on_message(ws, message):
    print(message, end='')

ws = websocket.WebSocketApp(
    "ws://localhost:8443/logs/job-abc123/stream",
    on_message=on_message
)

ws.run_forever()
```

### Download All Outputs

```bash
#!/bin/bash

JOB_ID="job-abc123"

# Get output list
OUTPUTS=$(curl -s http://localhost:8443/outputs/$JOB_ID | jq -r '.outputs[].path')

# Download each file
for OUTPUT in $OUTPUTS; do
  curl -o "$OUTPUT" "http://localhost:8443/download/$JOB_ID/$OUTPUT"
  echo "Downloaded: $OUTPUT"
done
```

## Client Libraries

### Python

```python
from integrations.python_client.sandrun_client import SandrunClient

client = SandrunClient("http://localhost:8443")

# Submit and wait
result = client.run_and_wait(
    code="print('Hello!')",
    interpreter="python3"
)

print(result['logs']['stdout'])
```

### JavaScript

See `integrations/web-frontend/` for a complete web client example.

### cURL

See `integrations/examples/curl_examples.sh` for command-line examples.

## Next Steps

- [Job Manifest](job-manifest.md) - Configure job execution
- [Getting Started](getting-started.md) - Setup and first job
- [Integrations](integrations/trusted-pool.md) - Extend capabilities
