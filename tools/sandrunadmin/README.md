# Sandrun Admin Console

Shell-like interface for managing sandrun instances.

## Features

- **Virtual Filesystem** - Navigate jobs, config, stats like a filesystem
- **Familiar Commands** - `cd`, `ls`, `cat`, `tail` work as expected
- **Job Management** - Submit, monitor, and download job outputs
- **Remote Access** - Manage sandrun over HTTP
- **System Stats** - Check quotas, environments, health

## Installation

```bash
cd tools/sandrunadmin
pip install -r requirements.txt
chmod +x sandrunadmin
```

## Usage

```bash
# Connect to local sandrun
./sandrunadmin

# Connect to remote sandrun
./sandrunadmin http://remote-server:8443
```

## Virtual Filesystem

```
/
├── jobs/               All jobs
│   ├── job-abc123/    Job directory
│   │   ├── manifest.json
│   │   ├── status.json
│   │   ├── logs/
│   │   │   ├── stdout.log
│   │   │   └── stderr.log
│   │   └── outputs/
│   │       └── result.txt
├── config/            Configuration
│   ├── rate_limits.json
│   └── environments.json
├── stats/             Statistics
│   ├── quotas.json
│   └── system.json
└── pool/              Pool coordinator (if available)
    ├── workers/
    └── jobs/
```

## Commands

### Navigation

```bash
cd <path>       # Change directory
ls [path]       # List directory contents
pwd             # Print working directory
```

### File Operations

```bash
cat <file>      # Display file contents
tail <file>     # Stream file (logs)
```

### Job Management

```bash
status <job_id>             # Get job status
logs <job_id>               # Get job logs
submit <tarball> [manifest] # Submit new job
download <job_id> <file>    # Download output file
```

### System

```bash
stats          # Show system statistics
environments   # List available environments
health         # Check server health
clear          # Clear screen
exit           # Exit console
```

## Examples

### Navigate to a Job

```bash
sandrun> cd /jobs/job-abc123
sandrun:/jobs/job-abc123> ls
manifest.json  logs/  outputs/  status.json

sandrun:/jobs/job-abc123> cat status.json
{
  "job_id": "job-abc123",
  "status": "completed",
  ...
}

sandrun:/jobs/job-abc123> tail logs/stdout.log
Hello from Sandrun!
Processing complete.
```

### Submit a Job

```bash
sandrun> submit /tmp/job.tar.gz '{"entrypoint":"main.py","interpreter":"python3"}'
✅ Job submitted: job-def456
   Status: queued

sandrun> status job-def456
{
  "job_id": "job-def456",
  "status": "running",
  ...
}
```

### Check System Stats

```bash
sandrun> stats

═══ Your Quota ═══
  Used:      2.50 / 10.00 CPU-sec
  Available: 7.50 CPU-sec
  Active:    1 jobs
  Can submit: ✅ Yes

═══ System Status ═══
  Queue length: 3
  Active jobs:  5
```

### List Environments

```bash
sandrun> environments

═══ Available Environments ═══
  • default
  • ml-basic
  • vision
  • nlp
  • data-science
  • scientific

═══ Environment Stats ═══
  Templates: 6
  Cached:    3
  Uses:      42
  Disk:      512 MB
```

### Download Output

```bash
sandrun> download job-abc123 result.txt
✅ Downloaded result.txt → result.txt

sandrun> download job-abc123 plots/figure1.png figure1.png
✅ Downloaded plots/figure1.png → figure1.png
```

## Remote Management

```bash
# Connect to remote server
./sandrunadmin http://production:8443

sandrun> health
✅ Server is healthy
   Worker ID: deEZc/XA7ZKG/OTKqtxoTyWlsoCgNy+V...

sandrun> stats
...
```

## Tab Completion

The console supports tab completion for commands:

```bash
sandrun> st<TAB>
stats  status

sandrun> cd /j<TAB>
cd /jobs/
```

## Scripting

You can pipe commands:

```bash
# Check multiple job statuses
for job in job-abc123 job-def456; do
  echo "status $job" | ./sandrunadmin
done

# Automated downloads
echo "download job-abc123 result.txt" | ./sandrunadmin http://server:8443
```

## Future Enhancements

Planned features:

- **Unix Socket Support** - Direct access to local sandrun internals
- **Real-time Monitoring** - `top`-like interface for jobs
- **Job Cancellation** - `kill <job_id>` command
- **Config Management** - `config set` commands
- **Pool Management** - Worker drain, status commands
- **Batch Operations** - Process multiple jobs at once

## Troubleshooting

### Connection Failed

```bash
❌ Failed to connect to http://localhost:8443: Connection refused
```

**Solution:** Ensure sandrun is running:

```bash
sudo ./build/sandrun --port 8443
```

### Command Not Found

If commands don't work, ensure you're in the right directory:

```bash
sandrun> pwd
/

sandrun> cd /jobs
sandrun:/jobs> status job-abc123
```

## Architecture

The admin console is a thin client that:

1. **HTTP API** - All operations go through REST endpoints
2. **Virtual FS** - Filesystem abstraction over API
3. **No Direct Access** - Safe, remote-capable
4. **Stateless** - Each command is independent

This design allows:
- ✅ Remote management
- ✅ Multiple instances
- ✅ No special privileges
- ✅ Scriptable automation

## See Also

- [API Reference](../../docs/api-reference.md)
- [Getting Started](../../docs/getting-started.md)
- [Trusted Pool](../../integrations/trusted-pool/)
