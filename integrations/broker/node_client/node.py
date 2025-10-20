#!/usr/bin/env python3
"""
Sandrun Node Client - Connects sandrun instance to broker
"""

import os
import sys
import json
import time
import requests
import tempfile
import tarfile
import argparse
import threading
import multiprocessing
from typing import Dict, Any, Optional
from datetime import datetime

class SandrunNode:
    """Node client that polls broker and executes jobs via sandrun"""
    
    def __init__(self, broker_url: str, sandrun_url: str, config_file: str = None):
        self.broker_url = broker_url.rstrip('/')
        self.sandrun_url = sandrun_url.rstrip('/')
        self.node_id = None
        self.running = False
        
        # Load configuration
        self.config = self.load_config(config_file)
        
        # Detect capabilities
        self.capabilities = self.detect_capabilities()
        
    def load_config(self, config_file: str) -> Dict[str, Any]:
        """Load configuration from file or defaults"""
        defaults = {
            'poll_interval': 5,
            'heartbeat_interval': 30,
            'max_concurrent_jobs': 1,
            'timeout_buffer': 10  # Extra seconds for sandrun timeout
        }
        
        if config_file and os.path.exists(config_file):
            with open(config_file, 'r') as f:
                loaded = json.load(f)
                defaults.update(loaded)
        
        return defaults
    
    def detect_capabilities(self) -> Dict[str, Any]:
        """Detect node capabilities"""
        capabilities = {
            'cpu_cores': multiprocessing.cpu_count(),
            'memory_gb': self.get_memory_gb(),
            'gpu': self.check_gpu(),
            'interpreters': self.check_interpreters()
        }
        
        # Override with config if present
        if 'capabilities' in self.config:
            capabilities.update(self.config['capabilities'])
        
        return capabilities
    
    def get_memory_gb(self) -> int:
        """Get system memory in GB"""
        try:
            with open('/proc/meminfo', 'r') as f:
                for line in f:
                    if line.startswith('MemTotal:'):
                        kb = int(line.split()[1])
                        return kb // (1024 * 1024)
        except:
            return 4  # Default fallback
        return 4
    
    def check_gpu(self) -> bool:
        """Check if GPU is available"""
        try:
            # Check for NVIDIA GPU
            import subprocess
            result = subprocess.run(['nvidia-smi'], capture_output=True, timeout=5)
            return result.returncode == 0
        except:
            return False
    
    def check_interpreters(self) -> list:
        """Check available interpreters"""
        interpreters = []
        checks = [
            ('python3', ['python3', '--version']),
            ('node', ['node', '--version']),
            ('bash', ['bash', '--version']),
            ('ruby', ['ruby', '--version']),
        ]
        
        for name, cmd in checks:
            try:
                import subprocess
                result = subprocess.run(cmd, capture_output=True, timeout=5)
                if result.returncode == 0:
                    interpreters.append(name)
            except:
                pass
        
        return interpreters
    
    def register(self) -> bool:
        """Register with broker"""
        try:
            response = requests.post(
                f"{self.broker_url}/register",
                json={
                    'endpoint': self.sandrun_url,
                    'capabilities': self.capabilities
                },
                timeout=10
            )
            
            if response.status_code == 200:
                self.node_id = response.json()['node_id']
                print(f"Registered with broker as node {self.node_id}")
                return True
        except Exception as e:
            print(f"Registration failed: {e}")
        
        return False
    
    def heartbeat(self):
        """Send heartbeat to broker"""
        if not self.node_id:
            return
        
        try:
            requests.post(
                f"{self.broker_url}/heartbeat",
                json={'node_id': self.node_id},
                timeout=5
            )
        except Exception as e:
            print(f"Heartbeat failed: {e}")
    
    def heartbeat_loop(self):
        """Background thread for heartbeats"""
        while self.running:
            self.heartbeat()
            time.sleep(self.config['heartbeat_interval'])
    
    def claim_job(self) -> Optional[Dict[str, Any]]:
        """Claim next available job from broker"""
        try:
            response = requests.post(
                f"{self.broker_url}/claim",
                json={'node_id': self.node_id},
                timeout=10
            )
            
            if response.status_code == 200:
                data = response.json()
                return data.get('job')
        except Exception as e:
            print(f"Failed to claim job: {e}")
        
        return None
    
    def execute_job(self, job: Dict[str, Any]) -> Dict[str, Any]:
        """Execute job using sandrun"""
        print(f"Executing job {job['id']}")
        
        # Create temporary directory for job files
        with tempfile.TemporaryDirectory() as tmpdir:
            # Write code to file
            code_file = os.path.join(tmpdir, 'main.py')
            with open(code_file, 'w') as f:
                f.write(job['code'])
            
            # Create tar archive
            tar_path = os.path.join(tmpdir, 'job.tar.gz')
            with tarfile.open(tar_path, 'w:gz') as tar:
                tar.add(code_file, arcname='main.py')
            
            # Prepare manifest
            manifest = {
                'entrypoint': 'main.py',
                'interpreter': job['interpreter'],
                'args': job.get('args', []),
                'timeout': self.config.get('job_timeout', 300)
            }
            
            # Submit to sandrun
            try:
                with open(tar_path, 'rb') as f:
                    files = {'files': ('job.tar.gz', f, 'application/gzip')}
                    data = {'manifest': json.dumps(manifest)}
                    
                    response = requests.post(
                        f"{self.sandrun_url}/submit",
                        files=files,
                        data=data,
                        timeout=10
                    )
                
                if response.status_code != 200:
                    return {
                        'output': '',
                        'error': f"Sandrun submission failed: {response.text}",
                        'exit_code': 1
                    }
                
                sandrun_job_id = response.json()['job_id']
                
                # Poll for completion
                timeout = manifest['timeout'] + self.config['timeout_buffer']
                start_time = time.time()
                
                while time.time() - start_time < timeout:
                    status_response = requests.get(
                        f"{self.sandrun_url}/status/{sandrun_job_id}",
                        timeout=5
                    )
                    
                    if status_response.status_code == 200:
                        status = status_response.json()
                        
                        if status['status'] == 'completed':
                            # Get logs
                            logs_response = requests.get(
                                f"{self.sandrun_url}/logs/{sandrun_job_id}",
                                timeout=10
                            )
                            
                            if logs_response.status_code == 200:
                                logs = logs_response.json()
                                return {
                                    'output': logs.get('stdout', ''),
                                    'error': logs.get('stderr', ''),
                                    'exit_code': 0
                                }
                        
                        elif status['status'] == 'failed':
                            logs_response = requests.get(
                                f"{self.sandrun_url}/logs/{sandrun_job_id}",
                                timeout=10
                            )
                            
                            if logs_response.status_code == 200:
                                logs = logs_response.json()
                                return {
                                    'output': logs.get('stdout', ''),
                                    'error': logs.get('stderr', 'Job failed'),
                                    'exit_code': 1
                                }
                    
                    time.sleep(2)
                
                # Timeout
                return {
                    'output': '',
                    'error': 'Job execution timeout',
                    'exit_code': 124  # Standard timeout exit code
                }
                
            except Exception as e:
                return {
                    'output': '',
                    'error': f"Execution error: {str(e)}",
                    'exit_code': 1
                }
    
    def report_completion(self, job_id: str, result: Dict[str, Any]):
        """Report job completion to broker"""
        try:
            response = requests.post(
                f"{self.broker_url}/complete",
                json={
                    'node_id': self.node_id,
                    'job_id': job_id,
                    'output': result['output'],
                    'error': result['error'],
                    'exit_code': result['exit_code']
                },
                timeout=10
            )
            
            if response.status_code == 200:
                print(f"Job {job_id} completed successfully")
            else:
                print(f"Failed to report completion: {response.text}")
        except Exception as e:
            print(f"Failed to report completion: {e}")
    
    def job_loop(self):
        """Main job execution loop"""
        while self.running:
            try:
                # Claim a job
                job = self.claim_job()
                
                if job:
                    # Execute job
                    result = self.execute_job(job)
                    
                    # Report completion
                    self.report_completion(job['id'], result)
                else:
                    # No jobs available, wait before polling again
                    time.sleep(self.config['poll_interval'])
                    
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Job loop error: {e}")
                time.sleep(self.config['poll_interval'])
    
    def start(self):
        """Start node client"""
        # Check sandrun is accessible
        try:
            response = requests.get(f"{self.sandrun_url}/", timeout=5)
            if response.status_code != 200:
                print(f"Sandrun not accessible at {self.sandrun_url}")
                return False
        except Exception as e:
            print(f"Cannot connect to sandrun: {e}")
            return False
        
        # Register with broker
        if not self.register():
            print("Failed to register with broker")
            return False
        
        self.running = True
        
        # Start heartbeat thread
        heartbeat_thread = threading.Thread(target=self.heartbeat_loop, daemon=True)
        heartbeat_thread.start()
        
        print(f"Node {self.node_id} started")
        print(f"Broker: {self.broker_url}")
        print(f"Sandrun: {self.sandrun_url}")
        print(f"Capabilities: {json.dumps(self.capabilities, indent=2)}")
        print("Polling for jobs...")
        
        # Start job loop
        try:
            self.job_loop()
        except KeyboardInterrupt:
            print("\nShutting down...")
        finally:
            self.running = False
        
        return True

def main():
    parser = argparse.ArgumentParser(description='Sandrun Node Client')
    parser.add_argument('--broker', required=True, help='Broker server URL')
    parser.add_argument('--sandrun', required=True, help='Local sandrun URL')
    parser.add_argument('--config', help='Configuration file')
    
    args = parser.parse_args()
    
    node = SandrunNode(args.broker, args.sandrun, args.config)
    
    if not node.start():
        sys.exit(1)

if __name__ == '__main__':
    main()