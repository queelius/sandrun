# Trusted Pool Coordinator

A simple pool coordinator that routes jobs to allowlisted workers. Workers are trusted based on their Ed25519 public keys.

## Architecture

```
Client → Pool Coordinator → Trusted Workers
                ↓
         Allowlist (public keys)
         Health checking
         Load balancing
```

### Trust Model

- **Workers are allowlisted** by their Ed25519 public keys
- **No result verification** needed (trusted execution)
- **Health checking** ensures worker availability
- **Load balancing** distributes jobs across available workers

This is simpler than the trustless pool because:
- No consensus needed
- No verification of results
- No economic incentives (stake/slash)
- Workers are pre-approved and trusted

## Setup

### 1. Install Dependencies

```bash
cd integrations/trusted-pool
pip install -r requirements.txt
```

### 2. Configure Workers

Create `workers.json` with your trusted workers:

```json
[
  {
    "worker_id": "base64-encoded-ed25519-public-key",
    "endpoint": "http://worker1.example.com:8443",
    "max_concurrent_jobs": 4
  },
  {
    "worker_id": "another-public-key-base64",
    "endpoint": "http://worker2.example.com:8443",
    "max_concurrent_jobs": 4
  }
]
```

To get a worker's public key (worker_id):

```bash
# On worker machine:
./sandrun --generate-key /etc/sandrun/worker.pem

# Output shows:
# ✅ Saved worker key to: /etc/sandrun/worker.pem
#    Worker ID: <base64-encoded-public-key>
```

Add the worker ID to your `workers.json` allowlist.

### 3. Start Workers

On each worker machine:

```bash
sudo ./sandrun --port 8443 --worker-key /etc/sandrun/worker.pem
```

### 4. Start Pool Coordinator

```bash
python coordinator.py --port 9000 --workers workers.json
```

## Usage

### Submit Job to Pool

Instead of submitting directly to a worker, submit to the pool coordinator:

```bash
curl -X POST http://pool.example.com:9000/submit \
  -F "files=@project.tar.gz" \
  -F 'manifest={"entrypoint":"main.py","interpreter":"python3"}'
```

Response:
```json
{
  "job_id": "pool-a1b2c3d4e5f6",
  "status": "queued"
}
```

### Check Job Status

```bash
curl http://pool.example.com:9000/status/pool-a1b2c3d4e5f6
```

Response:
```json
{
  "job_id": "pool-a1b2c3d4e5f6",
  "pool_status": "running",
  "worker_id": "base64-worker-public-key",
  "worker_status": {
    "job_id": "remote-job-id-on-worker",
    "status": "running",
    "execution_metadata": {
      "cpu_seconds": 1.23,
      "memory_peak_bytes": 52428800
    }
  },
  "submitted_at": 1234567890.123,
  "completed_at": null
}
```

### Download Output

```bash
curl http://pool.example.com:9000/outputs/pool-a1b2c3d4e5f6/results/output.png \
  -o output.png
```

### Check Pool Status

```bash
curl http://pool.example.com:9000/pool
```

Response:
```json
{
  "total_workers": 3,
  "healthy_workers": 2,
  "total_jobs": 15,
  "queued_jobs": 2,
  "workers": [
    {
      "worker_id": "worker-1-public-key",
      "endpoint": "http://worker1.example.com:8443",
      "is_healthy": true,
      "active_jobs": 3,
      "max_concurrent_jobs": 4,
      "last_health_check": 1234567890.123
    }
  ]
}
```

## API Endpoints

### POST /submit
Submit a job to the pool.

**Request:**
- `files`: Tarball of project files (multipart/form-data)
- `manifest`: Job manifest JSON

**Response:**
```json
{
  "job_id": "pool-xxx",
  "status": "queued"
}
```

### GET /status/{job_id}
Get job status.

**Response:**
```json
{
  "job_id": "pool-xxx",
  "pool_status": "running",
  "worker_id": "worker-public-key",
  "worker_status": { ... },
  "submitted_at": 1234567890.123,
  "completed_at": null
}
```

### GET /outputs/{job_id}/{path}
Download output file.

**Response:** Binary file content

### GET /pool
Get pool status.

**Response:**
```json
{
  "total_workers": 3,
  "healthy_workers": 2,
  "total_jobs": 10,
  "queued_jobs": 1,
  "workers": [ ... ]
}
```

## How It Works

### Job Flow

1. **Client submits job** to pool coordinator
2. **Job enters queue** with "queued" status
3. **Coordinator finds available worker** (healthy, not overloaded)
4. **Job dispatched to worker** via HTTP POST to worker's /submit endpoint
5. **Worker executes job** in sandbox
6. **Client polls status** via pool coordinator (proxied to worker)
7. **Client downloads outputs** via pool coordinator (proxied from worker)

### Health Checking

- Pool coordinator checks each worker every 30 seconds
- Health check: `GET http://worker:8443/health`
- Expected response: `{"status":"healthy","worker_id":"..."}`
- Unhealthy workers are excluded from routing

### Load Balancing

- Jobs routed to worker with **fewest active jobs**
- Workers have `max_concurrent_jobs` limit (default: 4)
- If no workers available, job waits in queue

### Failure Handling

- If worker rejects job → job re-queued
- If worker fails health check → marked unhealthy, excluded from routing
- Jobs in progress on failed workers remain assigned (client can retry)

## Differences from Trustless Pool

| Feature | Trusted Pool | Trustless Pool |
|---------|-------------|----------------|
| Worker authorization | Allowlist (public keys) | Open (anyone can join) |
| Result verification | None (trust workers) | Hash comparison + consensus |
| Economic model | None | Stake + slashing |
| Complexity | Simple (~200 lines) | Complex (~1000+ lines) |
| Use case | Private cluster, known workers | Public compute, anonymous workers |

## Security Considerations

### Worker Authentication

Workers must be started with `--worker-key` to have an identity. The pool coordinator verifies worker identity during health checks:

```python
if data.get("worker_id") == worker.worker_id:
    worker.is_healthy = True
```

This prevents:
- **Impersonation**: Rogue server can't pretend to be allowlisted worker
- **Unauthorized workers**: Only allowlisted workers receive jobs

### Network Security

Since this is a trusted pool, you should:

1. **Use private network** or VPN for worker communication
2. **Enable TLS** on workers (add HTTPS support)
3. **Firewall workers** to only accept from coordinator IP
4. **Restrict pool coordinator** to authorized clients

### Resource Limits

Workers enforce their own resource limits (as configured in sandrun). The pool coordinator adds:
- **max_concurrent_jobs**: Prevent worker overload
- **Job queueing**: Prevent coordinator overload
- **Health checks**: Detect and exclude failed workers

## Monitoring

### Logs

The coordinator logs:
- Job submissions and dispatching
- Worker health status changes
- Errors and warnings

Example:
```
INFO:__main__:Added trusted worker: a1b2c3d4e5f6... at http://worker1:8443
INFO:__main__:Queued job pool-abc123
INFO:__main__:Dispatched job pool-abc123 to a1b2c3d4e5f6... (remote: job-xyz789)
WARNING:__main__:Health check failed for b2c3d4e5f6g7...: Connection refused
```

### Metrics

Check `/pool` endpoint for real-time metrics:
- Total workers and healthy count
- Total jobs and queue depth
- Per-worker active job count

## Future Enhancements

Potential improvements for production use:

1. **Persistent storage** for job history (currently in-memory)
2. **Worker capacity discovery** (auto-detect max_concurrent_jobs)
3. **Job priority queues** (high/low priority jobs)
4. **Authentication** for clients (API keys, OAuth)
5. **TLS support** for encrypted communication
6. **Metrics export** (Prometheus, Grafana)
7. **Job cancellation** (cancel in-progress jobs)
8. **Worker drain mode** (stop accepting new jobs for maintenance)

## Troubleshooting

### No workers available

```
WARNING:__main__:No available workers for job pool-xxx
```

**Causes:**
- All workers unhealthy (check worker logs)
- All workers at max capacity (check `/pool` endpoint)
- Workers not started with `--worker-key`

**Solution:**
- Start more workers
- Increase `max_concurrent_jobs` per worker
- Check worker health endpoints directly

### Jobs stuck in "queued" status

**Causes:**
- No healthy workers available
- Worker endpoints incorrect in workers.json

**Solution:**
- Check `/pool` endpoint for worker health status
- Verify worker endpoints are reachable
- Check worker logs for errors

### Worker rejected job

```
ERROR:__main__:Worker a1b2c3d4... rejected job: 400
```

**Causes:**
- Invalid manifest format
- Files too large for worker
- Worker resource limits exceeded

**Solution:**
- Check worker logs for specific error
- Verify manifest is valid JSON
- Reduce job size or increase worker limits

## Example Deployment

### 3-Worker Pool

```bash
# Worker 1
sudo ./sandrun --port 8443 --worker-key /etc/sandrun/worker1.pem

# Worker 2
sudo ./sandrun --port 8443 --worker-key /etc/sandrun/worker2.pem

# Worker 3
sudo ./sandrun --port 8443 --worker-key /etc/sandrun/worker3.pem

# Coordinator
python coordinator.py --port 9000 --workers workers.json
```

**workers.json:**
```json
[
  {
    "worker_id": "worker1-public-key-from-generate-key",
    "endpoint": "http://192.168.1.101:8443",
    "max_concurrent_jobs": 4
  },
  {
    "worker_id": "worker2-public-key-from-generate-key",
    "endpoint": "http://192.168.1.102:8443",
    "max_concurrent_jobs": 4
  },
  {
    "worker_id": "worker3-public-key-from-generate-key",
    "endpoint": "http://192.168.1.103:8443",
    "max_concurrent_jobs": 4
  }
]
```

Now you can submit jobs to the pool at `http://coordinator-ip:9000/submit` and they will be automatically distributed across the 3 workers!
