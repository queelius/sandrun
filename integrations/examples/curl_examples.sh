#!/bin/bash
# Sandrun API Examples using curl
# Simple, elegant examples for interacting with Sandrun

# Configuration
SANDRUN_SERVER="${SANDRUN_SERVER:-http://localhost:8443}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Sandrun API Examples ===${NC}"
echo "Server: $SANDRUN_SERVER"
echo

# Helper function to print sections
section() {
    echo -e "\n${GREEN}>>> $1${NC}"
}

# Helper function to run command and show it
run_cmd() {
    echo -e "${YELLOW}$ $1${NC}"
    eval "$1"
    echo
}

# Test connection
section "Test Connection"
run_cmd "curl -s $SANDRUN_SERVER | jq '.'"

# Example 1: Submit simple Python code
section "Example 1: Quick Python Code"

# Create a simple Python file
cat > /tmp/hello.py << 'EOF'
print("Hello from Sandrun!")
import sys
print(f"Python version: {sys.version}")

# Create output file
with open("output.txt", "w") as f:
    f.write("Job completed successfully\n")
EOF

# Create tar.gz
tar -czf /tmp/simple_job.tar.gz -C /tmp hello.py

# Submit job
section "Submitting job..."
JOB_ID=$(curl -s -X POST $SANDRUN_SERVER/submit \
    -F "files=@/tmp/simple_job.tar.gz" \
    -F 'manifest={"entrypoint":"hello.py","interpreter":"python3"}' \
    | jq -r '.job_id')

echo "Job ID: $JOB_ID"

# Wait for completion
section "Waiting for job to complete..."
sleep 3

# Check status
section "Check job status"
run_cmd "curl -s $SANDRUN_SERVER/status/$JOB_ID | jq '.'"

# Get logs
section "Get job logs"
run_cmd "curl -s $SANDRUN_SERVER/logs/$JOB_ID | jq '.'"

# List output files
section "List output files"
run_cmd "curl -s $SANDRUN_SERVER/outputs/$JOB_ID | jq '.'"

# Example 2: Submit with arguments
section "Example 2: Job with Arguments"

cat > /tmp/args_example.py << 'EOF'
import sys
import json

print(f"Arguments received: {sys.argv[1:]}")

# Process arguments
result = {
    "args_count": len(sys.argv) - 1,
    "args": sys.argv[1:]
}

with open("result.json", "w") as f:
    json.dump(result, f, indent=2)

print("Result written to result.json")
EOF

tar -czf /tmp/args_job.tar.gz -C /tmp args_example.py

JOB_ID_2=$(curl -s -X POST $SANDRUN_SERVER/submit \
    -F "files=@/tmp/args_job.tar.gz" \
    -F 'manifest={
        "entrypoint":"args_example.py",
        "interpreter":"python3",
        "args":["--input","data.csv","--output","results.json"]
    }' | jq -r '.job_id')

echo "Job ID: $JOB_ID_2"
sleep 3

section "Get results"
run_cmd "curl -s $SANDRUN_SERVER/logs/$JOB_ID_2 | jq '.stdout' -r"

# Example 3: Data processing job
section "Example 3: Data Processing Job"

# Create a more complex job with multiple files
mkdir -p /tmp/data_job
cat > /tmp/data_job/processor.py << 'EOF'
import csv
import json

def process_data():
    # Simulate data processing
    data = [
        {"id": 1, "value": 100},
        {"id": 2, "value": 200},
        {"id": 3, "value": 300}
    ]
    
    # Calculate statistics
    total = sum(item["value"] for item in data)
    avg = total / len(data)
    
    result = {
        "total": total,
        "average": avg,
        "count": len(data)
    }
    
    # Write CSV output
    with open("output.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["id", "value"])
        writer.writeheader()
        writer.writerows(data)
    
    # Write JSON output
    with open("stats.json", "w") as f:
        json.dump(result, f, indent=2)
    
    return result

if __name__ == "__main__":
    result = process_data()
    print(f"Processing complete: {result}")
EOF

# Create manifest file
cat > /tmp/data_job/job.json << 'EOF'
{
    "entrypoint": "processor.py",
    "interpreter": "python3",
    "outputs": ["*.csv", "*.json"],
    "timeout": 60,
    "memory_mb": 256
}
EOF

# Create tar.gz
tar -czf /tmp/data_job.tar.gz -C /tmp/data_job .

# Submit with embedded manifest
JOB_ID_3=$(curl -s -X POST $SANDRUN_SERVER/submit \
    -F "files=@/tmp/data_job.tar.gz" \
    -F 'manifest={"entrypoint":"processor.py","interpreter":"python3","outputs":["*.csv","*.json"]}' \
    | jq -r '.job_id')

echo "Job ID: $JOB_ID_3"
sleep 3

section "Get processing results"
run_cmd "curl -s $SANDRUN_SERVER/logs/$JOB_ID_3 | jq '.stdout' -r"

section "List generated files"
run_cmd "curl -s $SANDRUN_SERVER/outputs/$JOB_ID_3 | jq '.'"

# Example 4: Bash script execution
section "Example 4: Bash Script"

cat > /tmp/system_info.sh << 'EOF'
#!/bin/bash
echo "=== System Information ==="
echo "Date: $(date)"
echo "Hostname: $(hostname)"
echo "User: $(whoami)"
echo "Working dir: $(pwd)"
echo "Files: $(ls -la)"
echo

echo "=== Resource Limits ==="
ulimit -a

echo "=== Environment ==="
env | grep -E "PATH|HOME|USER" | sort
EOF

tar -czf /tmp/bash_job.tar.gz -C /tmp system_info.sh

JOB_ID_4=$(curl -s -X POST $SANDRUN_SERVER/submit \
    -F "files=@/tmp/bash_job.tar.gz" \
    -F 'manifest={"entrypoint":"system_info.sh","interpreter":"bash"}' \
    | jq -r '.job_id')

echo "Job ID: $JOB_ID_4"
sleep 2

section "Bash script output"
run_cmd "curl -s $SANDRUN_SERVER/logs/$JOB_ID_4 | jq '.stdout' -r"

# Example 5: Error handling
section "Example 5: Error Handling"

cat > /tmp/error_example.py << 'EOF'
import sys

print("This will print to stdout")
print("Error: Something went wrong!", file=sys.stderr)
sys.exit(1)
EOF

tar -czf /tmp/error_job.tar.gz -C /tmp error_example.py

JOB_ID_5=$(curl -s -X POST $SANDRUN_SERVER/submit \
    -F "files=@/tmp/error_job.tar.gz" \
    -F 'manifest={"entrypoint":"error_example.py","interpreter":"python3"}' \
    | jq -r '.job_id')

echo "Job ID: $JOB_ID_5"
sleep 2

section "Check failed job"
run_cmd "curl -s $SANDRUN_SERVER/status/$JOB_ID_5 | jq '.'"
run_cmd "curl -s $SANDRUN_SERVER/logs/$JOB_ID_5 | jq '.'"

# Cleanup
section "Cleanup"
rm -f /tmp/*.py /tmp/*.tar.gz /tmp/*.sh
rm -rf /tmp/data_job
echo "Temporary files cleaned up"

echo -e "\n${BLUE}=== Examples Complete ===${NC}"
echo "You can now use these patterns to submit your own jobs to Sandrun!"
echo
echo "Tips:"
echo "  - Jobs auto-delete after 60 seconds for privacy"
echo "  - Use 'jq' for pretty JSON output"
echo "  - Set SANDRUN_SERVER environment variable to use different server"
echo "  - Check the Python client for more advanced usage"