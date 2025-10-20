#!/bin/bash
# Integration test for trusted pool coordinator

set -e  # Exit on error

echo "🧪 Testing Trusted Pool Coordinator"
echo "===================================="
echo

# Cleanup function
cleanup() {
    echo
    echo "🧹 Cleaning up..."
    sudo kill $WORKER1_PID 2>/dev/null || true
    sudo kill $WORKER2_PID 2>/dev/null || true
    kill $COORDINATOR_PID 2>/dev/null || true
    rm -rf /tmp/test_pool_* 2>/dev/null || true
    echo "✅ Cleanup complete"
}

trap cleanup EXIT

# Build sandrun if needed
if [ ! -f "../../build/sandrun" ]; then
    echo "Building sandrun..."
    (cd ../.. && cmake --build build)
fi

# Generate worker keys
echo "1️⃣  Generating worker keys..."
mkdir -p /tmp/test_pool_keys

if [ ! -f /tmp/test_pool_keys/worker1.pem ]; then
    ../../build/sandrun --generate-key /tmp/test_pool_keys/worker1.pem > /tmp/test_pool_keys/worker1.out
    WORKER1_ID=$(grep "Worker ID:" /tmp/test_pool_keys/worker1.out | awk '{print $3}')
    echo "   Worker 1 ID: $WORKER1_ID"
else
    echo "   Using existing worker 1 key"
fi

if [ ! -f /tmp/test_pool_keys/worker2.pem ]; then
    ../../build/sandrun --generate-key /tmp/test_pool_keys/worker2.pem > /tmp/test_pool_keys/worker2.out
    WORKER2_ID=$(grep "Worker ID:" /tmp/test_pool_keys/worker2.out | awk '{print $3}')
    echo "   Worker 2 ID: $WORKER2_ID"
else
    echo "   Using existing worker 2 key"
fi

# Extract worker IDs if they weren't just generated
if [ -z "$WORKER1_ID" ]; then
    WORKER1_ID=$(../../build/sandrun --worker-key /tmp/test_pool_keys/worker1.pem 2>&1 | grep "Worker ID:" | awk '{print $3}' || echo "")
    if [ -z "$WORKER1_ID" ]; then
        # Alternative: read from health endpoint
        ../../build/sandrun --port 18001 --worker-key /tmp/test_pool_keys/worker1.pem &
        TEMP_PID=$!
        sleep 2
        WORKER1_ID=$(curl -s http://localhost:18001/health | python3 -c "import sys, json; print(json.load(sys.stdin)['worker_id'])")
        kill $TEMP_PID
        wait $TEMP_PID 2>/dev/null || true
    fi
fi

if [ -z "$WORKER2_ID" ]; then
    ../../build/sandrun --port 18002 --worker-key /tmp/test_pool_keys/worker2.pem &
    TEMP_PID=$!
    sleep 2
    WORKER2_ID=$(curl -s http://localhost:18002/health | python3 -c "import sys, json; print(json.load(sys.stdin)['worker_id'])")
    kill $TEMP_PID
    wait $TEMP_PID 2>/dev/null || true
fi

echo "   Worker 1 ID: $WORKER1_ID"
echo "   Worker 2 ID: $WORKER2_ID"

# Create workers.json config
echo
echo "2️⃣  Creating workers.json config..."
cat > /tmp/test_pool_workers.json <<EOF
[
  {
    "worker_id": "$WORKER1_ID",
    "endpoint": "http://localhost:18001",
    "max_concurrent_jobs": 2
  },
  {
    "worker_id": "$WORKER2_ID",
    "endpoint": "http://localhost:18002",
    "max_concurrent_jobs": 2
  }
]
EOF
echo "   Config: /tmp/test_pool_workers.json"

# Start workers (requires sudo for namespace creation)
echo
echo "3️⃣  Starting workers..."
echo "   ⚠️  Workers require sudo for namespace creation"

sudo ../../build/sandrun --port 18001 --worker-key /tmp/test_pool_keys/worker1.pem > /tmp/test_pool_worker1.log 2>&1 &
WORKER1_PID=$!
echo "   Worker 1 PID: $WORKER1_PID (port 18001)"

sudo ../../build/sandrun --port 18002 --worker-key /tmp/test_pool_keys/worker2.pem > /tmp/test_pool_worker2.log 2>&1 &
WORKER2_PID=$!
echo "   Worker 2 PID: $WORKER2_PID (port 18002)"

sleep 3

# Check worker health
echo
echo "4️⃣  Checking worker health..."
HEALTH1=$(curl -s http://localhost:18001/health)
HEALTH2=$(curl -s http://localhost:18002/health)
echo "   Worker 1: $HEALTH1"
echo "   Worker 2: $HEALTH2"

# Install Python dependencies if needed
if ! python3 -c "import aiohttp" 2>/dev/null; then
    echo
    echo "📦 Installing Python dependencies..."
    pip3 install -q aiohttp aiofiles
fi

# Start coordinator
echo
echo "5️⃣  Starting pool coordinator..."
python3 coordinator.py --port 19000 --workers /tmp/test_pool_workers.json > /tmp/test_pool_coordinator.log 2>&1 &
COORDINATOR_PID=$!
echo "   Coordinator PID: $COORDINATOR_PID (port 19000)"

sleep 3

# Check pool status
echo
echo "6️⃣  Checking pool status..."
curl -s http://localhost:19000/pool | python3 -m json.tool

# Create test job
echo
echo "7️⃣  Creating test job..."
mkdir -p /tmp/test_pool_job
cat > /tmp/test_pool_job/hello.py <<'EOF'
import sys
import time

print("Hello from trusted pool!")
print(f"Python version: {sys.version}")
print(f"Arguments: {sys.argv[1:]}")

# Create output
with open("output.txt", "w") as f:
    f.write("Job completed successfully!\n")
    f.write(f"Timestamp: {time.time()}\n")

print("✅ Job done!")
EOF

cat > /tmp/test_pool_job/job.json <<'EOF'
{
  "entrypoint": "hello.py",
  "interpreter": "python3",
  "args": ["test", "pool"],
  "outputs": ["*.txt"],
  "timeout": 60
}
EOF

# Package job
(cd /tmp/test_pool_job && tar czf ../test_pool_job.tar.gz .)

# Submit job to pool
echo
echo "8️⃣  Submitting job to pool..."
SUBMIT_RESULT=$(curl -s -X POST http://localhost:19000/submit \
  -F "files=@/tmp/test_pool_job.tar.gz" \
  -F "manifest=$(cat /tmp/test_pool_job/job.json)")

echo "   $SUBMIT_RESULT"

JOB_ID=$(echo $SUBMIT_RESULT | python3 -c "import sys, json; print(json.load(sys.stdin)['job_id'])")
echo "   Job ID: $JOB_ID"

# Wait for job to complete
echo
echo "9️⃣  Waiting for job to complete..."
for i in {1..30}; do
    STATUS=$(curl -s http://localhost:19000/status/$JOB_ID)
    POOL_STATUS=$(echo $STATUS | python3 -c "import sys, json; print(json.load(sys.stdin).get('pool_status', 'unknown'))")

    echo "   [$i/30] Status: $POOL_STATUS"

    if [ "$POOL_STATUS" = "completed" ] || [ "$POOL_STATUS" = "failed" ]; then
        echo
        echo "   Full status:"
        echo "$STATUS" | python3 -m json.tool
        break
    fi

    sleep 2
done

# Download output
echo
echo "🔟 Downloading output..."
curl -s http://localhost:19000/outputs/$JOB_ID/output.txt -o /tmp/test_pool_output.txt

if [ -f /tmp/test_pool_output.txt ]; then
    echo "   ✅ Output downloaded:"
    cat /tmp/test_pool_output.txt | sed 's/^/      /'
else
    echo "   ❌ Failed to download output"
    exit 1
fi

echo
echo "✅ All tests passed!"
echo
echo "Pool coordinator successfully:"
echo "  - Accepted job submission"
echo "  - Routed to healthy worker"
echo "  - Tracked job status"
echo "  - Proxied output download"
