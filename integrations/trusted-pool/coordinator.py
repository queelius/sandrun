#!/usr/bin/env python3
"""
Trusted Pool Coordinator

A simple pool coordinator that routes jobs to allowlisted workers.
Workers are trusted based on their Ed25519 public keys.

Usage:
    python coordinator.py --port 9000 --workers workers.json
"""

import asyncio
import json
import time
from typing import Dict, List, Optional
from dataclasses import dataclass, asdict
from pathlib import Path
import argparse
import aiohttp
from aiohttp import web
import logging

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


@dataclass
class Worker:
    """Represents a trusted worker in the pool"""
    worker_id: str  # Base64-encoded Ed25519 public key
    endpoint: str   # HTTP endpoint (e.g., "http://worker1.example.com:8443")
    last_health_check: float = 0
    is_healthy: bool = False
    active_jobs: int = 0
    max_concurrent_jobs: int = 4


@dataclass
class PoolJob:
    """Represents a job in the pool"""
    job_id: str
    worker_id: Optional[str] = None
    status: str = "queued"  # queued, dispatched, running, completed, failed
    submitted_at: float = 0
    completed_at: float = 0


class TrustedPoolCoordinator:
    """
    Coordinates job distribution across trusted workers.

    Trust model:
    - Workers are allowlisted by public key
    - No result verification needed (trusted execution)
    - Health checking ensures worker availability
    """

    def __init__(self, workers_config: List[Dict]):
        self.workers: Dict[str, Worker] = {}
        self.jobs: Dict[str, PoolJob] = {}
        self.job_queue: asyncio.Queue = asyncio.Queue()

        # Load worker allowlist
        for worker_cfg in workers_config:
            worker = Worker(
                worker_id=worker_cfg["worker_id"],
                endpoint=worker_cfg["endpoint"],
                max_concurrent_jobs=worker_cfg.get("max_concurrent_jobs", 4)
            )
            self.workers[worker.worker_id] = worker
            logger.info(f"Added trusted worker: {worker.worker_id[:16]}... at {worker.endpoint}")

    async def health_check_worker(self, worker: Worker) -> bool:
        """Check if worker is healthy"""
        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(f"{worker.endpoint}/health", timeout=aiohttp.ClientTimeout(total=5)) as resp:
                    if resp.status == 200:
                        data = await resp.json()
                        # Verify worker identity matches
                        if data.get("worker_id") == worker.worker_id:
                            worker.is_healthy = True
                            worker.last_health_check = time.time()
                            return True

            worker.is_healthy = False
            return False

        except Exception as e:
            logger.warning(f"Health check failed for {worker.worker_id[:16]}...: {e}")
            worker.is_healthy = False
            return False

    async def health_check_loop(self):
        """Periodically check worker health"""
        while True:
            for worker in self.workers.values():
                await self.health_check_worker(worker)
            await asyncio.sleep(30)  # Check every 30 seconds

    def get_available_worker(self) -> Optional[Worker]:
        """Find an available healthy worker"""
        available = [
            w for w in self.workers.values()
            if w.is_healthy and w.active_jobs < w.max_concurrent_jobs
        ]

        if not available:
            return None

        # Return worker with fewest active jobs (load balancing)
        return min(available, key=lambda w: w.active_jobs)

    async def dispatch_job(self, job: PoolJob, files_data: bytes, manifest: Dict):
        """Dispatch job to an available worker"""
        worker = self.get_available_worker()

        if not worker:
            logger.warning(f"No available workers for job {job.job_id}")
            await asyncio.sleep(5)  # Wait and retry
            await self.job_queue.put((job, files_data, manifest))
            return

        try:
            # Forward job to worker
            async with aiohttp.ClientSession() as session:
                data = aiohttp.FormData()
                data.add_field('files', files_data, filename='project.tar.gz', content_type='application/gzip')
                data.add_field('manifest', json.dumps(manifest), content_type='application/json')

                async with session.post(f"{worker.endpoint}/submit", data=data, timeout=aiohttp.ClientTimeout(total=30)) as resp:
                    if resp.status == 200:
                        result = await resp.json()
                        remote_job_id = result.get("job_id")

                        job.worker_id = worker.worker_id
                        job.status = "dispatched"
                        worker.active_jobs += 1

                        logger.info(f"Dispatched job {job.job_id} to {worker.worker_id[:16]}... (remote: {remote_job_id})")

                        # Store remote job ID for tracking
                        self.jobs[job.job_id].remote_job_id = remote_job_id
                    else:
                        logger.error(f"Worker {worker.worker_id[:16]}... rejected job: {resp.status}")
                        # Re-queue job
                        await self.job_queue.put((job, files_data, manifest))

        except Exception as e:
            logger.error(f"Failed to dispatch job to {worker.worker_id[:16]}...: {e}")
            worker.is_healthy = False
            # Re-queue job
            await self.job_queue.put((job, files_data, manifest))

    async def job_dispatcher_loop(self):
        """Process queued jobs and dispatch to workers"""
        while True:
            job, files_data, manifest = await self.job_queue.get()
            await self.dispatch_job(job, files_data, manifest)

    async def submit_job(self, files_data: bytes, manifest: Dict) -> str:
        """Submit a new job to the pool"""
        import uuid
        job_id = f"pool-{uuid.uuid4().hex[:16]}"

        job = PoolJob(
            job_id=job_id,
            status="queued",
            submitted_at=time.time()
        )
        job.remote_job_id = None  # Will be set when dispatched
        self.jobs[job_id] = job

        # Queue for dispatching
        await self.job_queue.put((job, files_data, manifest))

        logger.info(f"Queued job {job_id}")
        return job_id

    async def get_job_status(self, job_id: str) -> Optional[Dict]:
        """Get status of a job in the pool"""
        if job_id not in self.jobs:
            return None

        job = self.jobs[job_id]

        # If job is dispatched, fetch status from worker
        if job.status in ["dispatched", "running"] and job.worker_id and hasattr(job, 'remote_job_id'):
            worker = self.workers.get(job.worker_id)
            if worker:
                try:
                    async with aiohttp.ClientSession() as session:
                        async with session.get(
                            f"{worker.endpoint}/status/{job.remote_job_id}",
                            timeout=aiohttp.ClientTimeout(total=10)
                        ) as resp:
                            if resp.status == 200:
                                worker_status = await resp.json()

                                # Update local job status
                                job.status = worker_status.get("status", job.status)

                                if job.status in ["completed", "failed"]:
                                    worker.active_jobs = max(0, worker.active_jobs - 1)
                                    job.completed_at = time.time()

                                return {
                                    "job_id": job_id,
                                    "pool_status": job.status,
                                    "worker_id": job.worker_id,
                                    "worker_status": worker_status,
                                    "submitted_at": job.submitted_at,
                                    "completed_at": job.completed_at if job.status in ["completed", "failed"] else None
                                }

                except Exception as e:
                    logger.error(f"Failed to get status from worker: {e}")

        # Return local status
        return {
            "job_id": job_id,
            "pool_status": job.status,
            "worker_id": job.worker_id,
            "submitted_at": job.submitted_at,
            "completed_at": job.completed_at if job.status in ["completed", "failed"] else None
        }

    async def get_job_output(self, job_id: str, output_path: str) -> Optional[bytes]:
        """Get output file from worker"""
        if job_id not in self.jobs:
            return None

        job = self.jobs[job_id]

        if not job.worker_id or not hasattr(job, 'remote_job_id'):
            return None

        worker = self.workers.get(job.worker_id)
        if not worker:
            return None

        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(
                    f"{worker.endpoint}/outputs/{job.remote_job_id}/{output_path}",
                    timeout=aiohttp.ClientTimeout(total=60)
                ) as resp:
                    if resp.status == 200:
                        return await resp.read()
        except Exception as e:
            logger.error(f"Failed to get output from worker: {e}")

        return None


# HTTP API handlers

async def handle_submit(request: web.Request) -> web.Response:
    """Handle job submission"""
    coordinator: TrustedPoolCoordinator = request.app['coordinator']

    try:
        reader = await request.multipart()
        files_data = None
        manifest = None

        async for field in reader:
            if field.name == 'files':
                files_data = await field.read()
            elif field.name == 'manifest':
                manifest_text = await field.text()
                manifest = json.loads(manifest_text)

        if not files_data or not manifest:
            return web.json_response({"error": "Missing files or manifest"}, status=400)

        job_id = await coordinator.submit_job(files_data, manifest)

        return web.json_response({
            "job_id": job_id,
            "status": "queued"
        })

    except Exception as e:
        logger.error(f"Submit error: {e}")
        return web.json_response({"error": str(e)}, status=500)


async def handle_status(request: web.Request) -> web.Response:
    """Handle status request"""
    coordinator: TrustedPoolCoordinator = request.app['coordinator']
    job_id = request.match_info['job_id']

    status = await coordinator.get_job_status(job_id)

    if not status:
        return web.json_response({"error": "Job not found"}, status=404)

    return web.json_response(status)


async def handle_output(request: web.Request) -> web.Response:
    """Handle output download"""
    coordinator: TrustedPoolCoordinator = request.app['coordinator']
    job_id = request.match_info['job_id']
    output_path = request.match_info['path']

    data = await coordinator.get_job_output(job_id, output_path)

    if not data:
        return web.Response(status=404, text="Output not found")

    return web.Response(body=data, content_type='application/octet-stream')


async def handle_pool_status(request: web.Request) -> web.Response:
    """Handle pool status request"""
    coordinator: TrustedPoolCoordinator = request.app['coordinator']

    workers_status = []
    for worker in coordinator.workers.values():
        workers_status.append({
            "worker_id": worker.worker_id,
            "endpoint": worker.endpoint,
            "is_healthy": worker.is_healthy,
            "active_jobs": worker.active_jobs,
            "max_concurrent_jobs": worker.max_concurrent_jobs,
            "last_health_check": worker.last_health_check
        })

    return web.json_response({
        "total_workers": len(coordinator.workers),
        "healthy_workers": sum(1 for w in coordinator.workers.values() if w.is_healthy),
        "total_jobs": len(coordinator.jobs),
        "queued_jobs": coordinator.job_queue.qsize(),
        "workers": workers_status
    })


async def start_background_tasks(app):
    """Start background tasks"""
    coordinator = app['coordinator']
    app['health_check_task'] = asyncio.create_task(coordinator.health_check_loop())
    app['dispatcher_task'] = asyncio.create_task(coordinator.job_dispatcher_loop())


async def cleanup_background_tasks(app):
    """Cleanup background tasks"""
    app['health_check_task'].cancel()
    app['dispatcher_task'].cancel()
    await asyncio.gather(
        app['health_check_task'],
        app['dispatcher_task'],
        return_exceptions=True
    )


def main():
    parser = argparse.ArgumentParser(description="Trusted Pool Coordinator")
    parser.add_argument("--port", type=int, default=9000, help="Port to listen on")
    parser.add_argument("--workers", type=str, required=True, help="Workers config file (JSON)")
    args = parser.parse_args()

    # Load workers config
    with open(args.workers) as f:
        workers_config = json.load(f)

    # Create coordinator
    coordinator = TrustedPoolCoordinator(workers_config)

    # Create web app
    app = web.Application(client_max_size=1024**3)  # 1GB max upload
    app['coordinator'] = coordinator

    # Routes
    app.router.add_post('/submit', handle_submit)
    app.router.add_get('/status/{job_id}', handle_status)
    app.router.add_get('/outputs/{job_id}/{path:.*}', handle_output)
    app.router.add_get('/pool', handle_pool_status)

    # Background tasks
    app.on_startup.append(start_background_tasks)
    app.on_cleanup.append(cleanup_background_tasks)

    logger.info(f"Starting trusted pool coordinator on port {args.port}")
    logger.info(f"Loaded {len(coordinator.workers)} trusted workers")

    web.run_app(app, host='0.0.0.0', port=args.port)


if __name__ == "__main__":
    main()
