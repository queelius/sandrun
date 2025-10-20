#!/usr/bin/env python3
"""
Example client for submitting jobs to the broker
"""

import sys
import time
import requests
import argparse

def submit_job(broker_url: str, code: str, interpreter: str = 'python3'):
    """Submit a job to the broker"""
    response = requests.post(
        f"{broker_url}/submit",
        json={
            'code': code,
            'interpreter': interpreter
        }
    )
    
    if response.status_code == 200:
        return response.json()['job_id']
    else:
        print(f"Failed to submit job: {response.text}")
        return None

def wait_for_completion(broker_url: str, job_id: str, timeout: int = 60):
    """Wait for job to complete and get results"""
    start_time = time.time()
    
    while time.time() - start_time < timeout:
        # Check status
        status_response = requests.get(f"{broker_url}/status/{job_id}")
        
        if status_response.status_code == 200:
            status = status_response.json()
            
            if status['status'] in ['completed', 'failed']:
                # Get results
                results_response = requests.get(f"{broker_url}/results/{job_id}")
                
                if results_response.status_code == 200:
                    return results_response.json()
                elif results_response.status_code == 202:
                    # Still processing
                    pass
                else:
                    print(f"Failed to get results: {results_response.text}")
                    return None
        
        print(f"Job {job_id} status: {status['status']}")
        time.sleep(2)
    
    print("Timeout waiting for job completion")
    return None

def main():
    parser = argparse.ArgumentParser(description='Submit job to broker')
    parser.add_argument('--broker', default='http://localhost:8000', 
                       help='Broker server URL')
    parser.add_argument('--code', help='Python code to execute')
    parser.add_argument('--file', help='File containing code to execute')
    parser.add_argument('--interpreter', default='python3',
                       help='Interpreter to use')
    
    args = parser.parse_args()
    
    # Get code
    if args.code:
        code = args.code
    elif args.file:
        with open(args.file, 'r') as f:
            code = f.read()
    else:
        # Default test code
        code = '''
import sys
import platform

print("Hello from distributed sandrun!")
print(f"Python version: {sys.version}")
print(f"Platform: {platform.platform()}")

# Some computation
result = sum(i**2 for i in range(1000))
print(f"Sum of squares (0-999): {result}")
'''
    
    print("Submitting job to broker...")
    print(f"Code:\n{code}\n")
    
    # Submit job
    job_id = submit_job(args.broker, code, args.interpreter)
    
    if not job_id:
        sys.exit(1)
    
    print(f"Job submitted with ID: {job_id}")
    print("Waiting for completion...")
    
    # Wait for results
    results = wait_for_completion(args.broker, job_id)
    
    if results:
        print("\n=== Job Results ===")
        print(f"Status: {results['status']}")
        print(f"Exit code: {results['exit_code']}")
        
        if results['output']:
            print("\nOutput:")
            print(results['output'])
        
        if results['error']:
            print("\nError:")
            print(results['error'])
    else:
        print("Failed to get results")
        sys.exit(1)

if __name__ == '__main__':
    main()