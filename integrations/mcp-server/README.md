# Sandrun MCP Server

**Give Claude (and other LLMs) the power to execute code safely!**

This MCP (Model Context Protocol) server allows AI assistants like Claude to execute Python, JavaScript, and Bash code in Sandrun's secure sandbox environment.

## Why This Is Awesome

### For Users:
- **Safe AI Code Execution**: Claude can run code without risking your system
- **Instant Results**: LLM writes code → Sandrun executes → Results back to LLM
- **No Docker Required**: Lightweight Linux namespaces instead of containers
- **Fair Resource Sharing**: Built-in rate limiting and queuing

### For Developers:
- **Drop-in Solution**: Just add to Claude Desktop config
- **Multiple Languages**: Python, JavaScript/Node, Bash out of the box
- **Async Support**: Non-blocking execution with status polling
- **Battle-Tested**: Uses Sandrun's proven isolation

## Quick Start

### 1. Install Dependencies

```bash
cd integrations/mcp-server
pip install -e .
```

### 2. Start Sandrun

```bash
# In terminal 1
sudo ./build/sandrun --port 8443
```

### 3. Configure Claude Desktop

Add to `~/Library/Application Support/Claude/claude_desktop_config.json` (Mac) or
`%APPDATA%\Claude\claude_desktop_config.json` (Windows):

```json
{
  "mcpServers": {
    "sandrun": {
      "command": "python3",
      "args": [
        "/path/to/sandrun/integrations/mcp-server/sandrun_mcp.py"
      ]
    }
  }
}
```

### 4. Restart Claude Desktop

The MCP server will start automatically when Claude launches.

## Usage Examples

Once configured, you can ask Claude:

### Example 1: Data Analysis
```
"Can you analyze this list of numbers and tell me the mean, median, and mode?"
```

Claude will use `execute_python` to run NumPy/statistics code and return results.

### Example 2: File Processing
```
"I need to process this CSV data and extract unique values from column 3"
```

Claude writes Python/pandas code, executes it, and shows you the results.

### Example 3: Algorithm Implementation
```
"Implement the Sieve of Eratosthenes for finding primes up to 1000"
```

Claude writes the code, runs it via Sandrun, and shows both code and output.

### Example 4: Text Processing
```
"Use bash tools to count word frequencies in this text"
```

Claude uses `execute_bash` with grep/awk/sort to process text.

## Available Tools

The MCP server exposes these tools to the LLM:

### `execute_python`
Execute Python code in a secure sandbox.
- **Input**: Python code string
- **Output**: stdout, stderr, exit code, metrics
- **Limits**: 512MB RAM, 5min timeout, no network

### `execute_javascript`
Execute JavaScript/Node.js code.
- Same security and limits as Python

### `execute_bash`
Execute Bash commands/scripts.
- Standard Unix tools available
- Same isolation and limits

### `check_job_status`
Check status of a submitted job.
- Returns: queued | running | completed | failed

### `get_job_logs`
Get execution output for a job.
- Returns: stdout and stderr

## Security Features

All code executes with:
- ✅ **Namespace Isolation**: Separate PID, network, mount namespaces
- ✅ **No Network Access**: Completely airgapped execution
- ✅ **Resource Limits**: 512MB RAM, 5 minute timeout
- ✅ **Seccomp Filtering**: Only ~60 safe syscalls allowed
- ✅ **Auto-Cleanup**: All data destroyed after execution
- ✅ **Rate Limiting**: 10 CPU-seconds per minute per IP

## Configuration

### Environment Variables

- `SANDRUN_URL`: Sandrun server URL (default: http://localhost:8443)
- `SANDRUN_TIMEOUT`: Max wait time for jobs (default: 30 seconds)

### Custom Configuration

Edit `sandrun_mcp.py` to customize:
- Timeout values
- Server URL
- Tool descriptions
- Response formatting

## Architecture

```
┌──────────────┐
│ Claude       │  "Calculate fibonacci(100)"
│ Desktop      │
└──────┬───────┘
       │ MCP Protocol (stdio)
       ▼
┌──────────────┐
│ sandrun_mcp  │  Translates to HTTP API calls
│ .py          │
└──────┬───────┘
       │ HTTP REST
       ▼
┌──────────────┐
│ Sandrun      │  Executes in isolated sandbox
│ (C++ server) │  Returns stdout/stderr
└──────────────┘
```

## Troubleshooting

### "Connection refused"
- Make sure Sandrun is running: `sudo ./build/sandrun --port 8443`
- Check firewall settings

### "Job timeout"
- Increase `SANDRUN_TIMEOUT` for longer-running code
- Check if Sandrun is under heavy load

### "Rate limit exceeded"
- Too many requests from same IP
- Wait 1 minute or increase limits in Sandrun config

### Claude doesn't see the tools
- Check Claude Desktop config file syntax
- Restart Claude Desktop
- Check MCP server logs

## Development

### Running Standalone

```bash
# Test the MCP server directly
python3 sandrun_mcp.py
```

Then interact via MCP protocol over stdin/stdout.

### Testing Tools

```python
import asyncio
from sandrun_mcp import SandrunMCPServer

async def test():
    server = SandrunMCPServer()
    result = await server.execute_python("print('Hello!')")
    print(result)

asyncio.run(test())
```

## Use Cases

### 1. **Code Learning**
Ask Claude to demonstrate algorithms, data structures, or language features with live execution.

### 2. **Data Analysis**
Give Claude a dataset, ask for analysis, get immediate pandas/numpy results.

### 3. **Prototyping**
Quickly test ideas: "Does this regex work?" "What's the output of this function?"

### 4. **Teaching**
Students can ask Claude questions and see working code examples executed.

### 5. **Debugging**
"Why isn't this code working?" Claude can modify and test hypotheses.

## Comparison to Alternatives

| Feature | Sandrun MCP | E2B | Docker | Cloud Functions |
|---------|-------------|-----|--------|----------------|
| Setup Time | 1 min | Account signup | 5-10 min | Account signup |
| Cold Start | <100ms | ~1s | ~1s | ~500ms |
| Memory Overhead | ~10MB | ~50MB | ~100MB | Varies |
| Network Isolation | ✅ | ❌ | Optional | ❌ |
| Cost | Free | Paid tiers | Free | Paid |
| Privacy | Complete | Data sent to cloud | Local | Data sent to cloud |

## Contributing

Ideas for improvements:
- Add more language support (Ruby, Go, Rust)
- Streaming output support
- File upload/download for multi-file projects
- Custom resource limits per tool
- Output visualization (plots, charts)

## License

Same as Sandrun main project (MIT).

---

**Now LLMs can safely execute code!** 🎉
