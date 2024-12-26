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
    