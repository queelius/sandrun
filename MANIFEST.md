# Job Manifest Specification

## Overview

Each job submitted to Sandrun can include a manifest file (`job.json`) that specifies execution parameters. This allows for reproducible, queue-friendly batch processing.

## Manifest Format

```json
{
  "entrypoint": "main.py",
  "interpreter": "python3", 
  "args": ["--input", "data.csv"],
  "env": {
    "PYTHONPATH": "./lib"
  },
  "outputs": [
    "results/",
    "*.png",
    "logs/*.log"
  ],
  "requirements": "requirements.txt",
  "timeout": 300,
  "memory_mb": 512,
  "cpu_seconds": 10
}
```

## Fields

### `entrypoint` (required)
- **Type**: string
- **Description**: The script or binary to execute
- **Examples**: `"main.py"`, `"run.sh"`, `"analyze.R"`

### `interpreter` (optional)
- **Type**: string  
- **Default**: Auto-detected from file extension
- **Options**: `"python3"`, `"node"`, `"bash"`, `"ruby"`, `"Rscript"`
- **Description**: The interpreter to use for the entrypoint

### `args` (optional)
- **Type**: array of strings
- **Default**: `[]`
- **Description**: Command-line arguments to pass to the entrypoint
- **Example**: `["--input", "data.csv", "--verbose"]`

### `env` (optional)
- **Type**: object
- **Default**: `{}`
- **Description**: Environment variables to set
- **Note**: Cannot override system security variables

### `outputs` (optional)
- **Type**: array of strings (glob patterns)
- **Default**: `["*"]` (everything)
- **Description**: Files/directories to include in output download
- **Examples**:
  - `"results/"` - Include entire results directory
  - `"*.png"` - All PNG files
  - `"output.json"` - Specific file
  - `"logs/*.log"` - All log files in logs directory

### `requirements` (optional)
- **Type**: string
- **Description**: Dependencies file to install before execution
- **Supported**:
  - Python: `"requirements.txt"` → runs `pip install -r requirements.txt`
  - Node: `"package.json"` → runs `npm install`
  - Ruby: `"Gemfile"` → runs `bundle install`

### `timeout` (optional)
- **Type**: integer (seconds)
- **Default**: 300 (5 minutes)
- **Maximum**: 3600 (1 hour)
- **Description**: Maximum execution time

### `memory_mb` (optional)
- **Type**: integer
- **Default**: 512
- **Maximum**: 2048
- **Description**: Memory limit in megabytes

### `cpu_seconds` (optional)
- **Type**: integer
- **Default**: 10
- **Maximum**: 60
- **Description**: CPU seconds per minute quota

### `gpu` (optional)
- **Type**: object
- **Description**: GPU requirements for ML/compute workloads
- **Fields**:
  - `required` (boolean): Whether GPU is required
  - `device_id` (integer): Specific GPU device (default: 0)
  - `min_vram_gb` (integer): Minimum VRAM required in GB
  - `cuda_version` (string): Minimum CUDA version (e.g., "11.8")
  - `compute_capability` (string): Minimum compute capability (e.g., "7.0")
- **Example**:
  ```json
  {
    "required": true,
    "min_vram_gb": 8,
    "cuda_version": "11.8",
    "compute_capability": "7.0"
  }
  ```

## Execution Flow

1. **Upload**: User uploads directory with code and `job.json`
2. **Queue**: Job enters queue with manifest metadata
3. **Prepare**: When job starts:
   - Extract files to sandbox
   - Install dependencies if specified
   - Set environment variables
4. **Execute**: Run entrypoint with args
5. **Capture**: 
   - stdout → `stdout.log`
   - stderr → `stderr.log`
   - Both streamed to API for live viewing
6. **Package**: Create output archive with files matching `outputs` patterns
7. **Cleanup**: Delete all job data after download or timeout

## Examples

### Python Data Analysis
```json
{
  "entrypoint": "analyze.py",
  "args": ["--dataset", "sales.csv"],
  "outputs": ["figures/", "report.pdf"],
  "requirements": "requirements.txt",
  "memory_mb": 1024
}
```

### Node.js Build Job
```json
{
  "entrypoint": "build.js",
  "interpreter": "node",
  "env": {
    "NODE_ENV": "production"
  },
  "outputs": ["dist/"],
  "requirements": "package.json"
}
```

### Shell Script Pipeline
```json
{
  "entrypoint": "pipeline.sh",
  "interpreter": "bash",
  "outputs": ["processed/*.csv", "summary.txt"],
  "timeout": 600
}
```

### R Statistical Analysis
```json
{
  "entrypoint": "model.R",
  "interpreter": "Rscript",
  "args": ["--confidence", "0.95"],
  "outputs": ["plots/*.png", "results.rds"]
}
```

### ML Training with GPU
```json
{
  "entrypoint": "train.py",
  "args": ["--epochs", "10", "--batch-size", "32"],
  "gpu": {
    "required": true,
    "min_vram_gb": 8,
    "cuda_version": "11.8"
  },
  "outputs": ["checkpoints/", "metrics.json"],
  "requirements": "requirements.txt",
  "timeout": 1800,
  "memory_mb": 2048
}
```

### Stable Diffusion Inference
```json
{
  "entrypoint": "generate.py",
  "args": ["--prompt", "a beautiful sunset", "--steps", "50"],
  "gpu": {
    "required": true,
    "min_vram_gb": 6,
    "compute_capability": "7.0"
  },
  "outputs": ["images/*.png"],
  "timeout": 600
}
```

## Privacy Considerations

- Manifest is deleted immediately after job parsing
- No manifest contents are logged (only metrics)
- Output patterns are applied before download (unmatched files never leave sandbox)
- All job data auto-deleted after:
  - Successful download (immediate)
  - Job failure (5 minutes)
  - No download (1 hour)

## API Endpoints

### Submit Job
```
POST /submit
Content-Type: multipart/form-data

Files: [project files]
Manifest: job.json (optional, can be in files or separate field)
```

### Get Status
```
GET /status/{job_id}

Returns:
{
  "status": "queued|running|completed|failed",
  "queue_position": 3,
  "metrics": {
    "cpu_seconds": 2.34,
    "memory_mb": 128,
    "runtime": 45
  }
}
```

### Stream Logs
```
GET /logs/{job_id}

Returns:
{
  "stdout": "...",
  "stderr": "..."
}
```

### List Outputs
```
GET /outputs/{job_id}

Returns:
{
  "files": ["results/analysis.csv", "plot.png"]
}
```

### Download Outputs
```
GET /download/{job_id}          # All outputs as tar.gz
GET /download/{job_id}/{file}   # Specific file
```