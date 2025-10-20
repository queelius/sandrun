# Testing the Trusted Pool Coordinator

## Quick Test

The `test_pool.sh` script provides an automated integration test that:
1. Generates worker keys
2. Starts 2 sandrun workers on ports 18001-18002
3. Starts the pool coordinator on port 19000
4. Submits a test job
5. Verifies job completion and output download

### Running the Test

```bash
cd integrations/trusted-pool
./test_pool.sh
```

**Note:** This requires `sudo` because sandrun workers need privileges for namespace creation.

Expected output:
```
🧪 Testing Trusted Pool Coordinator
====================================

1️⃣  Generating worker keys...
   Worker 1 ID: <base64-public-key>
   Worker 2 ID: <base64-public-key>

2️⃣  Creating workers.json config...
   Config: /tmp/test_pool_workers.json

3️⃣  Starting workers...
   Worker 1 PID: 12345 (port 18001)
   Worker 2 PID: 12346 (port 18002)

...

✅ All tests passed!
```

## Manual Testing

If you prefer to test manually or the automated test fails:

### 1. Generate Worker Keys

```bash
cd /path/to/sandrun

# Generate key for worker 1
./build/sandrun --generate-key /tmp/worker1.pem

# Generate key for worker 2
./build/sandrun --generate-key /tmp/worker2.pem
```

Save the worker IDs shown in the output.

### 2. Create Workers Config

Create `workers.json`:

```json
[
  {
    "worker_id": "<worker-1-id-from-step-1>",
    "endpoint": "http://localhost:8443",
    "max_concurrent_jobs": 2
  },
  {
    "worker_id": "<worker-2-id-from-step-1>",
    "endpoint": "http://localhost:8444",
    "max_concurrent_jobs": 2
  }
]
```

### 3. Start Workers

Terminal 1:
```bash
sudo ./build/sandrun --port 8443 --worker-key /tmp/worker1.pem
```

Terminal 2:
```bash
sudo ./build/sandrun --port 8444 --worker-key /tmp/worker2.pem
```

### 4. Start Coordinator

Terminal 3:
```bash
cd integrations/trusted-pool
python3 coordinator.py --port 9000 --workers workers.json
```

You should see:
```
INFO:__main__:Added trusted worker: <worker-1-id>... at http://localhost:8443
INFO:__main__:Added trusted worker: <worker-2-id>... at http://localhost:8444
INFO:__main__:Starting trusted pool coordinator on port 9000
```

### 5. Check Pool Status

```bash
curl http://localhost:9000/pool | python3 -m json.tool
```

Expected:
```json
{
  "total_workers": 2,
  "healthy_workers": 2,
  "total_jobs": 0,
  "queued_jobs": 0,
  "workers": [
    {
      "worker_id": "...",
      "endpoint": "http://localhost:8443",
      "is_healthy": true,
      "active_jobs": 0,
      "max_concurrent_jobs": 2
    },
    ...
  ]
}
```

### 6. Submit Test Job

Create a test project:

```bash
mkdir test-job
cd test-job

# Create Python script
cat > hello.py <<EOF
print("Hello from the pool!")
with open("result.txt", "w") as f:
    f.write("Success!\n")
EOF

# Create manifest
cat > job.json <<EOF
{
  "entrypoint": "hello.py",
  "interpreter": "python3",
  "outputs": ["*.txt"]
}
EOF

# Package and submit
tar czf ../test-job.tar.gz .
cd ..

curl -X POST http://localhost:9000/submit \
  -F "files=@test-job.tar.gz" \
  -F "manifest=$(cat test-job/job.json)"
```

Expected response:
```json
{
  "job_id": "pool-abc123...",
  "status": "queued"
}
```

### 7. Check Job Status

```bash
curl http://localhost:9000/status/pool-abc123 | python3 -m json.tool
```

Expected (when running):
```json
{
  "job_id": "pool-abc123",
  "pool_status": "running",
  "worker_id": "<worker-id>",
  "worker_status": {
    "job_id": "job-xyz789",
    "status": "running",
    ...
  }
}
```

### 8. Download Output

```bash
curl http://localhost:9000/outputs/pool-abc123/result.txt
```

Expected:
```
Success!
```

## Troubleshooting

### Workers Show as Unhealthy

**Check:** Worker health endpoints directly:
```bash
curl http://localhost:8443/health
curl http://localhost:8444/health
```

**Expected:** Each should return:
```json
{
  "status": "healthy",
  "worker_id": "<base64-public-key>"
}
```

**If failing:**
- Ensure workers started with `--worker-key` flag
- Ensure workers are running with sudo
- Check firewall isn't blocking ports

### Job Stuck in "queued"

**Check:** Pool status:
```bash
curl http://localhost:9000/pool | python3 -m json.tool
```

**Look for:**
- `healthy_workers: 0` → Workers aren't healthy
- `active_jobs: max_concurrent_jobs` for all workers → All workers busy

**Fix:**
- Restart unhealthy workers
- Wait for jobs to complete
- Add more workers

### Coordinator Won't Start

**Error:** `ModuleNotFoundError: No module named 'aiohttp'`

**Fix:**
```bash
pip3 install aiohttp aiofiles
```

**Error:** `Permission denied` on workers.json

**Fix:**
```bash
chmod 644 workers.json
```

### Job Fails on Worker

**Check:** Worker logs:
```bash
# If worker started in foreground, check terminal output
# If worker started in background, check /tmp logs

sudo journalctl -u sandrun  # If running as systemd service
```

**Common issues:**
- Invalid manifest JSON
- Missing interpreter (e.g., python3 not installed)
- Job exceeds resource limits

## Performance Testing

### Load Test

Submit multiple jobs to test load balancing:

```bash
for i in {1..10}; do
  curl -X POST http://localhost:9000/submit \
    -F "files=@test-job.tar.gz" \
    -F "manifest=$(cat test-job/job.json)" &
done
```

Check pool status:
```bash
curl http://localhost:9000/pool | python3 -m json.tool
```

You should see jobs distributed across workers:
```json
{
  "total_workers": 2,
  "healthy_workers": 2,
  "total_jobs": 10,
  "queued_jobs": 6,
  "workers": [
    {
      "worker_id": "...",
      "active_jobs": 2,  // At max capacity
      "max_concurrent_jobs": 2
    },
    {
      "worker_id": "...",
      "active_jobs": 2,  // At max capacity
      "max_concurrent_jobs": 2
    }
  ]
}
```

### Stress Test

```bash
# Submit 100 jobs
for i in {1..100}; do
  echo "Submitting job $i..."
  curl -s -X POST http://localhost:9000/submit \
    -F "files=@test-job.tar.gz" \
    -F "manifest=$(cat test-job/job.json)" | jq .job_id
done
```

Monitor queue depth:
```bash
watch -n 1 'curl -s http://localhost:9000/pool | jq ".queued_jobs, .total_jobs"'
```

## CI/CD Integration

For automated testing in CI/CD:

```yaml
# .github/workflows/test-pool.yml
name: Test Pool Coordinator

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v3

      - name: Build sandrun
        run: |
          cmake -B build
          cmake --build build

      - name: Install Python deps
        run: pip3 install aiohttp aiofiles

      - name: Run pool test
        run: |
          cd integrations/trusted-pool
          sudo ./test_pool.sh
```

**Note:** Requires self-hosted runner or container with appropriate permissions for namespace creation.
