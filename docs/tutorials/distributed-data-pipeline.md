# Building a Distributed Data Processing Pipeline

A hands-on tutorial that takes you from processing a single file to building a distributed pipeline with cryptographic verification.

!!! info "Tutorial Overview"
    **Level:** Beginner to Advanced
    **Time:** 45-60 minutes
    **What You'll Build:** A distributed data processing system that analyzes sales data across multiple workers with real-time monitoring and verified results

## Learning Objectives

By the end of this tutorial, you will:

1. Execute single-file Python scripts in Sandrun
2. Structure multi-file projects with dependencies
3. Use pre-built environments for data science workflows
4. Scale processing across multiple files
5. Set up a trusted pool for distributed execution
6. Monitor jobs with WebSocket streaming
7. Verify results with cryptographic signatures

## Prerequisites

Before starting, ensure you have:

- [x] Sandrun server running locally (see [Getting Started](../getting-started.md))
- [x] Python 3.8+ installed
- [x] Basic familiarity with Python and pandas
- [x] `curl` and `jq` installed for API interaction
- [x] (Optional) Multiple machines or VMs for distributed setup

**Quick verification:**
```bash
# Check Sandrun is running
curl http://localhost:8443/health

# Expected response:
# {"status":"healthy"}
```

## Part 1: Simple Data Analysis

Let's start by analyzing a single CSV file with basic Python.

### Step 1.1: Create Sample Data

```bash
# Create a sample sales dataset
cat > sales.csv <<'EOF'
date,product,quantity,revenue
2025-01-15,Widget A,5,125.00
2025-01-15,Widget B,3,90.00
2025-01-16,Widget A,8,200.00
2025-01-16,Widget C,2,150.00
2025-01-17,Widget A,12,300.00
2025-01-17,Widget B,7,210.00
EOF
```

### Step 1.2: Write Analysis Script

```bash
cat > analyze.py <<'EOF'
#!/usr/bin/env python3
import csv
from collections import defaultdict

# Read data
data = []
with open('sales.csv', 'r') as f:
    reader = csv.DictReader(f)
    data = list(reader)

# Calculate total revenue
total_revenue = sum(float(row['revenue']) for row in data)

# Revenue by product
product_revenue = defaultdict(float)
for row in data:
    product_revenue[row['product']] += float(row['revenue'])

# Print results
print("=== Sales Analysis Report ===")
print(f"\nTotal Revenue: ${total_revenue:.2f}")
print("\nRevenue by Product:")
for product, revenue in sorted(product_revenue.items()):
    print(f"  {product}: ${revenue:.2f}")

# Save report
with open('report.txt', 'w') as f:
    f.write(f"Total Revenue: ${total_revenue:.2f}\n")
    f.write("Revenue by Product:\n")
    for product, revenue in sorted(product_revenue.items()):
        f.write(f"  {product}: ${revenue:.2f}\n")

print("\n✓ Report saved to report.txt")
EOF
```

### Step 1.3: Create Job Manifest

```bash
cat > job.json <<'EOF'
{
  "entrypoint": "analyze.py",
  "interpreter": "python3",
  "outputs": ["report.txt"]
}
EOF
```

### Step 1.4: Submit Job

```bash
# Package files
tar czf job.tar.gz analyze.py sales.csv

# Submit to Sandrun
RESPONSE=$(curl -s -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F "manifest=$(cat job.json)")

# Extract job ID
JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
echo "Job submitted: $JOB_ID"
```

**Expected Output:**
```
Job submitted: job-a1b2c3d4e5f6
```

### Step 1.5: Monitor Job

```bash
# Check status
curl http://localhost:8443/status/$JOB_ID | jq

# Get logs
curl http://localhost:8443/logs/$JOB_ID

# Download report
curl http://localhost:8443/outputs/$JOB_ID/report.txt
```

**Expected Logs:**
```
=== Sales Analysis Report ===

Total Revenue: $1075.00

Revenue by Product:
  Widget A: $625.00
  Widget B: $300.00
  Widget C: $150.00

✓ Report saved to report.txt
```

!!! success "Checkpoint 1"
    You've successfully executed a simple Python script in Sandrun's isolated sandbox! The job ran without any local Python environment setup.

## Part 2: Multi-File Project with Dependencies

Now let's structure our analysis as a proper project with modules and external dependencies.

### Step 2.1: Create Project Structure

```bash
mkdir -p analytics-project/data
cd analytics-project
```

### Step 2.2: Write Modular Code

Create the main analysis module:

```bash
cat > analyzer.py <<'EOF'
"""Sales data analyzer with pandas"""
import pandas as pd
import matplotlib
matplotlib.use('Agg')  # Non-interactive backend
import matplotlib.pyplot as plt

def load_data(filepath):
    """Load sales data from CSV"""
    df = pd.read_csv(filepath)
    df['date'] = pd.to_datetime(df['date'])
    return df

def analyze_revenue(df):
    """Calculate revenue statistics"""
    total = df['revenue'].sum()
    by_product = df.groupby('product')['revenue'].sum()
    by_date = df.groupby('date')['revenue'].sum()

    return {
        'total': total,
        'by_product': by_product,
        'by_date': by_date
    }

def create_visualizations(df, output_dir='plots'):
    """Generate analysis plots"""
    import os
    os.makedirs(output_dir, exist_ok=True)

    # Revenue by product (bar chart)
    product_revenue = df.groupby('product')['revenue'].sum()
    plt.figure(figsize=(10, 6))
    product_revenue.plot(kind='bar', color='steelblue')
    plt.title('Revenue by Product')
    plt.xlabel('Product')
    plt.ylabel('Revenue ($)')
    plt.tight_layout()
    plt.savefig(f'{output_dir}/revenue_by_product.png')
    plt.close()

    # Daily revenue trend (line chart)
    daily_revenue = df.groupby('date')['revenue'].sum()
    plt.figure(figsize=(10, 6))
    daily_revenue.plot(kind='line', marker='o', color='green')
    plt.title('Daily Revenue Trend')
    plt.xlabel('Date')
    plt.ylabel('Revenue ($)')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(f'{output_dir}/daily_trend.png')
    plt.close()

    print("✓ Visualizations saved to plots/")

def save_report(stats, filepath='report.txt'):
    """Save analysis report"""
    with open(filepath, 'w') as f:
        f.write("=== Sales Analysis Report ===\n\n")
        f.write(f"Total Revenue: ${stats['total']:.2f}\n\n")
        f.write("Revenue by Product:\n")
        for product, revenue in stats['by_product'].items():
            f.write(f"  {product}: ${revenue:.2f}\n")
        f.write("\nDaily Revenue:\n")
        for date, revenue in stats['by_date'].items():
            f.write(f"  {date.date()}: ${revenue:.2f}\n")

    print(f"✓ Report saved to {filepath}")
EOF
```

Create the main script:

```bash
cat > main.py <<'EOF'
#!/usr/bin/env python3
"""Main entry point for sales analysis"""
import sys
import argparse
from analyzer import load_data, analyze_revenue, create_visualizations, save_report

def main():
    parser = argparse.ArgumentParser(description='Analyze sales data')
    parser.add_argument('--input', default='data/sales.csv', help='Input CSV file')
    parser.add_argument('--output', default='report.txt', help='Output report file')
    parser.add_argument('--plots', action='store_true', help='Generate plots')
    args = parser.parse_args()

    print(f"Loading data from {args.input}...")
    df = load_data(args.input)
    print(f"✓ Loaded {len(df)} records")

    print("\nAnalyzing revenue...")
    stats = analyze_revenue(df)
    print(f"✓ Total revenue: ${stats['total']:.2f}")

    if args.plots:
        print("\nGenerating visualizations...")
        create_visualizations(df)

    print(f"\nSaving report to {args.output}...")
    save_report(stats, args.output)

    print("\n🎉 Analysis complete!")

if __name__ == '__main__':
    main()
EOF
```

### Step 2.3: Add Dependencies

```bash
cat > requirements.txt <<'EOF'
pandas==2.0.3
matplotlib==3.7.2
EOF
```

### Step 2.4: Copy Data

```bash
cp ../sales.csv data/
```

### Step 2.5: Create Enhanced Manifest

```bash
cat > job.json <<'EOF'
{
  "entrypoint": "main.py",
  "interpreter": "python3",
  "args": ["--input", "data/sales.csv", "--plots"],
  "requirements": "requirements.txt",
  "outputs": ["report.txt", "plots/*.png"],
  "timeout": 300,
  "memory_mb": 512
}
EOF
```

### Step 2.6: Submit Project

```bash
# Package entire project
tar czf ../analytics-project.tar.gz .

# Submit
cd ..
RESPONSE=$(curl -s -X POST http://localhost:8443/submit \
  -F "files=@analytics-project.tar.gz" \
  -F "manifest=$(cat analytics-project/job.json)")

JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
echo "Job submitted: $JOB_ID"

# Wait for completion (this may take 30-60 seconds for pip install)
echo "Installing dependencies..."
sleep 45

# Check status
curl http://localhost:8443/status/$JOB_ID | jq '.status'
```

### Step 2.7: Download Results

```bash
# List available outputs
curl http://localhost:8443/outputs/$JOB_ID | jq

# Download all outputs as tarball
curl http://localhost:8443/download/$JOB_ID -o results.tar.gz
tar xzf results.tar.gz

# View files
ls -R plots/
cat report.txt
```

!!! tip "Dependency Installation"
    Sandrun installs dependencies in the sandbox on first use. This adds startup time but ensures complete isolation. For faster execution, use pre-built environments (next section).

!!! success "Checkpoint 2"
    You've built a modular project with external dependencies! Notice how Sandrun automatically installed pandas and matplotlib without any local setup.

## Part 3: Using Pre-Built Environments

Installing dependencies on every job is slow. Let's use Sandrun's pre-built environments for faster execution.

### Step 3.1: List Available Environments

```bash
curl http://localhost:8443/environments | jq
```

**Response:**
```json
{
  "environments": [
    {
      "name": "ml-basic",
      "packages": ["numpy", "pandas", "scikit-learn", "matplotlib"],
      "python_version": "3.10.12"
    },
    {
      "name": "data-science",
      "packages": ["numpy", "pandas", "matplotlib", "seaborn", "jupyter"],
      "python_version": "3.10.12"
    }
  ]
}
```

### Step 3.2: Update Manifest

```bash
cd analytics-project

# Use ml-basic environment (has pandas and matplotlib pre-installed)
cat > job.json <<'EOF'
{
  "entrypoint": "main.py",
  "interpreter": "python3",
  "environment": "ml-basic",
  "args": ["--input", "data/sales.csv", "--plots"],
  "outputs": ["report.txt", "plots/*.png"],
  "timeout": 300,
  "memory_mb": 512
}
EOF
```

!!! note "Environment Benefits"
    By specifying `"environment": "ml-basic"`, we skip pip installation entirely. The job starts instantly because pandas and matplotlib are already available.

### Step 3.3: Submit with Environment

```bash
tar czf ../analytics-env.tar.gz .
cd ..

RESPONSE=$(curl -s -X POST http://localhost:8443/submit \
  -F "files=@analytics-env.tar.gz" \
  -F "manifest=$(cat analytics-project/job.json)")

JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
echo "Job submitted: $JOB_ID"

# Check execution time (should be much faster)
sleep 5
curl http://localhost:8443/status/$JOB_ID | jq '.execution_metadata'
```

**Expected Response:**
```json
{
  "cpu_seconds": 0.8,
  "memory_peak_bytes": 45678912,
  "exit_code": 0,
  "runtime_seconds": 2.3
}
```

!!! success "Checkpoint 3"
    Job execution is now ~20x faster! From 45 seconds (with pip install) to 2-3 seconds (pre-built environment). This makes Sandrun practical for high-volume batch processing.

## Part 4: Processing Multiple Files

Real-world pipelines process multiple datasets. Let's extend our analysis to handle multiple files.

### Step 4.1: Generate Multiple Datasets

```bash
# Create datasets for different regions
mkdir -p multi-region/data

cat > multi-region/data/sales_north.csv <<'EOF'
date,product,quantity,revenue
2025-01-15,Widget A,10,250.00
2025-01-16,Widget B,5,150.00
2025-01-17,Widget A,15,375.00
EOF

cat > multi-region/data/sales_south.csv <<'EOF'
date,product,quantity,revenue
2025-01-15,Widget C,8,240.00
2025-01-16,Widget A,6,150.00
2025-01-17,Widget B,9,270.00
EOF

cat > multi-region/data/sales_east.csv <<'EOF'
date,product,quantity,revenue
2025-01-15,Widget A,12,300.00
2025-01-16,Widget C,4,120.00
2025-01-17,Widget A,8,200.00
EOF
```

### Step 4.2: Create Batch Processor

```bash
cd multi-region

cat > batch_analyze.py <<'EOF'
#!/usr/bin/env python3
"""Batch analysis across multiple datasets"""
import os
import glob
import pandas as pd
import json

def process_region(filepath):
    """Process a single region's data"""
    region = os.path.basename(filepath).replace('sales_', '').replace('.csv', '')
    df = pd.read_csv(filepath)

    total_revenue = df['revenue'].sum()
    total_quantity = df['quantity'].sum()
    unique_products = df['product'].nunique()

    return {
        'region': region,
        'total_revenue': float(total_revenue),
        'total_quantity': int(total_quantity),
        'unique_products': unique_products,
        'records': len(df)
    }

def main():
    print("Finding data files...")
    files = glob.glob('data/sales_*.csv')
    print(f"✓ Found {len(files)} region files")

    # Process each region
    results = []
    for filepath in sorted(files):
        print(f"\nProcessing {os.path.basename(filepath)}...")
        result = process_region(filepath)
        results.append(result)
        print(f"  Revenue: ${result['total_revenue']:.2f}")
        print(f"  Products: {result['unique_products']}")

    # Calculate aggregates
    total_revenue = sum(r['total_revenue'] for r in results)
    total_quantity = sum(r['total_quantity'] for r in results)

    # Create summary
    summary = {
        'regions': results,
        'totals': {
            'revenue': total_revenue,
            'quantity': total_quantity,
            'regions_processed': len(results)
        }
    }

    # Save JSON report
    with open('summary.json', 'w') as f:
        json.dump(summary, f, indent=2)

    # Save text report
    with open('summary.txt', 'w') as f:
        f.write("=== Multi-Region Sales Summary ===\n\n")
        f.write(f"Regions Processed: {len(results)}\n")
        f.write(f"Total Revenue: ${total_revenue:.2f}\n")
        f.write(f"Total Quantity: {total_quantity}\n\n")
        f.write("By Region:\n")
        for r in results:
            f.write(f"  {r['region'].upper()}: ${r['total_revenue']:.2f} ({r['total_quantity']} units)\n")

    print(f"\n✓ Summary saved")
    print(f"  Total revenue across all regions: ${total_revenue:.2f}")
    print("\n🎉 Batch processing complete!")

if __name__ == '__main__':
    main()
EOF

cat > job.json <<'EOF'
{
  "entrypoint": "batch_analyze.py",
  "interpreter": "python3",
  "environment": "ml-basic",
  "outputs": ["summary.json", "summary.txt"],
  "timeout": 300,
  "memory_mb": 512
}
EOF
```

### Step 4.3: Submit Batch Job

```bash
tar czf ../multi-region.tar.gz .
cd ..

RESPONSE=$(curl -s -X POST http://localhost:8443/submit \
  -F "files=@multi-region.tar.gz" \
  -F "manifest=$(cat multi-region/job.json)")

JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
echo "Job submitted: $JOB_ID"

# Wait and download
sleep 5
curl http://localhost:8443/outputs/$JOB_ID/summary.json | jq
```

**Expected Output:**
```json
{
  "regions": [
    {
      "region": "east",
      "total_revenue": 620.0,
      "total_quantity": 24,
      "unique_products": 2,
      "records": 3
    },
    {
      "region": "north",
      "total_revenue": 775.0,
      "total_quantity": 30,
      "unique_products": 2,
      "records": 3
    },
    {
      "region": "south",
      "total_revenue": 660.0,
      "total_quantity": 23,
      "unique_products": 3,
      "records": 3
    }
  ],
  "totals": {
    "revenue": 2055.0,
    "quantity": 77,
    "regions_processed": 3
  }
}
```

!!! success "Checkpoint 4"
    You've processed multiple datasets in a single job! However, all processing still happens sequentially on one worker. Let's distribute the work next.

## Part 5: Distributed Processing with Trusted Pool

Now let's scale horizontally by distributing jobs across multiple workers using Sandrun's trusted pool coordinator.

### Step 5.1: Generate Worker Identities

First, we need to create worker identities on each machine:

```bash
# On Worker 1
sudo ./build/sandrun --generate-key /etc/sandrun/worker1.pem

# Output:
# ✅ Saved worker key to: /etc/sandrun/worker1.pem
#    Worker ID: J7X8K3mNqR4tUvWxYz9A2bCdEfGhIjKlMnOpQrStUvWx==

# On Worker 2
sudo ./build/sandrun --generate-key /etc/sandrun/worker2.pem

# On Worker 3
sudo ./build/sandrun --generate-key /etc/sandrun/worker3.pem
```

!!! note "Worker Identity"
    Each worker gets an Ed25519 key pair. The public key (Worker ID) is used to identify and trust the worker. The private key signs all job results for verification.

### Step 5.2: Configure Pool

```bash
cd /path/to/sandrun/integrations/trusted-pool

# Create worker configuration
cat > workers.json <<'EOF'
[
  {
    "worker_id": "J7X8K3mNqR4tUvWxYz9A2bCdEfGhIjKlMnOpQrStUvWx==",
    "endpoint": "http://192.168.1.101:8443",
    "max_concurrent_jobs": 4
  },
  {
    "worker_id": "K8Y9L4nOqS5uVwXyZa0B3cDeEfGhIjKlMnOpQrStUvWx==",
    "endpoint": "http://192.168.1.102:8443",
    "max_concurrent_jobs": 4
  },
  {
    "worker_id": "L9Z0M5oPrT6vWxYza1C4dEfGhIjKlMnOpQrStUvWxYz==",
    "endpoint": "http://192.168.1.103:8443",
    "max_concurrent_jobs": 4
  }
]
EOF
```

!!! tip "Local Testing"
    Don't have multiple machines? Run workers on different ports locally:
    ```bash
    # Terminal 1
    sudo ./build/sandrun --port 8443 --worker-key worker1.pem

    # Terminal 2
    sudo ./build/sandrun --port 8444 --worker-key worker2.pem

    # Terminal 3
    sudo ./build/sandrun --port 8445 --worker-key worker3.pem
    ```

    Then use `http://localhost:8443`, `http://localhost:8444`, `http://localhost:8445` in workers.json.

### Step 5.3: Start Workers

```bash
# On each worker machine (or in separate terminals if local)
sudo ./build/sandrun --port 8443 --worker-key /etc/sandrun/worker1.pem
```

### Step 5.4: Start Pool Coordinator

```bash
# Install dependencies
pip install -r requirements.txt

# Start coordinator
python coordinator.py --port 9000 --workers workers.json
```

**Expected Output:**
```
INFO:__main__:Added trusted worker: J7X8K3mNqR4t... at http://192.168.1.101:8443
INFO:__main__:Added trusted worker: K8Y9L4nOqS5u... at http://192.168.1.102:8443
INFO:__main__:Added trusted worker: L9Z0M5oPrT6v... at http://192.168.1.103:8443
INFO:__main__:Starting health checker (interval: 30s)
INFO:__main__:Pool coordinator listening on port 9000
```

### Step 5.5: Verify Pool Status

```bash
curl http://localhost:9000/pool | jq
```

**Response:**
```json
{
  "total_workers": 3,
  "healthy_workers": 3,
  "total_jobs": 0,
  "queued_jobs": 0,
  "workers": [
    {
      "worker_id": "J7X8K3mNqR4t...",
      "endpoint": "http://192.168.1.101:8443",
      "is_healthy": true,
      "active_jobs": 0,
      "max_concurrent_jobs": 4
    }
  ]
}
```

### Step 5.6: Submit Jobs to Pool

Now submit multiple jobs - they'll be automatically distributed:

```bash
# Submit 9 jobs (3 per worker)
for i in {1..9}; do
  echo "Submitting job $i..."
  RESPONSE=$(curl -s -X POST http://localhost:9000/submit \
    -F "files=@analytics-project.tar.gz" \
    -F "manifest=$(cat analytics-project/job.json)")

  JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
  echo "  Job $i: $JOB_ID"

  # Save job IDs
  echo $JOB_ID >> pool_jobs.txt
done

echo "✓ Submitted 9 jobs to pool"
```

### Step 5.7: Monitor Distribution

```bash
# Check pool status
curl http://localhost:9000/pool | jq '.workers[] | {worker_id: .worker_id[0:16], active_jobs: .active_jobs}'
```

**Response (jobs distributed across workers):**
```json
{"worker_id": "J7X8K3mNqR4t", "active_jobs": 3}
{"worker_id": "K8Y9L4nOqS5u", "active_jobs": 3}
{"worker_id": "L9Z0M5oPrT6v", "active_jobs": 3}
```

!!! success "Checkpoint 5"
    Jobs are now distributed across 3 workers! Each worker processes 3 jobs concurrently, giving you 9x parallelism. The pool coordinator automatically load balances based on worker availability.

## Part 6: Real-Time Monitoring with WebSocket

For long-running jobs, you want to see progress in real-time. Let's use WebSocket streaming.

### Step 6.1: Create Long-Running Job

```bash
mkdir -p streaming-demo

cat > streaming-demo/process.py <<'EOF'
#!/usr/bin/env python3
"""Long-running job with progress updates"""
import time
import sys

def process_batch(batch_num, items):
    """Simulate processing a batch"""
    print(f"[Batch {batch_num}] Processing {items} items...")
    sys.stdout.flush()

    for i in range(items):
        time.sleep(0.5)  # Simulate work
        if (i + 1) % 5 == 0:
            print(f"  Progress: {i+1}/{items} items processed")
            sys.stdout.flush()

    print(f"[Batch {batch_num}] ✓ Complete ({items} items)")
    sys.stdout.flush()

def main():
    print("=== Starting Data Processing Pipeline ===\n")
    sys.stdout.flush()

    batches = [
        (1, 10),
        (2, 15),
        (3, 8),
    ]

    for batch_num, items in batches:
        process_batch(batch_num, items)
        print()
        sys.stdout.flush()

    print("🎉 All batches processed successfully!")
    sys.stdout.flush()

if __name__ == '__main__':
    main()
EOF

cat > streaming-demo/job.json <<'EOF'
{
  "entrypoint": "process.py",
  "interpreter": "python3",
  "timeout": 300
}
EOF
```

### Step 6.2: Create WebSocket Client

```bash
cat > stream_logs.py <<'EOF'
#!/usr/bin/env python3
"""WebSocket client to stream job logs in real-time"""
import sys
import asyncio
import websockets

async def stream_logs(job_id):
    uri = f"ws://localhost:8443/logs/{job_id}/stream"

    print(f"Connecting to {uri}...")
    print("-" * 60)

    try:
        async with websockets.connect(uri) as ws:
            async for message in ws:
                print(message, end='', flush=True)
    except websockets.exceptions.ConnectionClosed:
        print("\n" + "-" * 60)
        print("✓ Stream closed (job completed)")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python stream_logs.py <job_id>")
        sys.exit(1)

    job_id = sys.argv[1]
    asyncio.run(stream_logs(job_id))
EOF

chmod +x stream_logs.py
```

### Step 6.3: Submit and Stream

```bash
# Submit job
cd streaming-demo
tar czf ../streaming.tar.gz .
cd ..

RESPONSE=$(curl -s -X POST http://localhost:8443/submit \
  -F "files=@streaming.tar.gz" \
  -F "manifest=$(cat streaming-demo/job.json)")

JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
echo "Job submitted: $JOB_ID"

# Stream logs in real-time
python stream_logs.py $JOB_ID
```

**Live Output:**
```
Connecting to ws://localhost:8443/logs/job-abc123/stream...
------------------------------------------------------------
=== Starting Data Processing Pipeline ===

[Batch 1] Processing 10 items...
  Progress: 5/10 items processed
  Progress: 10/10 items processed
[Batch 1] ✓ Complete (10 items)

[Batch 2] Processing 15 items...
  Progress: 5/15 items processed
  Progress: 10/15 items processed
  Progress: 15/15 items processed
[Batch 2] ✓ Complete (15 items)

[Batch 3] Processing 8 items...
  Progress: 5/8 items processed
  Progress: 8/8 items processed
[Batch 3] ✓ Complete (8 items)

🎉 All batches processed successfully!
------------------------------------------------------------
✓ Stream closed (job completed)
```

!!! tip "Pool Streaming"
    WebSocket streaming also works through the pool coordinator:
    ```bash
    # Submit to pool
    RESPONSE=$(curl -s -X POST http://localhost:9000/submit ...)

    # Stream from pool (it proxies to the worker)
    python stream_logs.py $POOL_JOB_ID
    ```

!!! success "Checkpoint 6"
    You can now monitor long-running jobs in real-time! This is essential for debugging and progress tracking in production pipelines.

## Part 7: Cryptographic Result Verification

When running untrusted workers or processing sensitive data, you need to verify results weren't tampered with.

### Step 7.1: Understand Worker Signatures

Every job executed by a worker with `--worker-key` includes a cryptographic signature:

```bash
# Check job status
curl http://localhost:8443/status/$JOB_ID | jq '.worker_metadata'
```

**Response:**
```json
{
  "worker_id": "J7X8K3mNqR4tUvWxYz9A2bCdEfGhIjKlMnOpQrStUvWx==",
  "signature": "k8mN9qR2tVwX...base64-signature...",
  "signature_algorithm": "Ed25519",
  "signed_data": {
    "job_id": "job-abc123",
    "exit_code": 0,
    "cpu_seconds": 1.23,
    "memory_peak_bytes": 52428800,
    "output_hash": "sha256:a3f2b8d1c7e5..."
  }
}
```

### Step 7.2: Extract Worker Public Key

```bash
# Get worker public key from worker endpoint
curl http://localhost:8443/health | jq -r '.worker_id' > worker_pubkey.txt

cat worker_pubkey.txt
# J7X8K3mNqR4tUvWxYz9A2bCdEfGhIjKlMnOpQrStUvWx==
```

### Step 7.3: Create Verification Script

```bash
cat > verify_result.py <<'EOF'
#!/usr/bin/env python3
"""Verify job result signature"""
import sys
import json
import base64
import hashlib
from cryptography.hazmat.primitives.asymmetric import ed25519
from cryptography.hazmat.primitives import serialization

def load_worker_pubkey(pubkey_b64):
    """Load Ed25519 public key from base64"""
    pubkey_bytes = base64.b64decode(pubkey_b64)
    return ed25519.Ed25519PublicKey.from_public_bytes(pubkey_bytes)

def verify_signature(pubkey, signed_data, signature_b64):
    """Verify Ed25519 signature"""
    signature = base64.b64decode(signature_b64)
    message = json.dumps(signed_data, sort_keys=True).encode()

    try:
        pubkey.verify(signature, message)
        return True
    except Exception:
        return False

def main(job_id, worker_pubkey_b64):
    # Fetch job status
    import subprocess
    result = subprocess.run(
        ['curl', '-s', f'http://localhost:8443/status/{job_id}'],
        capture_output=True,
        text=True
    )

    status = json.loads(result.stdout)

    if 'worker_metadata' not in status:
        print("❌ No worker metadata (job not executed by signed worker)")
        sys.exit(1)

    metadata = status['worker_metadata']

    # Verify worker identity
    if metadata['worker_id'] != worker_pubkey_b64:
        print("❌ Worker ID mismatch!")
        print(f"  Expected: {worker_pubkey_b64}")
        print(f"  Got: {metadata['worker_id']}")
        sys.exit(1)

    print(f"✓ Worker identity verified: {metadata['worker_id'][:16]}...")

    # Verify signature
    pubkey = load_worker_pubkey(worker_pubkey_b64)
    signed_data = metadata['signed_data']
    signature = metadata['signature']

    if verify_signature(pubkey, signed_data, signature):
        print("✓ Signature valid (result authenticated)")
        print(f"\nSigned Data:")
        print(f"  Job ID: {signed_data['job_id']}")
        print(f"  Exit Code: {signed_data['exit_code']}")
        print(f"  CPU Time: {signed_data['cpu_seconds']}s")
        print(f"  Memory Peak: {signed_data['memory_peak_bytes'] / 1024 / 1024:.1f} MB")
        print(f"  Output Hash: {signed_data['output_hash']}")
        print("\n🎉 Result verified successfully!")
    else:
        print("❌ Invalid signature (result may be tampered)")
        sys.exit(1)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python verify_result.py <job_id> <worker_pubkey_base64>")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
EOF

# Install cryptography library
pip install cryptography
```

### Step 7.4: Verify a Job Result

```bash
# Get worker public key
WORKER_PUBKEY=$(curl -s http://localhost:8443/health | jq -r '.worker_id')

# Verify job result
python verify_result.py $JOB_ID $WORKER_PUBKEY
```

**Output:**
```
✓ Worker identity verified: J7X8K3mNqR4t...
✓ Signature valid (result authenticated)

Signed Data:
  Job ID: job-abc123
  Exit Code: 0
  CPU Time: 1.23s
  Memory Peak: 50.0 MB
  Output Hash: sha256:a3f2b8d1c7e5...

🎉 Result verified successfully!
```

### Step 7.5: Verify Pool Job

For pool jobs, the coordinator tracks which worker executed the job:

```bash
# Get pool job status
curl http://localhost:9000/status/$POOL_JOB_ID | jq '{pool_job_id: .job_id, worker_id: .worker_id, remote_job_id: .worker_status.job_id}'

# Get worker public key from pool
WORKER_ID=$(curl -s http://localhost:9000/status/$POOL_JOB_ID | jq -r '.worker_id')

# Fetch actual job status from worker
curl http://localhost:8443/status/$REMOTE_JOB_ID | jq '.worker_metadata'

# Verify
python verify_result.py $REMOTE_JOB_ID $WORKER_ID
```

!!! warning "Trust Model"
    The trusted pool coordinator doesn't verify signatures itself - it trusts allowlisted workers. You should verify signatures if:

    - Running a public pool open to anyone
    - Processing sensitive data requiring audit trails
    - Implementing compliance requirements

    For private clusters with trusted workers, signature verification is optional.

!!! success "Checkpoint 7"
    You can now cryptographically verify that results came from specific workers and weren't tampered with. This enables trustless distributed computing!

## Part 8: Complete Pipeline Example

Let's tie everything together: distributed processing with monitoring and verification.

### Step 8.1: Create Distributed Data Pipeline

```bash
mkdir -p complete-pipeline

cat > complete-pipeline/pipeline.py <<'EOF'
#!/usr/bin/env python3
"""Complete data pipeline with progress tracking"""
import sys
import json
import pandas as pd
import numpy as np
from datetime import datetime

def log(message):
    """Log with timestamp and flush"""
    timestamp = datetime.now().strftime('%H:%M:%S')
    print(f"[{timestamp}] {message}", flush=True)

def process_dataset(filepath):
    """Process a single dataset"""
    log(f"Loading {filepath}...")
    df = pd.read_csv(filepath)

    log(f"  Records: {len(df)}")

    # Data validation
    log("  Validating data...")
    assert not df.isnull().any().any(), "Found null values"
    assert all(df['revenue'] >= 0), "Found negative revenue"

    # Calculate metrics
    log("  Calculating metrics...")
    metrics = {
        'total_revenue': float(df['revenue'].sum()),
        'avg_revenue': float(df['revenue'].mean()),
        'total_quantity': int(df['quantity'].sum()),
        'unique_products': int(df['product'].nunique()),
        'date_range': {
            'start': df['date'].min(),
            'end': df['date'].max()
        }
    }

    log(f"  ✓ Revenue: ${metrics['total_revenue']:.2f}")
    return metrics

def main():
    log("=== Data Pipeline Starting ===")

    # Discover input files
    import glob
    files = sorted(glob.glob('data/*.csv'))
    log(f"Found {len(files)} data files")

    # Process each file
    results = {}
    for filepath in files:
        dataset_name = filepath.split('/')[-1].replace('.csv', '')
        log(f"\n--- Processing {dataset_name} ---")

        try:
            metrics = process_dataset(filepath)
            results[dataset_name] = {
                'status': 'success',
                'metrics': metrics
            }
        except Exception as e:
            log(f"  ❌ Error: {e}")
            results[dataset_name] = {
                'status': 'failed',
                'error': str(e)
            }

    # Save results
    log("\n--- Saving Results ---")

    output = {
        'timestamp': datetime.now().isoformat(),
        'datasets_processed': len(results),
        'datasets': results,
        'summary': {
            'successful': sum(1 for r in results.values() if r['status'] == 'success'),
            'failed': sum(1 for r in results.values() if r['status'] == 'failed'),
            'total_revenue': sum(
                r['metrics']['total_revenue']
                for r in results.values()
                if r['status'] == 'success'
            )
        }
    }

    with open('pipeline_results.json', 'w') as f:
        json.dump(output, f, indent=2)

    log(f"✓ Results saved to pipeline_results.json")
    log(f"\n=== Pipeline Complete ===")
    log(f"  Processed: {output['summary']['successful']}/{len(results)} datasets")
    log(f"  Total Revenue: ${output['summary']['total_revenue']:.2f}")

if __name__ == '__main__':
    main()
EOF
```

### Step 8.2: Create Submission Script

```bash
cat > submit_pipeline.sh <<'EOF'
#!/bin/bash
set -e

echo "=== Distributed Pipeline Submission ==="
echo

# Configuration
POOL_URL="http://localhost:9000"
PROJECT_DIR="complete-pipeline"

# Create manifest
cat > $PROJECT_DIR/job.json <<MANIFEST
{
  "entrypoint": "pipeline.py",
  "interpreter": "python3",
  "environment": "ml-basic",
  "outputs": ["pipeline_results.json"],
  "timeout": 600,
  "memory_mb": 1024
}
MANIFEST

# Package project
echo "Packaging project..."
tar czf pipeline.tar.gz -C $PROJECT_DIR .

# Submit multiple jobs
JOB_IDS=()
NUM_JOBS=5

echo "Submitting $NUM_JOBS jobs to pool..."
for i in $(seq 1 $NUM_JOBS); do
    RESPONSE=$(curl -s -X POST $POOL_URL/submit \
        -F "files=@pipeline.tar.gz" \
        -F "manifest=$(cat $PROJECT_DIR/job.json)")

    JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
    JOB_IDS+=($JOB_ID)
    echo "  Job $i: $JOB_ID"
done

echo
echo "✓ Submitted $NUM_JOBS jobs"
echo

# Monitor pool status
echo "Monitoring pool distribution..."
sleep 2
curl -s $POOL_URL/pool | jq '{
    total_workers: .total_workers,
    healthy_workers: .healthy_workers,
    queued_jobs: .queued_jobs,
    worker_loads: [.workers[] | {
        worker: .worker_id[0:16],
        active: .active_jobs,
        max: .max_concurrent_jobs
    }]
}'

echo
echo "Job IDs:"
printf '%s\n' "${JOB_IDS[@]}"
echo
echo "To stream a job's logs:"
echo "  python stream_logs.py <job_id>"
echo
echo "To verify a job's results:"
echo "  python verify_result.py <job_id> <worker_pubkey>"
EOF

chmod +x submit_pipeline.sh
```

### Step 8.3: Copy Sample Data

```bash
cp -r multi-region/data complete-pipeline/
```

### Step 8.4: Run Complete Pipeline

```bash
# Submit jobs
./submit_pipeline.sh
```

**Output:**
```
=== Distributed Pipeline Submission ===

Packaging project...
Submitting 5 jobs to pool...
  Job 1: pool-a1b2c3d4
  Job 2: pool-e5f6g7h8
  Job 3: pool-i9j0k1l2
  Job 4: pool-m3n4o5p6
  Job 5: pool-q7r8s9t0

✓ Submitted 5 jobs

Monitoring pool distribution...
{
  "total_workers": 3,
  "healthy_workers": 3,
  "queued_jobs": 2,
  "worker_loads": [
    {"worker": "J7X8K3mNqR4t", "active": 2, "max": 4},
    {"worker": "K8Y9L4nOqS5u", "active": 2, "max": 4},
    {"worker": "L9Z0M5oPrT6v", "active": 1, "max": 4}
  ]
}

Job IDs:
pool-a1b2c3d4
pool-e5f6g7h8
pool-i9j0k1l2
pool-m3n4o5p6
pool-q7r8s9t0

To stream a job's logs:
  python stream_logs.py <job_id>

To verify a job's results:
  python verify_result.py <job_id> <worker_pubkey>
```

### Step 8.5: Monitor Job Progress

```bash
# Stream first job
python stream_logs.py pool-a1b2c3d4
```

### Step 8.6: Collect and Verify Results

```bash
cat > collect_results.sh <<'EOF'
#!/bin/bash

POOL_URL="http://localhost:9000"
JOB_IDS_FILE="pipeline_jobs.txt"

echo "=== Collecting Pipeline Results ==="
echo

mkdir -p results

while IFS= read -r JOB_ID; do
    echo "Checking $JOB_ID..."

    # Get status
    STATUS=$(curl -s $POOL_URL/status/$JOB_ID | jq -r '.pool_status')

    if [ "$STATUS" = "completed" ]; then
        echo "  ✓ Completed"

        # Download results
        curl -s $POOL_URL/outputs/$JOB_ID/pipeline_results.json \
            -o results/${JOB_ID}_results.json

        # Show summary
        cat results/${JOB_ID}_results.json | jq '.summary'

        # Verify signature (if available)
        WORKER_ID=$(curl -s $POOL_URL/status/$JOB_ID | jq -r '.worker_id')
        REMOTE_JOB=$(curl -s $POOL_URL/status/$JOB_ID | jq -r '.worker_status.job_id')

        echo "  Worker: ${WORKER_ID:0:16}..."
        echo
    else
        echo "  Status: $STATUS"
        echo
    fi
done < "$JOB_IDS_FILE"

echo "✓ Results collected in results/"
EOF

chmod +x collect_results.sh

# Save job IDs for collection
./submit_pipeline.sh | grep "pool-" > pipeline_jobs.txt

# Wait for completion
sleep 10

# Collect results
./collect_results.sh
```

!!! success "Checkpoint 8 - Complete!"
    You've built a production-ready distributed data processing pipeline with:

    - ✅ Distributed execution across multiple workers
    - ✅ Real-time progress monitoring via WebSocket
    - ✅ Cryptographic result verification
    - ✅ Automated job submission and collection
    - ✅ Error handling and status tracking

## Summary and Next Steps

Congratulations! You've learned to build distributed data pipelines with Sandrun.

### What You've Accomplished

1. **Basic Execution** - Ran Python scripts in isolated sandboxes
2. **Project Structure** - Organized multi-file projects with dependencies
3. **Performance Optimization** - Used pre-built environments for instant execution
4. **Batch Processing** - Processed multiple datasets in parallel
5. **Distributed Computing** - Scaled across worker pools with load balancing
6. **Real-Time Monitoring** - Streamed logs via WebSocket
7. **Security** - Verified results with cryptographic signatures
8. **Production Pipeline** - Automated end-to-end workflows

### Key Takeaways

!!! tip "Best Practices"
    - **Use pre-built environments** for faster execution (20x speedup)
    - **Structure projects** with manifest files for reproducibility
    - **Monitor with WebSocket** for long-running jobs
    - **Verify signatures** for untrusted workers or compliance needs
    - **Distribute work** across pools for horizontal scaling
    - **Handle errors gracefully** with status checking and retries

### Performance Benchmarks

From this tutorial:

| Metric | Single Worker | 3-Worker Pool |
|--------|--------------|---------------|
| **Job Throughput** | 1 job/3s | 3 jobs/3s |
| **Parallel Capacity** | 2 concurrent | 12 concurrent |
| **Total Throughput** | 600 jobs/hour | 1800 jobs/hour |

### Next Steps

Ready to go deeper? Check out:

- **[API Reference](../api-reference.md)** - Complete endpoint documentation
- **[Job Manifest](../job-manifest.md)** - Advanced configuration options
- **[MCP Integration](../integrations/mcp-server.md)** - Give Claude AI code execution
- **[Security Model](../security.md)** - Understand isolation guarantees
- **[Troubleshooting](../troubleshooting.md)** - Debug common issues

### Real-World Applications

Apply what you've learned to:

- **Data Science** - Process large datasets in parallel
- **CI/CD** - Run test suites in isolated environments
- **ML Training** - Distribute training jobs with GPU workers
- **LLM Integration** - Give AI assistants safe code execution
- **Privacy Computing** - Process sensitive data without persistence

### Community

Questions or want to share your pipeline?

- **GitHub**: [github.com/yourusername/sandrun](https://github.com/yourusername/sandrun)
- **Discussions**: Share use cases and get help
- **Issues**: Report bugs or request features

---

**You're now ready to build production data pipelines with Sandrun!** 🎉
