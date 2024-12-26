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
