# Sandrun Integrations

Simple, elegant ways to interact with Sandrun's anonymous code execution service.

## 🌐 Web Frontend

A clean, minimal web interface for submitting and monitoring jobs.

### Features
- **Configurable server endpoint** - Connect to any Sandrun instance
- **Drag-and-drop file upload** - Upload entire directories
- **Quick code mode** - Submit code directly without files  
- **Real-time monitoring** - Watch job progress and logs
- **Output downloads** - Retrieve generated files

### Usage
```bash
# Serve the frontend
cd web-frontend
python3 -m http.server 8000

# Open in browser
# http://localhost:8000
```

The server URL can be configured directly in the UI. Default: `http://localhost:8443`

### No Dependencies
- Pure HTML/CSS/JavaScript
- No build process required
- No framework dependencies
- Works in any modern browser

## 🐍 Python Client

Elegant Python library for programmatic job submission.

### Installation
```bash
pip install requests  # Only dependency
```

### Quick Start
```python
from sandrun_client import SandrunClient

# Initialize client (server URL configurable)
client = SandrunClient("http://localhost:8443")

# Submit quick code
result = client.run_and_wait(
    code="print('Hello from Sandrun!')",
    interpreter="python3"
)
print(result['logs']['stdout'])

# Submit directory
result = client.run_and_wait(
    directory="./my_project",
    entrypoint="main.py",
    args=["--input", "data.csv"]
)
```

### Examples
```bash
# Run examples
python sandrun_client.py quick      # Quick code execution
python sandrun_client.py directory  # Directory submission
python sandrun_client.py batch      # Parallel job processing
```

## 📟 Command Line (curl)

Simple bash script with curl examples for all API endpoints.

### Usage
```bash
# Make executable
chmod +x examples/curl_examples.sh

# Run examples (uses localhost:8443 by default)
./examples/curl_examples.sh

# Use different server
SANDRUN_SERVER=http://myserver:8443 ./examples/curl_examples.sh
```

### Quick Submit
```bash
# Submit Python code
curl -X POST http://localhost:8443/submit \
  -F "files=@job.tar.gz" \
  -F 'manifest={"entrypoint":"main.py","interpreter":"python3"}'

# Check status
curl http://localhost:8443/status/{job_id}

# Get logs
curl http://localhost:8443/logs/{job_id}
```

## API Reference

### Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Server info and status |
| POST | `/submit` | Submit new job |
| GET | `/status/{job_id}` | Get job status |
| GET | `/logs/{job_id}` | Get stdout/stderr |
| GET | `/outputs/{job_id}` | List output files |
| GET | `/download/{job_id}/{file}` | Download specific file |

### Job Manifest

```json
{
  "entrypoint": "main.py",      // Required: file to execute
  "interpreter": "python3",      // Required: python3/node/bash/sh
  "args": ["--flag", "value"],  // Optional: command line args
  "outputs": ["*.png", "results/"], // Optional: files to collect
  "timeout": 300,                // Optional: seconds (default 300)
  "memory_mb": 512              // Optional: MB (default 512)
}
```

### Supported Interpreters
- `python3` - Python 3.x
- `python` - Python 2.x
- `node` - Node.js
- `bash` - Bash shell
- `sh` - POSIX shell

## Design Philosophy

These integrations follow Sandrun's core principles:

1. **Simplicity** - No unnecessary complexity
2. **Privacy** - No tracking, no accounts
3. **Elegance** - Clean, readable code
4. **Flexibility** - Use any language or tool
5. **Configurable** - Connect to any Sandrun server

## Custom Integrations

Creating your own integration is simple. The API accepts:
- **Files**: tar.gz archive with your code
- **Manifest**: JSON configuration

Any HTTP client in any language can interact with Sandrun:

```ruby
# Ruby example
require 'net/http'
require 'json'

uri = URI('http://localhost:8443/submit')
request = Net::HTTP::Post.new(uri)
request.set_form([
  ['files', File.open('job.tar.gz')],
  ['manifest', {entrypoint: 'main.rb', interpreter: 'ruby'}.to_json]
])
response = Net::HTTP.start(uri.hostname, uri.port) {|http| http.request(request)}
```

```go
// Go example
resp, _ := http.PostForm("http://localhost:8443/submit",
    url.Values{
        "files": {jobTarGz},
        "manifest": {`{"entrypoint":"main.go","interpreter":"go"}`},
    })
```

## Security Notes

- All jobs run in isolated sandboxes
- Network access is blocked
- System files are inaccessible  
- Jobs auto-delete after completion
- No data persistence between jobs
- Anonymous usage - no accounts needed

## License

Same as Sandrun main project.