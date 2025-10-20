#!/usr/bin/env python3
"""
Sandrun Admin Console

A shell-like interface for managing sandrun instances.
Provides a virtual filesystem abstraction over the HTTP API.
"""

import cmd
import sys
import requests
from pathlib import Path
from typing import Optional, List
import json
from datetime import datetime
import shlex


class SandrunConsole(cmd.Cmd):
    """Interactive shell for managing sandrun instances"""

    intro = """
╔═══════════════════════════════════════╗
║   Sandrun Admin Console v1.0          ║
║   Type 'help' or '?' for commands     ║
╚═══════════════════════════════════════╝
"""
    prompt = 'sandrun> '

    def __init__(self, base_url: str, skip_connection_test: bool = False):
        super().__init__()
        self.base_url = base_url.rstrip('/')
        self.cwd = Path('/')
        self.session = requests.Session()

        # Test connection (skip for testing)
        if not skip_connection_test:
            try:
                resp = self.session.get(f'{self.base_url}/', timeout=5)
                resp.raise_for_status()
                print(f"✅ Connected to {self.base_url}")
            except Exception as e:
                print(f"❌ Failed to connect to {self.base_url}: {e}")
                sys.exit(1)

    def _resolve_path(self, arg: str) -> Path:
        """Resolve relative path to absolute"""
        if not arg:
            return self.cwd

        path = Path(arg)
        if path.is_absolute():
            return path
        return (self.cwd / path).resolve()

    def _format_job_list(self, jobs: List[dict]) -> str:
        """Format job list for display"""
        if not jobs:
            return "No jobs found"

        lines = []
        for job in jobs:
            status_color = {
                'queued': '\033[93m',     # Yellow
                'running': '\033[94m',    # Blue
                'completed': '\033[92m',  # Green
                'failed': '\033[91m'      # Red
            }.get(job.get('status', ''), '')

            reset = '\033[0m'

            job_id = job.get('job_id', 'unknown')
            status = job.get('status', 'unknown')
            age = job.get('age', '?')

            lines.append(f"{job_id}  {status_color}[{status}]{reset}  {age}")

        return '\n'.join(lines)

    # Navigation commands

    def do_cd(self, arg):
        """Change directory: cd <path>"""
        try:
            new_path = self._resolve_path(arg)
            self.cwd = new_path
            self.prompt = f'sandrun:{self.cwd}> '
        except Exception as e:
            print(f"cd: {e}")

    def do_pwd(self, arg):
        """Print working directory"""
        print(self.cwd)

    def do_ls(self, arg):
        """List directory contents: ls [path]"""
        path = self._resolve_path(arg)

        try:
            if path == Path('/'):
                # Root directory
                print("jobs/")
                print("config/")
                print("stats/")
                print("pool/")

            elif path == Path('/jobs'):
                # List all jobs
                # Note: Sandrun doesn't have a /jobs endpoint yet
                # For now, show placeholder
                print("(Job listing requires /jobs API endpoint)")
                print("Use: status <job_id> to check individual jobs")

            elif path.parent == Path('/jobs'):
                # List job contents
                job_id = path.name
                print("manifest.json")
                print("status.json")
                print("logs/")
                print("outputs/")

            elif path == Path('/config'):
                print("rate_limits.json")
                print("environments.json")

            elif path == Path('/stats'):
                print("quotas.json")
                print("system.json")

            elif path == Path('/pool'):
                print("workers/")
                print("jobs/")
                print("status")

            else:
                print(f"ls: {path}: No such file or directory")

        except Exception as e:
            print(f"ls: {e}")

    # File operations

    def do_cat(self, arg):
        """Display file contents: cat <file>"""
        path = self._resolve_path(arg)

        try:
            if path.parent.parent == Path('/jobs') and path.name == 'status.json':
                # Get job status
                job_id = path.parent.name
                resp = self.session.get(f'{self.base_url}/status/{job_id}')
                resp.raise_for_status()
                print(json.dumps(resp.json(), indent=2))

            elif path == Path('/stats/system.json'):
                # Get system stats
                resp = self.session.get(f'{self.base_url}/stats')
                resp.raise_for_status()
                print(json.dumps(resp.json(), indent=2))

            elif path == Path('/config/environments.json'):
                # Get environments
                resp = self.session.get(f'{self.base_url}/environments')
                resp.raise_for_status()
                print(json.dumps(resp.json(), indent=2))

            else:
                print(f"cat: {path}: No such file or directory")

        except requests.HTTPError as e:
            print(f"cat: HTTP {e.response.status_code}: {e.response.text}")
        except Exception as e:
            print(f"cat: {e}")

    def do_tail(self, arg):
        """Stream file (logs): tail <file>"""
        path = self._resolve_path(arg)

        try:
            # Check if this is a log file
            if 'logs' in path.parts and path.parent.parent.parent == Path('/jobs'):
                job_id = path.parent.parent.name
                resp = self.session.get(f'{self.base_url}/logs/{job_id}')
                resp.raise_for_status()
                print(resp.text)
            else:
                print(f"tail: {path}: Not a log file")

        except requests.HTTPError as e:
            print(f"tail: HTTP {e.response.status_code}: {e.response.text}")
        except Exception as e:
            print(f"tail: {e}")

    # Job management

    def do_status(self, arg):
        """Get job status: status <job_id>"""
        if not arg:
            print("Usage: status <job_id>")
            return

        try:
            resp = self.session.get(f'{self.base_url}/status/{arg}')
            resp.raise_for_status()
            print(json.dumps(resp.json(), indent=2))
        except requests.HTTPError as e:
            print(f"status: HTTP {e.response.status_code}: {e.response.text}")
        except Exception as e:
            print(f"status: {e}")

    def do_logs(self, arg):
        """Get job logs: logs <job_id>"""
        if not arg:
            print("Usage: logs <job_id>")
            return

        try:
            resp = self.session.get(f'{self.base_url}/logs/{arg}')
            resp.raise_for_status()
            print(resp.text)
        except requests.HTTPError as e:
            print(f"logs: HTTP {e.response.status_code}: {e.response.text}")
        except Exception as e:
            print(f"logs: {e}")

    def do_submit(self, arg):
        """Submit job: submit <tarball> [manifest_json]"""
        args = shlex.split(arg)
        if len(args) < 1:
            print("Usage: submit <tarball> [manifest_json]")
            print("Example: submit job.tar.gz '{\"entrypoint\":\"main.py\",\"interpreter\":\"python3\"}'")
            return

        tarball = args[0]
        manifest = args[1] if len(args) > 1 else '{}'

        try:
            with open(tarball, 'rb') as f:
                files = {'files': f}
                data = {'manifest': manifest}
                resp = self.session.post(f'{self.base_url}/submit', files=files, data=data)
                resp.raise_for_status()
                result = resp.json()
                print(f"✅ Job submitted: {result.get('job_id')}")
                print(f"   Status: {result.get('status')}")
        except FileNotFoundError:
            print(f"submit: {tarball}: No such file")
        except requests.HTTPError as e:
            print(f"submit: HTTP {e.response.status_code}: {e.response.text}")
        except Exception as e:
            print(f"submit: {e}")

    def do_download(self, arg):
        """Download output file: download <job_id> <filepath> [output_file]"""
        args = shlex.split(arg)
        if len(args) < 2:
            print("Usage: download <job_id> <filepath> [output_file]")
            return

        job_id = args[0]
        filepath = args[1]
        output_file = args[2] if len(args) > 2 else filepath.split('/')[-1]

        try:
            resp = self.session.get(f'{self.base_url}/download/{job_id}/{filepath}')
            resp.raise_for_status()

            with open(output_file, 'wb') as f:
                f.write(resp.content)

            print(f"✅ Downloaded {filepath} → {output_file}")
        except requests.HTTPError as e:
            print(f"download: HTTP {e.response.status_code}: {e.response.text}")
        except Exception as e:
            print(f"download: {e}")

    # System commands

    def do_stats(self, arg):
        """Show system statistics"""
        try:
            resp = self.session.get(f'{self.base_url}/stats')
            resp.raise_for_status()
            data = resp.json()

            print("\n═══ Your Quota ═══")
            quota = data.get('your_quota', {})
            print(f"  Used:      {quota.get('used', 0):.2f} / {quota.get('limit', 0):.2f} CPU-sec")
            print(f"  Available: {quota.get('available', 0):.2f} CPU-sec")
            print(f"  Active:    {quota.get('active_jobs', 0)} jobs")
            print(f"  Can submit: {'✅ Yes' if quota.get('can_submit') else '❌ No'}")

            print("\n═══ System Status ═══")
            system = data.get('system', {})
            print(f"  Queue length: {system.get('queue_length', 0)}")
            print(f"  Active jobs:  {system.get('active_jobs', 0)}")
            print()

        except Exception as e:
            print(f"stats: {e}")

    def do_environments(self, arg):
        """List available environments"""
        try:
            resp = self.session.get(f'{self.base_url}/environments')
            resp.raise_for_status()
            data = resp.json()

            print("\n═══ Available Environments ═══")
            for env in data.get('templates', []):
                print(f"  • {env}")

            print("\n═══ Environment Stats ═══")
            stats = data.get('stats', {})
            print(f"  Templates: {stats.get('total_templates', 0)}")
            print(f"  Cached:    {stats.get('cached_environments', 0)}")
            print(f"  Uses:      {stats.get('total_uses', 0)}")
            print(f"  Disk:      {stats.get('disk_usage_mb', 0)} MB")
            print()

        except Exception as e:
            print(f"environments: {e}")

    def do_health(self, arg):
        """Check server health"""
        try:
            resp = self.session.get(f'{self.base_url}/health')
            resp.raise_for_status()
            data = resp.json()

            status = data.get('status', 'unknown')
            worker_id = data.get('worker_id')

            if status == 'healthy':
                print(f"✅ Server is healthy")
            else:
                print(f"⚠️  Server status: {status}")

            if worker_id:
                print(f"   Worker ID: {worker_id[:32]}...")

        except Exception as e:
            print(f"health: {e}")

    # Utility commands

    def do_clear(self, arg):
        """Clear screen"""
        print('\033[2J\033[H', end='')

    def do_exit(self, arg):
        """Exit console"""
        print("Goodbye!")
        return True

    def do_quit(self, arg):
        """Exit console"""
        return self.do_exit(arg)

    def do_EOF(self, arg):
        """Exit on Ctrl+D"""
        print()
        return self.do_exit(arg)

    def emptyline(self):
        """Do nothing on empty line"""
        pass


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Sandrun Admin Console')
    parser.add_argument('url', nargs='?', default='http://localhost:8443',
                        help='Sandrun server URL (default: http://localhost:8443)')
    args = parser.parse_args()

    console = SandrunConsole(args.url)
    console.cmdloop()


if __name__ == '__main__':
    main()
