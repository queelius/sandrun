# Sandrun Broker - Simple Job Distribution

A lightweight job broker for distributing work across multiple sandrun nodes. No blockchain, no complexity - just a simple HTTP coordinator.

## Architecture

```
Client → Broker Server → Sandrun Nodes
         (job queue)     (execute jobs)
```

## Quick Start

### 1. Start Broker Server

```bash
cd server
pip install -r requirements.txt
python broker.py --port 8000
```

### 2. Start Sandrun Node with Broker Client

```bash
# Terminal 1: Start sandrun
sudo sandrun --port 8443

# Terminal 2: Connect to broker
cd node_client
python node.py --broker http://localhost:8000 --sandrun http://localhost:8443
```

### 3. Submit Jobs

```python
import requests

# Submit job
response = requests.post('http://localhost:8000/submit', json={
    'code': 'print("Hello distributed world!")',
    'interpreter': 'python3',
    'timeout': 60
})

job_id = response.json()['job_id']

# Check status
status = requests.get(f'http://localhost:8000/status/{job_id}')
print(status.json())

# Get results
results = requests.get(f'http://localhost:8000/results/{job_id}')
print(results.json()['output'])
```

## Components

### Broker Server (`server/broker.py`)
- Maintains job queue in SQLite
- Tracks registered nodes
- Assigns jobs to available nodes
- Stores results temporarily

### Node Client (`node_client/node.py`)
- Registers with broker
- Polls for available jobs
- Executes via local sandrun
- Returns results to broker

## API Endpoints

### Broker Server

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/submit` | POST | Submit new job |
| `/status/{job_id}` | GET | Check job status |
| `/results/{job_id}` | GET | Get job results |
| `/nodes` | GET | List registered nodes |
| `/register` | POST | Register node (internal) |
| `/heartbeat` | POST | Node keepalive (internal) |
| `/claim` | POST | Claim job (internal) |

## Database Schema

```sql
-- Jobs table
CREATE TABLE jobs (
    id TEXT PRIMARY KEY,
    code TEXT,
    interpreter TEXT,
    status TEXT,  -- 'pending', 'assigned', 'running', 'completed', 'failed'
    node_id TEXT,
    output TEXT,
    error TEXT,
    created_at TIMESTAMP,
    completed_at TIMESTAMP
);

-- Nodes table
CREATE TABLE nodes (
    id TEXT PRIMARY KEY,
    endpoint TEXT,
    capabilities TEXT,  -- JSON
    last_heartbeat TIMESTAMP,
    jobs_completed INTEGER
);
```

## Features

- ✅ Simple HTTP-based coordination
- ✅ SQLite persistence (no external DB needed)
- ✅ Automatic node failover
- ✅ Basic load balancing
- ✅ Result caching
- ✅ No blockchain complexity
- ✅ No cryptocurrency required
- ✅ Works with existing sandrun

## Configuration

### Broker Server
```python
# server/config.py
JOB_TIMEOUT = 300  # seconds
RESULT_TTL = 3600  # seconds
NODE_TIMEOUT = 60  # seconds before marking node dead
MAX_RETRIES = 3
```

### Node Client
```json
// node_client/config.json
{
  "broker_url": "http://localhost:8000",
  "sandrun_url": "http://localhost:8443",
  "poll_interval": 5,
  "capabilities": {
    "cpu_cores": 4,
    "memory_gb": 8,
    "gpu": false,
    "interpreters": ["python3", "node", "bash"]
  }
}
```

## Security Notes

- Broker should run behind HTTPS in production
- Use API keys for authentication (not implemented in demo)
- Sandrun provides the actual security isolation
- Broker is just a coordinator, not a security boundary

## Future Enhancements (if needed)

- [ ] Payment integration (Stripe/PayPal)
- [ ] Priority queues
- [ ] Geographic node selection
- [ ] Job result verification (multiple nodes)
- [ ] Web dashboard
- [ ] Metrics and monitoring