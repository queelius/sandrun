# Frequently Asked Questions

Common questions about Sandrun and their answers.

## General Questions

### What is Sandrun?

Sandrun is an anonymous, ephemeral, sandboxed code execution service. It allows you to run untrusted code in isolated Linux namespaces without creating accounts or leaving persistent data.

### Why would I use Sandrun?

- **Privacy**: No signup, no tracking, all data auto-deletes
- **Security**: Hardware-enforced isolation prevents malicious code from escaping
- **Simplicity**: Just HTTP POST your code and get results
- **Versatility**: Python, JavaScript, Bash, R, Ruby, and more
- **Scalability**: Distribute jobs across multiple workers with pool coordinator

### Is Sandrun free?

Yes! Sandrun is open source under the MIT license. You can run your own instance for free, or use a public instance (subject to rate limits).

### How does Sandrun compare to Docker?

| Feature | Sandrun | Docker |
|---------|---------|--------|
| **Startup time** | <10ms | ~1s |
| **Memory overhead** | ~10MB | ~100MB |
| **Isolation** | Linux namespaces + seccomp | Container with configurable isolation |
| **Persistence** | None (tmpfs only) | Optional volumes |
| **Network access** | Blocked by default | Configurable |
| **Use case** | Ephemeral code execution | Application deployment |

Sandrun is optimized for **short-lived, untrusted code execution**, while Docker is better for **long-running applications and services**.

## Security Questions

### Is it safe to run untrusted code in Sandrun?

Sandrun provides strong isolation using multiple layers:

1. **Linux namespaces** (PID, network, mount, IPC, UTS)
2. **Seccomp-BPF syscall filtering** (~60 safe syscalls only)
3. **Capability dropping** (no privileged operations)
4. **Resource limits** (CPU, memory, timeout via cgroups)
5. **tmpfs-only storage** (no disk access)

However, **no system is 100% secure**. Sandrun relies on the Linux kernel for isolation. Keep your kernel updated and follow security best practices.

### Can jobs access the network?

**No.** Jobs run in isolated network namespaces with no external connectivity. This prevents:

- Data exfiltration
- Communication between jobs
- DDoS attacks from sandboxed code

If your job needs network access, you'll need to modify Sandrun or use a different solution.

### Can jobs see other jobs?

**No.** Each job runs in its own PID namespace and cannot see processes from other jobs or the host system.

### What happens to my code after execution?

All job data is automatically deleted:

- **After download**: Immediate deletion
- **If failed**: 5 minutes retention for debugging
- **If no download**: 1 hour maximum retention

Data is stored in tmpfs (RAM only) and never touches disk, so it cannot be recovered after deletion.

### Can the server admin see my code?

The server admin could potentially access job data while jobs are running, since Sandrun runs with root privileges. For maximum privacy:

- Run your own Sandrun instance
- Use encrypted tarballs (decrypt inside the sandbox)
- Use end-to-end encryption for sensitive workflows

## Usage Questions

### What languages does Sandrun support?

Out of the box:

- **Python** (python3)
- **JavaScript/Node.js** (node)
- **Bash** (bash)
- **Ruby** (ruby)
- **R** (Rscript)
- **Perl** (perl)
- **PHP** (php)

You can add support for other languages by installing them in the environment templates or including static binaries in your job tarball.

### How do I install dependencies?

Use the `requirements` field in your manifest:

=== "Python"
    ```json
    {
      "entrypoint": "main.py",
      "requirements": "requirements.txt"
    }
    ```
    Runs `pip install -r requirements.txt` before execution.

=== "Node.js"
    ```json
    {
      "entrypoint": "main.js",
      "requirements": "package.json"
    }
    ```
    Runs `npm install` before execution.

=== "Ruby"
    ```json
    {
      "entrypoint": "main.rb",
      "requirements": "Gemfile"
    }
    ```
    Runs `bundle install` before execution.

### What are environment templates?

Pre-built environments with common packages:

- **default**: Minimal system tools
- **ml-basic**: NumPy, SciPy, pandas, scikit-learn
- **vision**: OpenCV, Pillow, scikit-image
- **nlp**: NLTK, spaCy, transformers
- **data-science**: Jupyter, matplotlib, seaborn
- **scientific**: SymPy, NetworkX, statsmodels

Use them in your manifest:
```json
{
  "entrypoint": "train.py",
  "environment": "ml-basic"
}
```

### How do I download output files?

1. **Specify outputs in manifest:**
   ```json
   {
     "outputs": ["results/", "*.png", "model.pkl"]
   }
   ```

2. **List available outputs:**
   ```bash
   curl http://localhost:8443/outputs/job-abc123
   ```

3. **Download specific file:**
   ```bash
   curl http://localhost:8443/download/job-abc123/results/output.png -o output.png
   ```

### What's the maximum job size?

- **Tarball upload**: 100MB (configurable)
- **Memory per job**: 512MB (configurable)
- **Timeout**: 5 minutes (configurable)
- **CPU quota**: 10 seconds per minute per IP

### Can I run long-running jobs?

By default, jobs timeout after 5 minutes. For longer jobs:

1. **Increase timeout in manifest:**
   ```json
   {
     "timeout": 3600  // 1 hour
   }
   ```

2. **Note:** Long jobs consume more CPU quota, which may prevent you from submitting additional jobs.

### How do I check job status?

```bash
# Get status
curl http://localhost:8443/status/job-abc123

# Poll until complete
while true; do
  STATUS=$(curl -s http://localhost:8443/status/job-abc123 | jq -r '.status')
  echo "Status: $STATUS"
  [ "$STATUS" = "completed" ] || [ "$STATUS" = "failed" ] && break
  sleep 2
done
```

Or use WebSocket streaming:
```javascript
const ws = new WebSocket('ws://localhost:8443/logs/job-abc123/stream');
ws.onmessage = (event) => console.log(event.data);
```

## Rate Limiting Questions

### What are the rate limits?

Default limits per IP address:

- **10 CPU-seconds per minute**
- **2 concurrent jobs maximum**
- **512MB RAM per job**
- **5 minute timeout per job**

### How is CPU quota calculated?

CPU quota measures **actual CPU time**, not wall-clock time:

- A 2-second job using 1 CPU core = 2 CPU-seconds
- A 1-second job using 4 CPU cores = 4 CPU-seconds

### What happens if I exceed quota?

You'll receive a 429 error:
```json
{
  "error": "Rate limit exceeded",
  "reason": "CPU quota exhausted (10.2/10.0 seconds used)",
  "retry_after": 45
}
```

Wait for the `retry_after` seconds, or for 1 hour of inactivity to reset quota.

### Can I increase my rate limits?

If running your own instance, edit the configuration in the source code. If using a public instance, you'll need to follow their policies (or run your own!).

## Pool & Distribution Questions

### What is the pool coordinator?

The pool coordinator distributes jobs across multiple Sandrun workers for horizontal scaling. It provides:

- Health checking of workers
- Load balancing across available workers
- Centralized job submission
- Result proxying

[Learn more →](integrations/trusted-pool.md)

### How do I set up a pool?

1. Start multiple Sandrun workers with worker keys
2. Create `workers.json` with worker public keys
3. Run pool coordinator with worker allowlist
4. Submit jobs to coordinator instead of individual workers

[Detailed setup guide →](integrations/trusted-pool.md#setup)

### What's the difference between trusted and trustless pools?

| Feature | Trusted Pool | Trustless Pool |
|---------|-------------|----------------|
| Worker authorization | Allowlist (public keys) | Open (anyone) |
| Result verification | None (trust workers) | Consensus + verification |
| Economic model | None | Stake/slashing |
| Complexity | Simple (~200 lines) | Complex |
| Use case | Private cluster | Public compute |

Most users should use the **trusted pool** for private deployments.

## Integration Questions

### Can I use Sandrun with Claude?

Yes! The MCP (Model Context Protocol) server integration allows Claude Desktop to execute code via Sandrun:

1. Install MCP server: `pip install -e integrations/mcp-server`
2. Add to Claude Desktop config
3. Ask Claude to execute code

[MCP integration guide →](integrations/mcp-server.md)

### How do I integrate Sandrun into my app?

Use the REST API directly:

```python
import requests

# Submit job
response = requests.post('http://localhost:8443/submit',
    files={'files': open('job.tar.gz', 'rb')},
    data={'manifest': '{"entrypoint":"main.py"}'})

job_id = response.json()['job_id']

# Get results
status = requests.get(f'http://localhost:8443/status/{job_id}').json()
```

See [API Reference](api-reference.md) for full documentation.

### Can I use Sandrun in CI/CD?

Yes! Sandrun is great for isolated test execution:

```yaml
# .github/workflows/test.yml
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Run tests in Sandrun
        run: |
          tar czf tests.tar.gz tests/
          curl -X POST http://sandrun.example.com:8443/submit \
            -F "files=@tests.tar.gz" \
            -F 'manifest={"entrypoint":"run_tests.sh"}'
```

## Troubleshooting Questions

### Why is my job stuck in "queued" status?

Possible causes:

1. **No available workers** (if using pool)
2. **Rate limit reached** (2 concurrent jobs per IP)
3. **Server overloaded** (check `/stats` endpoint)

### Why did my job fail?

Check logs for errors:
```bash
curl http://localhost:8443/logs/job-abc123
```

Common causes:

- Missing files in tarball
- Syntax errors in code
- Missing dependencies
- Exceeded memory limit
- Timeout (job took too long)

### How do I report bugs?

1. Check [GitHub Issues](https://github.com/yourusername/sandrun/issues) for existing reports
2. Create new issue with:
   - Sandrun version (`./build/sandrun --version`)
   - Operating system and kernel version
   - Steps to reproduce
   - Error messages and logs

### Where can I get help?

- **Documentation**: This site!
- **GitHub Discussions**: Ask questions, share use cases
- **GitHub Issues**: Report bugs and request features
- **Source Code**: Read the code in `src/`

## Performance Questions

### How fast is Sandrun?

- **Job startup**: <10ms
- **API latency**: <50ms
- **Throughput**: 100+ jobs/second (on modern hardware)
- **Overhead**: <1% CPU for orchestration

### Can Sandrun handle GPU workloads?

Yes! Use the `gpu` field in your manifest:

```json
{
  "entrypoint": "train.py",
  "gpu": {
    "required": true,
    "min_vram_gb": 8,
    "cuda_version": "11.8"
  }
}
```

Note: GPU support requires proper NVIDIA drivers and configuration on the host.

### How does Sandrun scale?

- **Vertical**: Single instance can handle 100+ jobs/second
- **Horizontal**: Use pool coordinator to distribute across multiple workers
- **Geographic**: Deploy pools in different regions

## Development Questions

### How do I build Sandrun from source?

```bash
git clone https://github.com/yourusername/sandrun.git
cd sandrun
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

[Full build guide →](development/building.md)

### How do I contribute?

1. Fork the repository
2. Create a feature branch
3. Write tests for your changes
4. Submit a pull request

[Contributing guidelines →](https://github.com/yourusername/sandrun/blob/master/CONTRIBUTING.md)

### Where is the code?

- **C++ Backend**: `src/`
- **Integrations**: `integrations/`
- **Tests**: `tests/`
- **Documentation**: `docs/`

---

**Have more questions?** [Open a discussion on GitHub →](https://github.com/yourusername/sandrun/discussions)
