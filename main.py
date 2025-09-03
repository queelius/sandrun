"""
Simple anonymous job execution service with privacy and rate limiting.
No user accounts, just IP-based rate limits and ephemeral execution.
"""

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from contextlib import asynccontextmanager
import os
import shutil
from pathlib import Path

from routes import router
from job_queue import JobQueue

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup
    Path("./jobd/jobs").mkdir(parents=True, exist_ok=True)
    Path("/tmp/jobd").mkdir(parents=True, exist_ok=True)  # For tmpfs mounting
    
    # Cleanup old job directories on startup (privacy)
    for job_dir in Path("./jobd/jobs").glob("job_*"):
        if job_dir.is_dir():
            age = (Path.ctime(Path()) - job_dir.stat().st_ctime) / 3600
            if age > 24:  # Auto-delete after 24 hours
                shutil.rmtree(job_dir, ignore_errors=True)
    
    yield
    
    # Shutdown - cleanup sensitive data
    # Could add more aggressive cleanup here

app = FastAPI(
    title="jobd",
    description="Anonymous job execution service - simple, private, ephemeral",
    version="0.1.0",
    lifespan=lifespan
)

# CORS for web clients
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Anonymous access from anywhere
    allow_methods=["*"],
    allow_headers=["*"],
)

# Include routes
app.include_router(router)

@app.get("/")
async def root():
    return {
        "service": "jobd",
        "status": "running",
        "description": "Anonymous job execution - no accounts required",
        "privacy": "Jobs auto-delete after completion or 24 hours",
        "limits": "Rate limited by IP address"
    }

@app.get("/health")
async def health():
    return {"status": "healthy"}