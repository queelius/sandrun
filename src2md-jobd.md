# Project: jobd

## Documentation Files

### README.md

# `jobd`: Job Daemon Web Service

The project `jobd` is a web service for script execution with job management. It allows you to upload scripts and additional dependencies, execute them as jobs, monitor their status, retrieve logs, cancel or delete jobs, and download job outputs.

## Workflow

1. **Upload a aggregate file or set of files related to a job**
2. **Execute the job**
3. **Monitor job status and logs**
4. **Cancel or delete jobs**
5. **Download a tarball of a job's working directory**

## Getting Started

To get the server up and running, follow these steps:

1. **Install dependencies:**

    ```bash
    pip install -r requirements.txt
    ```

2. **Run the FastAPI server:**

    ```bash
    uvicorn main:app --reload
    ```

3. **Access the Documentation:**

    Open [http://localhost:8000/docs](http://localhost:8000/docs) for the API documentation.

## API Endpoints

The application provides several API endpoints for managing jobs and executing scripts. See [API Documentation](http://localhost:8000/docs) for detailed information.

### Create a Job

**Endpoint:** `POST /jobs`

**Description:** Upload a set of  files (or aggregate file, e.g. tarball) to create and enqueue a job for execution.

### Get Job Details

**Endpoint:** `GET /jobs/{job_id}`

**Description:** Retrieve details of a specific job.

### Get Job Status

**Endpoint:** `GET /jobs/{job_id}/status`

**Description:** Get the current status of a job.

### Get Job Logs

**Endpoint:** `GET /jobs/{job_id}/logs`

**Description:** Retrieve the logs of a job's execution. Useful for monitoring the executation of jobs.

### Delete a Job

**Endpoint:** `DELETE /jobs/{job_id}`

**Description:** Remove a job from the system.

### List All Jobs

**Endpoint:** `GET /jobs`

**Description:** List all job IDs and their statuses.

### Download Job Tarball

**Endpoint:** `POST /jobs/{job_id}/tarball`

**Description:** Download a tarball of the job's working directory. This is generally how you get the final results of a job.

### Cancel a Job

**Endpoint:** `POST /jobs/{job_id}/cancel`

**Description:** Cancel a running job.

---

### docs/index.md

# `taskd`: Task Daemon for Decentralized Task Execution for Long-Running Tasks

The project `taskd` is a web service for script execution environment with workspace management. It allows you to create a workspace, upload a script, execute the script, and delete the workspace.

## Workflow

1. Create a workspace
2. Upload a script
3. Execute a script
4. Delete a workspace

## Getting Started

To get the server up and running, follow these steps:

1. Install dependencies:

    ```bash
    pip install -r requirements.txt
    ```

2. Install and run `redis-server`:
    - On Linux, you can install Redis using `apt-get install redis-server` and run it using `sudo systemctl start redis-server`
3. Install and run `rq worker`:
    - On Linux, you can install RQ using `pip install rq` and run it using `rq worker`
4. Run `rq-dashboard`:
    - On Linux, you can install RQ Dashboard using `pip install rq-dashboard` and run it using `rq-dashboard`
5. Run `uvicorn main:app --reload`:
    - On Linux, you can install Uvicorn using `pip install uvicorn` and run it using `uvicorn main:app --reload`
6. Open `http://localhost:8000/docs` for the docs

## API Endpoints

The application provides several API endpoints for managing workspaces and executing scripts. These are defined in [`routes.py`](routes.py).

## Script Execution

Scripts are executed in their respective workspaces. The output of the script execution is logged to a file in the workspace. This is handled by the `execute_script` function in [`utils.py`](utils.py).

## Documentation

For more detailed information about the application and its usage, refer to the [docs](docs/index.md).
---

### Source File: `job_queue.py`

#### Source Code

```python
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
            
```

---

### Source File: `routes.py`

#### Source Code

```python
"""
FastAPI routes to manage jobs. Each job is run in isolation under an ephemeral user
in its own directory, with optional secret-based access.
"""

from typing import List, Optional
from fastapi import APIRouter, HTTPException, File, UploadFile, Query, FileResponse
from job_queue import JobQueue, hash_secret
import uuid, tarfile, zipfile, os


router = APIRouter()
queue = JobQueue(max_workers=4)  # Increase workers as needed

script_types = {".py": "python",
                ".sh": "bash",
                ".R": "Rsript",
                ".js": "node"}

@router.post("/jobs", response_model=dict)
async def create_job(
    files: List[UploadFile] = File(..., description="The script and any additional files needed to run the job."),
    main: str = Query(..., description="The main script to run."),
    function: Optional[str] = Query(None, description="The function to run in the script. If None, the script is run as a standalone script."),
    args: Optional[str] = Query(None, description="Arguments to pass to the script or function."),    
    interpreter: Optional[str] = Query("python", description="Interpreter to execute the script (e.g., 'python', 'bash')"),
    secret: Optional[str] = Query(None, description="Optional secret for job confidentiality")
):
    """
    Enqueue a job by uploading files. The main script is required, and should
    be a file in `files`. The interpreter is optional and defaults to being
    based on the file extension. If the script is a standalone script, the
    function is None. If the script is a module, `function` is the name of
    the function to run. Arguments can be passed to the script or function.
    
    Args:
        files: A list of files to upload, including the main script. If the file
        is a single tarball or compressed file, it will be extracted.
        main: The main script to run.
        function: The function to run in the script. If None, the script is run as a standalone script.
        args: Arguments to pass to the script or function.
        interpreter: The command or interpreter to be used (e.g., 'python', 'bash'). Defaults to using the file extension to determine the interpreter.
        secret: Optional secret for confidential access to job details.
    
    Returns:
        A dictionary containing the job ID and a status message.
    Raises:
        HTTPException: If the script type is not supported or no files are uploaded.
    """

    if len(files) == 0:
        raise HTTPException(status_code=400, detail="No files uploaded.")
    
    # Enqueue the job first to get a job_id and directory
    job_id = str(uuid.uuid4())
    job_dir = f"./jobd/jobs/job_{uuid.uuid4().hex}"
    os.makedirs(job_dir, exist_ok=True)
    
    if len(files) == 1 and files[0].filename.endswith((".tar", ".gz", ".zip")):
        # extract the tarball or compressed file
        ext = os.path.splitext(files[0].filename)[1]

        if ext == ".tar":
            with tarfile.open(files[0].file, "r") as tar:
                tar.extractall(job_dir)
        elif ext == ".gz":
            with tarfile.open(files[0].file, "r:gz") as tar:
                tar.extractall(job_dir)
        elif ext == ".zip":
            with zipfile.ZipFile(files[0].file, "r") as zipf:
                zipf.extractall(job_dir)
        else:
            raise HTTPException(status_code=400, detail="Unsupported aggregate file type.")
    else:
        for file in files:
            file_path = os.path.join(job_dir, file.filename)
            with open(file_path, "wb") as f:
                f.write(await file.read())

    if interpreter:
        if interpreter not in script_types.values():
            raise HTTPException(status_code=400, detail="Unsupported interpreter.")
    else:
        ext = os.path.splitext(main)[1]
        if ext not in script_types:
            raise HTTPException(status_code=400, detail=f"Unsupported script extension: {ext}")
        else:
            interpreter = script_types[ext]        

    # Build the command to run the script
    command = [interpreter, main, function, args]
    job_id = queue.enqueue(command, secret)
    return {"message": f"{interpreter} job enqueued",
            "job_id": job_id}


@router.post("/admin/overview", response_model=dict)
async def admin_overview(admin_secret: str = Query(..., description="Admin secret")):
    """
    Retrieve an overview of the job queue, including the number of jobs and their statuses.
    
    Returns:
        A dictionary containing the number of jobs and their statuses.
    """

    if admin_secret != ADMIN_SECRET:
        raise HTTPException(status_code=403, detail="Invalid admin secret.")
    
    # get the number of jobs in each status
    statuses = {}
    for job in queue.jobs.values():
        if job.status in statuses:
            statuses[job.status] += 1
        else:
            statuses[job.status] = 1

    from datetime import datetime as dt

    now = dt.now()
    # get the runtime duration of each job
    runtimes = {}
    for job_id, job in queue.jobs.items():
        if job.status == "completed" or job.status == "canceled" or job.status == "failed":
            runtimes[job_id] = job.end_time - job.start_time
        elif job.status == "running":
            runtimes[job_id] = now - job.start_time
        elif job.status == "queued":
            runtimes[job_id] = now - job.enqueued_time

    # get the total runtime of all jobs
    total_runtime = sum([runtime.total_seconds() for runtime in runtimes.values()])

    return {"queue_size": len(queue.jobs),
            "statuses": statuses,
            "runtimes": runtimes,
            "total_runtime": total_runtime,
            "busy_workers": queue.busy_workers,
            "max_workers": queue.max_workers,
            "free_workers": queue.max_workers - queue.busy_workers,
            "deleted_jobs": queue.deleted_jobs}


@router.post("/jobs/{job_id}", response_model=dict)
async def get_job(job_id: str,
                  secret: Optional[str] = Query(None, description="Job secret")):
    """
    Retrieve a job's details. If the job has a secret, you must provide it.

    Args:
        job_id: The unique ID of the job.
        secret: The secret associated with the job, if any.
    """
    job = queue.get_job(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")
    
    if job.hashed_secret:
        if not secret:
            raise HTTPException(status_code=403, detail="This job is protected by a secret.")
        if hash_secret(secret) != job.hashed_secret:
            raise HTTPException(status_code=403, detail="Invalid secret.")
        
    return job.to_dict()
                                                             

@router.get("/jobs/{job_id}/status", response_model=dict)
async def get_job_status(job_id: str, secret: Optional[str] = Query(None, min_length=8, description="Job secret if set")):
    """
    Retrieve a job's status. If a job has a secret, you must provide it.
    
    Args:
        job_id: The unique ID of the job.
        secret: The secret associated with the job, if any.
    
    Returns:
        A dictionary containing the job's status.
    """
    job = queue.get_job(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")
    
    if job.hashed_secret:
        if not secret:
            raise HTTPException(status_code=403, detail="This job is protected by a secret.")
        if hash_secret(secret) != job.hashed_secret:
            raise HTTPException(status_code=403, detail="Invalid secret.")

    return {"job_id": job.job_id, "status": job.status}

@router.get("/jobs/{job_id}/logs", response_model=dict)
async def get_job_logs(job_id: str, secret: Optional[str] = Query(None, description="Job secret if set")):
    """
    Retrieve a job's logs. If the job has a secret, you must provide it.
    
    Args:
        job_id: The unique ID of the job.
        secret: The secret associated with the job, if any.
    
    Returns:
        A dictionary containing the job's logs.
    """
    job = queue.get_job(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")

    if job.hashed_secret:
        if not secret:
            raise HTTPException(status_code=403, detail="This job is protected by a secret.")
        if hash_secret(secret) != job.hashed_secret:
            raise HTTPException(status_code=403, detail="Invalid secret.")

    return {"job_id": job.job_id, "logs": job.logs}

@router.delete("/jobs/{job_id}", response_model=dict)
async def delete_job(job_id: str, secret: Optional[str] = Query(None, min_length=8, description="Job secret if set"), remove_dir: bool = Query(False, description="Whether to remove the job's directory")):
    """
    Remove a job from the system. If it has a secret, you must provide it.
    Optionally remove its working directory from disk.
    
    Args:
        job_id: The unique ID of the job to delete.
        secret: The secret associated with the job, if any.
        remove_dir: Whether to remove the job's working directory.
    
    Returns:
        A dictionary containing a message and the job ID.
    """
    job = queue.get_job(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")
    
    if job.hashed_secret:
        if not secret:
            raise HTTPException(status_code=403, detail="This job is protected by a secret.")
        if hash_secret(secret) != job.hashed_secret:
            raise HTTPException(status_code=403, detail="Invalid secret.")

    queue.delete_job(job_id)
    
    return {"message": "Job deleted", "job_id": job_id}

@router.get("/jobs", response_model=List[str])
async def list_jobs():
    """
    List all job IDs and their statuses.
    
    Returns:
        A list of job IDs.
    """
    return {job_id: queue.get_job(job_id).status for job_id in queue.jobs.keys()}

@router.post("/jobs/{job_id}/tarball", response_model=File)
async def download_job_tarball(
    job_id: str,
    secret: Optional[str] = Query(None, description="Job secret if set")):
    """
    Download the tarball of a job's working directory. If the job has a secret,
    you must provide it.
    
    Args:
        job_id: The unique ID of the job.
        secret: The secret associated with the job, if any.
    
    Returns:
        A tarball of the job's working directory, i.e., all files and directories
        within the job's working directory and their contents. It will be a
        compressed tarball file that the user downloads with the job's ID.
    """
    job = queue.get_job(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")
    
    if job.hashed_secret:
        if not secret:
            raise HTTPException(status_code=403, detail="This job is protected by a secret.")
        if hash_secret(secret) != job.hashed_secret:
            raise HTTPException(status_code=403, detail="Invalid secret.")
        
    # if job is still running, raise an error
    if job.status == "running":
        raise HTTPException(status_code=400, detail="Job is still running. Please wait for it to complete.")

    tarball_path = job.tarball()
    return FileResponse(tarball_path, filename=f"{job_id}.tar.gz")


@router.post("/jobs/{job_id}/cancel", response_model=dict)
async def cancel_job(job_id: str, secret: Optional[str] = Query(None, description="Job secret if set")):
    """
    Cancel a job. If the job has a secret, you must provide it.
    
    Args:
        job_id: The unique ID of the job.
        secret: The secret associated with the job, if any.
    
    Returns:
        A dictionary containing a message and the job ID.
    """
    job = queue.get_job(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")
    
    if job.hashed_secret:
        if not secret:
            raise HTTPException(status_code=403, detail="This job is protected by a secret.")
        if hash_secret(secret) != job.hashed_secret:
            raise HTTPException(status_code=403, detail="Invalid secret.")
    
    try:
        resp = queue.cancel_job(job_id)
        if not resp:
            raise HTTPException(status_code=500, detail="Failed to cancel job.")
        else:   
            return {"message": "Job canceled", "job_id": job_id}
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))
    
```

---

### Directory: `docs`

