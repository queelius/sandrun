"""
Manages enqueuing and executing jobs using ephemeral users for isolation.
Stores jobs persistently in a JSON file, keyed by their job ID.
"""

import hashlib
import os
import subprocess
import threading
import uuid
import json
import shutil
from typing import Dict, List, Optional
from concurrent.futures import ThreadPoolExecutor

def hash_secret(secret: str) -> str:
    """Hash the secret using SHA-256 for storage."""
    return hashlib.sha256(secret.encode()).hexdigest()

class Job:
    """
    Represents a single job, providing isolation via ephemeral user and unique directory.

    Attributes:
        job_id: Unique identifier for the job.
        ephemeral_user: System user created for isolation.
        directory: The isolated directory in which the job runs.
        command: Command or interpreter + script path to run.
        status: The current status of the job (queued, running, completed, failed).
        logs: Collected logs from the job's execution.
        hashed_secret: Hash of the optional user-supplied secret.
    """
    def __init__(self, command: List[str], secret: Optional[str] = None):
        self.job_id = str(uuid.uuid4())
        self.ephemeral_user = f"job_{uuid.uuid4().hex}"
        self.directory = f"./jobs/job_{uuid.uuid4().hex}"
        self.command = command
        self.status = "queued"
        self.logs = ""
        self.hashed_secret = hash_secret(secret) if secret else None

    def to_dict(self) -> Dict:
        """Convert the Job instance to a dictionary for JSON serialization."""
        return {
            "job_id": self.job_id,
            "ephemeral_user": self.ephemeral_user,
            "directory": self.directory,
            "command": self.command,
            "status": self.status,
            "logs": self.logs,
            "hashed_secret": self.hashed_secret
        }

    @staticmethod
    def from_dict(data: Dict) -> 'Job':
        """Create a Job instance from a dictionary."""
        job = Job(command=data["command"], secret=None)
        job.job_id = data["job_id"]
        job.ephemeral_user = data["ephemeral_user"]
        job.directory = data["directory"]
        job.status = data["status"]
        job.logs = data["logs"]
        job.hashed_secret = data.get("hashed_secret")
        return job
    
    def tarball(self):
        """Create a tarball of the job directory."""
        shutil.make_archive(self.directory, 'gztar', self.directory)
        return f"{self.directory}.tar.gz"

class JobQueue:
    """
    Manages enqueuing and executing jobs using ephemeral users for isolation.
    Stores jobs persistently in a JSON file, keyed by their job ID.
    """
    def __init__(self, max_workers: int = 2, storage_file: str = "./jobd/jobs.json"):
        self.jobs: Dict[str, Job] = {}
        self.lock = threading.Lock()
        self.pool = ThreadPoolExecutor(max_workers=max_workers)
        self.storage_file = storage_file
        self._load_jobs()

    def _load_jobs(self):
        """Load jobs from the JSON storage file."""
        if not os.path.exists(self.storage_file):
            os.makedirs(os.path.dirname(self.storage_file), exist_ok=True)
            with open(self.storage_file, 'w') as f:
                json.dump({}, f)

        with self.lock:
            with open(self.storage_file, 'r') as f:
                data = json.load(f)
                for job_id, job_data in data.items():
                    job = Job.from_dict(job_data)
                    self.jobs[job_id] = job
                    if job.status in ["queued", "running"]:
                        # Retry running jobs that were not completed
                        self.pool.submit(self._run_job, job_id)

    def _save_jobs(self):
        """Save all jobs to the JSON storage file."""
        with self.lock:
            data = {job_id: job.to_dict() for job_id, job in self.jobs.items()}
            with open(self.storage_file, 'w') as f:
                json.dump(data, f, indent=4)

    def enqueue(self, command: List[str], secret: Optional[str]) -> str:
        """
        Enqueue a job. Creates an ephemeral user and a unique directory for isolation.

        Args:
            command: List of command arguments (e.g., ["python", "script.py"]).
            secret: Optional secret for confidential access to job details.

        Returns:
            The unique job ID assigned to the enqueued job.
        """
        job = Job(command=command, secret=secret)
        with self.lock:
            self.jobs[job.job_id] = job
            self._save_jobs()

        os.makedirs(job.directory, exist_ok=True)
        self._create_user(job.ephemeral_user)
        self.pool.submit(self._run_job, job.job_id)
        return job.job_id

    def _run_job(self, job_id: str):
        """
        Internal method to run the job under its ephemeral user, then clean up the user.

        Args:
            job_id: The ID of the job to run.
        """
        with self.lock:
            job = self.jobs[job_id]
            job.status = "running"
            self._save_jobs()

        try:
            run_command = ["sudo", "-u", job.ephemeral_user] + job.command
            process = subprocess.Popen(
                run_command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                cwd=job.directory
            )
            stdout, _ = process.communicate()
            exit_code = process.returncode
            job.logs = stdout

            if exit_code == 0:
                job.status = "completed"
            else:
                job.status = "failed"
                job.logs += "\n[Error] Non-zero return code."
        except Exception as e:
            job.status = "failed"
            job.logs += f"\n[Exception] {e}"
        finally:
            with self.lock:
                self._save_jobs()
            self._delete_user(job.ephemeral_user)

    def _create_user(self, username: str):
        """
        Create an ephemeral system user without a home directory or valid shell.

        Args:
            username: The name of the user to create.
        """
        subprocess.run(["sudo", "useradd", "-M", "-s", "/usr/sbin/nologin", username], check=True)

    def _delete_user(self, username: str):
        """
        Delete the ephemeral system user.

        Args:
            username: The name of the user to delete.
        """
        subprocess.run(["sudo", "userdel", "-r", username], check=False)

    def get_job(self, job_id: str) -> Optional[Job]:
        """
        Retrieve the job object by its ID.

        Args:
            job_id: The unique ID of the job.

        Returns:
            The Job object if found, else None.
        """
        with self.lock:
            return self.jobs.get(job_id)

    def delete_job(self, job_id: str) -> None:
        """
        Delete a job from the queue and persist the change.

        Args:
            job_id: The unique ID of the job to delete.
        """
        with self.lock:
            job = self.jobs.get(job_id)
            if not job:
                raise ValueError("Job not found.")
            elif job.status == "running":
                raise ValueError("Cannot delete a running job. Cancel it first.")
            else:
                try:
                    del self.jobs[job_id]
                    self._save_jobs()
                    if os.path.exists(job.directory):
                        shutil.rmtree(job.directory, ignore_errors=True)
                except Exception as e:
                    raise ValueError(f"Error deleting job: {e}")

    def cancel_job(self, job_id: str) -> None:
        """
        Cancel a running job by terminating the process.

        Args:
            job_id: The unique ID of the job to cancel.
        """
        with self.lock:
            job = self.jobs.get(job_id)
            if not job:
                raise ValueError("Job not found.")
            elif job.status != "running":
                raise ValueError("Job is not running.")
            else:
                try:
                    # Terminate the process
                    resp = subprocess.run(["sudo", "pkill", "-u", job.ephemeral_user], check=False)
                    if resp.returncode != 0:
                        raise ValueError("Failed to cancel job.")
                    job.status = "cancelled"
                    self._save_jobs()
                except Exception as e:
                    raise ValueError(f"Error canceling job: {e}")
                
    def restart_job(self, job_id: str) -> None:
        """
        Restart a cancelled job.

        Args:
            job_id: The unique ID of the job to restart.
        """
        with self.lock:
            job = self.jobs.get(job_id)
            if not job:
                raise ValueError("Job not found.")
            elif job.status != "cancelled":
                raise ValueError("Job is not cancelled.")
            
            job.status = "queued"
            self._save_jobs()
            os.makedirs(job.directory, exist_ok=True)
            self._create_user(job.ephemeral_user)
            self.pool.submit(self._run_job, job.job_id)
            