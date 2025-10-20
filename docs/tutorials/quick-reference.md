# Tutorial Quick Reference

Quick command reference for the [Distributed Data Pipeline Tutorial](distributed-data-pipeline.md).

## Basic Commands

### Submit Job
```bash
tar czf job.tar.gz <files>
curl -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F "manifest=$(cat job.json)"
```

### Check Status
```bash
curl http://localhost:8443/status/<job_id> | jq
```

### Get Logs
```bash
curl http://localhost:8443/logs/<job_id>
```

### Download Output
```bash
curl http://localhost:8443/outputs/<job_id>/<filename> -o <filename>
```

## Job Manifest Templates

### Basic Python Job
```json
{
  "entrypoint": "main.py",
  "interpreter": "python3"
}
```

### With Dependencies
```json
{
  "entrypoint": "main.py",
  "interpreter": "python3",
  "requirements": "requirements.txt",
  "outputs": ["results/", "*.png"]
}
```

### With Pre-Built Environment
```json
{
  "entrypoint": "main.py",
  "interpreter": "python3",
  "environment": "ml-basic",
  "outputs": ["report.txt"]
}
```

### Full Configuration
```json
{
  "entrypoint": "main.py",
  "interpreter": "python3",
  "environment": "ml-basic",
  "args": ["--input", "data.csv"],
  "outputs": ["results/", "*.json"],
  "timeout": 600,
  "memory_mb": 1024
}
```

## Pool Commands

### Check Pool Status
```bash
curl http://localhost:9000/pool | jq
```

### Submit to Pool
```bash
curl -X POST http://localhost:9000/submit \
  -F "files=@job.tar.gz" \
  -F "manifest=$(cat job.json)"
```

### Get Pool Job Status
```bash
curl http://localhost:9000/status/<pool_job_id> | jq
```

## WebSocket Streaming

### Python Client
```python
import asyncio
import websockets

async def stream_logs(job_id):
    uri = f"ws://localhost:8443/logs/{job_id}/stream"
    async with websockets.connect(uri) as ws:
        async for message in ws:
            print(message, end='')

asyncio.run(stream_logs("job-abc123"))
```

### JavaScript Client
```javascript
const ws = new WebSocket('ws://localhost:8443/logs/job-abc123/stream');
ws.onmessage = (event) => console.log(event.data);
```

## Worker Setup

### Generate Key
```bash
sudo ./build/sandrun --generate-key /etc/sandrun/worker.pem
```

### Start Worker
```bash
sudo ./build/sandrun --port 8443 --worker-key /etc/sandrun/worker.pem
```

### Start Pool Coordinator
```bash
python coordinator.py --port 9000 --workers workers.json
```

## Useful Aliases

Add these to your `~/.bashrc`:

```bash
# Sandrun aliases
alias sandrun-submit='curl -X POST http://localhost:8443/submit'
alias sandrun-status='curl -s http://localhost:8443/status/'
alias sandrun-logs='curl http://localhost:8443/logs/'

# With jq formatting
alias ss='sandrun-status "$1" | jq'
alias sl='sandrun-logs "$1"'

# Pool aliases
alias pool-status='curl -s http://localhost:9000/pool | jq'
alias pool-submit='curl -X POST http://localhost:9000/submit'
```

## Common Patterns

### Extract Job ID
```bash
RESPONSE=$(curl -s -X POST http://localhost:8443/submit ...)
JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
```

### Wait for Completion
```bash
while true; do
    STATUS=$(curl -s http://localhost:8443/status/$JOB_ID | jq -r '.status')
    [ "$STATUS" = "completed" ] && break
    sleep 2
done
```

### Batch Submit
```bash
for file in data/*.csv; do
    # Create job for each file
    tar czf "job_$file.tar.gz" "$file" process.py job.json
    curl -X POST http://localhost:8443/submit \
        -F "files=@job_$file.tar.gz" \
        -F "manifest=$(cat job.json)"
done
```

## Environments

| Name | Packages | Use Case |
|------|----------|----------|
| `ml-basic` | NumPy, Pandas, Scikit-learn | Traditional ML |
| `vision` | PyTorch, OpenCV, Pillow | Computer vision |
| `nlp` | PyTorch, Transformers | NLP/LLMs |
| `data-science` | Pandas, Matplotlib, Jupyter | Data analysis |
| `scientific` | NumPy, SciPy, SymPy | Scientific computing |

## Troubleshooting

### Check Server Health
```bash
curl http://localhost:8443/health
```

### Check Quota
```bash
curl http://localhost:8443/stats | jq '.your_quota'
```

### Verify Worker Identity
```bash
curl http://localhost:8443/health | jq -r '.worker_id'
```

### List Available Environments
```bash
curl http://localhost:8443/environments | jq
```

## Performance Tips

1. **Use pre-built environments** - 20x faster than pip install
2. **Specify outputs** - Only download what you need
3. **Increase memory** - For large datasets (up to 2048MB)
4. **Distribute work** - Use pool for parallel processing
5. **Stream logs** - Monitor long-running jobs in real-time

## Security

### Verify Result Signature
```python
from cryptography.hazmat.primitives.asymmetric import ed25519
import base64, json

def verify(pubkey_b64, signed_data, signature_b64):
    pubkey_bytes = base64.b64decode(pubkey_b64)
    pubkey = ed25519.Ed25519PublicKey.from_public_bytes(pubkey_bytes)
    signature = base64.b64decode(signature_b64)
    message = json.dumps(signed_data, sort_keys=True).encode()
    pubkey.verify(signature, message)
```

## Resources

- [Full Tutorial](distributed-data-pipeline.md) - Complete walkthrough
- [API Reference](../api-reference.md) - Endpoint documentation
- [Job Manifest](../job-manifest.md) - Configuration options
- [Troubleshooting](../troubleshooting.md) - Common issues

---

**Print this page for quick reference during the tutorial!**
