#!/usr/bin/env python3
"""
Sandrun Python Client
Simple, elegant client for submitting jobs to Sandrun server
"""

import requests
import tarfile
import json
import time
import os
import io
from pathlib import Path
from typing import Dict, List, Optional, Any


class SandrunClient:
    """Client for interacting with Sandrun anonymous code execution service."""
    
    def __init__(self, server_url: str = "http://localhost:8443"):
        """
        Initialize Sandrun client.
        
        Args:
            server_url: Base URL of Sandrun server
        """
        self.server_url = server_url.rstrip('/')
        self.session = requests.Session()
    
    def test_connection(self) -> bool:
        """Test connection to Sandrun server."""
        try:
            resp = self.session.get(self.server_url)
            data = resp.json()
            return data.get('service') == 'sandrun'
        except Exception as e:
            print(f"Connection failed: {e}")
            return False
    
    def submit_directory(self, 
                        directory: str,
                        entrypoint: str,
                        interpreter: str = "python3",
                        args: List[str] = None,
                        outputs: List[str] = None,
                        timeout: int = 300,
                        memory_mb: int = 512) -> str:
        """
        Submit a directory of files as a job.
        
        Args:
            directory: Path to directory containing job files
            entrypoint: Main file to execute
            interpreter: Interpreter to use (python3, node, bash, etc.)
            args: Command line arguments
            outputs: Output file patterns to collect
            timeout: Timeout in seconds
            memory_mb: Memory limit in MB
            
        Returns:
            Job ID
        """
        # Create tar.gz archive
        tar_buffer = io.BytesIO()
        with tarfile.open(fileobj=tar_buffer, mode='w:gz') as tar:
            tar.add(directory, arcname='.')
        tar_buffer.seek(0)
        
        # Create manifest
        manifest = {
            'entrypoint': entrypoint,
            'interpreter': interpreter,
            'timeout': timeout,
            'memory_mb': memory_mb
        }
        if args:
            manifest['args'] = args
        if outputs:
            manifest['outputs'] = outputs
        
        # Submit job
        files = {'files': ('job.tar.gz', tar_buffer, 'application/gzip')}
        data = {'manifest': json.dumps(manifest)}
        
        resp = self.session.post(f"{self.server_url}/submit", files=files, data=data)
        resp.raise_for_status()
        
        result = resp.json()
        return result['job_id']
    
    def submit_code(self,
                   code: str,
                   interpreter: str = "python3",
                   filename: str = None) -> str:
        """
        Submit code directly as a job.
        
        Args:
            code: Source code to execute
            interpreter: Interpreter to use
            filename: Name for the code file (defaults to main.ext)
            
        Returns:
            Job ID
        """
        # Determine filename
        if not filename:
            extensions = {
                'python3': 'py',
                'python': 'py',
                'node': 'js',
                'bash': 'sh',
                'sh': 'sh'
            }
            ext = extensions.get(interpreter, 'txt')
            filename = f"main.{ext}"
        
        # Create tar.gz with single file
        tar_buffer = io.BytesIO()
        with tarfile.open(fileobj=tar_buffer, mode='w:gz') as tar:
            code_bytes = code.encode('utf-8')
            tarinfo = tarfile.TarInfo(name=filename)
            tarinfo.size = len(code_bytes)
            tar.addfile(tarinfo, io.BytesIO(code_bytes))
        tar_buffer.seek(0)
        
        # Create manifest
        manifest = {
            'entrypoint': filename,
            'interpreter': interpreter
        }
        
        # Submit job
        files = {'files': ('job.tar.gz', tar_buffer, 'application/gzip')}
        data = {'manifest': json.dumps(manifest)}
        
        resp = self.session.post(f"{self.server_url}/submit", files=files, data=data)
        resp.raise_for_status()
        
        result = resp.json()
        return result['job_id']
    
    def get_status(self, job_id: str) -> Dict[str, Any]:
        """Get job status."""
        resp = self.session.get(f"{self.server_url}/status/{job_id}")
        resp.raise_for_status()
        return resp.json()
    
    def get_logs(self, job_id: str) -> Dict[str, str]:
        """Get job stdout and stderr."""
        resp = self.session.get(f"{self.server_url}/logs/{job_id}")
        resp.raise_for_status()
        return resp.json()
    
    def list_outputs(self, job_id: str) -> List[str]:
        """List output files for a job."""
        resp = self.session.get(f"{self.server_url}/outputs/{job_id}")
        resp.raise_for_status()
        data = resp.json()
        return data.get('files', [])
    
    def download_file(self, job_id: str, filename: str, save_path: str = None) -> bytes:
        """
        Download a specific output file.
        
        Args:
            job_id: Job ID
            filename: Name of file to download
            save_path: Optional path to save file to
            
        Returns:
            File contents as bytes
        """
        resp = self.session.get(f"{self.server_url}/download/{job_id}/{filename}")
        resp.raise_for_status()
        
        if save_path:
            with open(save_path, 'wb') as f:
                f.write(resp.content)
        
        return resp.content
    
    def wait_for_completion(self, job_id: str, poll_interval: int = 2) -> Dict[str, Any]:
        """
        Wait for job to complete.
        
        Args:
            job_id: Job ID
            poll_interval: Seconds between status checks
            
        Returns:
            Final job status
        """
        while True:
            status = self.get_status(job_id)
            if status['status'] in ['completed', 'failed']:
                return status
            time.sleep(poll_interval)
    
    def run_and_wait(self,
                    code: str = None,
                    directory: str = None,
                    **kwargs) -> Dict[str, Any]:
        """
        Submit job and wait for completion.
        
        Args:
            code: Code to execute (for quick jobs)
            directory: Directory to submit (for file-based jobs)
            **kwargs: Additional arguments for submit methods
            
        Returns:
            Dict with job_id, status, logs, and outputs
        """
        # Submit job
        if code:
            job_id = self.submit_code(code, **kwargs)
        elif directory:
            job_id = self.submit_directory(directory, **kwargs)
        else:
            raise ValueError("Either 'code' or 'directory' must be provided")
        
        print(f"Job submitted: {job_id}")
        
        # Wait for completion
        print("Waiting for completion...")
        final_status = self.wait_for_completion(job_id)
        
        # Get logs
        logs = self.get_logs(job_id)
        
        # Get outputs
        output_files = self.list_outputs(job_id)
        
        return {
            'job_id': job_id,
            'status': final_status,
            'logs': logs,
            'output_files': output_files
        }


# Example usage functions
def example_quick_code():
    """Example: Submit quick Python code."""
    client = SandrunClient()
    
    code = """
import json
import hashlib

data = {"message": "Hello from Sandrun!"}
print(json.dumps(data, indent=2))

# Calculate hash
hash_val = hashlib.sha256(json.dumps(data).encode()).hexdigest()
print(f"SHA256: {hash_val}")

# Write output file
with open("output.txt", "w") as f:
    f.write(f"Processed data: {data}\\n")
    f.write(f"Hash: {hash_val}\\n")
print("Output written to output.txt")
"""
    
    result = client.run_and_wait(code=code)
    
    print("\n=== Results ===")
    print(f"Status: {result['status']['status']}")
    print(f"stdout:\n{result['logs']['stdout']}")
    if result['logs']['stderr']:
        print(f"stderr:\n{result['logs']['stderr']}")
    
    # Download output files
    for filename in result['output_files']:
        print(f"Downloading {filename}...")
        client.download_file(result['job_id'], filename, f"./{filename}")


def example_submit_directory():
    """Example: Submit a directory of files."""
    client = SandrunClient()
    
    # Create example project directory
    project_dir = Path("./example_project")
    project_dir.mkdir(exist_ok=True)
    
    # Create main.py
    (project_dir / "main.py").write_text("""
import sys
from processor import process_data

if __name__ == "__main__":
    data = sys.argv[1] if len(sys.argv) > 1 else "default"
    result = process_data(data)
    print(f"Result: {result}")
    
    with open("result.json", "w") as f:
        import json
        json.dump({"result": result}, f)
""")
    
    # Create processor.py
    (project_dir / "processor.py").write_text("""
def process_data(data):
    return f"Processed: {data.upper()}"
""")
    
    # Submit directory
    result = client.run_and_wait(
        directory=str(project_dir),
        entrypoint="main.py",
        args=["test-input"],
        outputs=["*.json"]
    )
    
    print("\n=== Results ===")
    print(f"stdout:\n{result['logs']['stdout']}")
    
    # Download results
    for filename in result['output_files']:
        content = client.download_file(result['job_id'], filename)
        print(f"Downloaded {filename}: {content.decode()}")


def example_batch_processing():
    """Example: Process multiple jobs in parallel."""
    client = SandrunClient()
    
    # Submit multiple jobs
    job_ids = []
    for i in range(3):
        code = f"""
import time
import random

print(f"Job {i} starting...")
time.sleep(random.uniform(1, 3))
result = {i} * {i}
print(f"Job {i} result: {{result}}")

with open("result_{i}.txt", "w") as f:
    f.write(str(result))
"""
        job_id = client.submit_code(code)
        job_ids.append(job_id)
        print(f"Submitted job {i}: {job_id}")
    
    # Wait for all jobs
    results = []
    for job_id in job_ids:
        print(f"Waiting for {job_id}...")
        status = client.wait_for_completion(job_id)
        logs = client.get_logs(job_id)
        results.append({
            'job_id': job_id,
            'status': status,
            'output': logs['stdout']
        })
    
    # Display results
    print("\n=== All Jobs Complete ===")
    for result in results:
        print(f"{result['job_id']}: {result['status']['status']}")
        print(f"  Output: {result['output'].strip()}")


if __name__ == "__main__":
    import sys
    
    # Simple CLI
    if len(sys.argv) > 1:
        if sys.argv[1] == "quick":
            example_quick_code()
        elif sys.argv[1] == "directory":
            example_submit_directory()
        elif sys.argv[1] == "batch":
            example_batch_processing()
        else:
            print("Usage: sandrun_client.py [quick|directory|batch]")
    else:
        # Default: run quick example
        print("Running quick code example...")
        example_quick_code()