#!/usr/bin/env python3
"""
Sandrun MCP Server
==================

A Model Context Protocol server that provides safe code execution through Sandrun.

This allows LLMs like Claude to execute Python/Node/Bash code in a secure,
isolated sandbox with automatic resource limits and cleanup.

Tools provided:
- execute_python: Execute Python code
- execute_javascript: Execute JavaScript/Node.js code
- execute_bash: Execute Bash commands
- check_job_status: Check execution status
- get_job_logs: Get execution output

Example usage in Claude:
    "Can you calculate the fibonacci sequence up to 100?"
    Claude will use execute_python to run the code and return results.
"""

import asyncio
import json
import time
from typing import Any, Sequence
import httpx
from mcp.server import Server
from mcp.types import Tool, TextContent, ImageContent, EmbeddedResource
import mcp.server.stdio


# Sandrun server configuration
SANDRUN_URL = "http://localhost:8443"
DEFAULT_TIMEOUT = 30  # seconds to wait for job completion


class SandrunMCPServer:
    """MCP Server that wraps Sandrun for safe code execution."""

    def __init__(self, sandrun_url: str = SANDRUN_URL):
        self.sandrun_url = sandrun_url
        self.server = Server("sandrun")
        self.setup_handlers()

    def setup_handlers(self):
        """Register MCP tool handlers."""

        @self.server.list_tools()
        async def list_tools() -> list[Tool]:
            """List available code execution tools."""
            return [
                Tool(
                    name="execute_python",
                    description="""Execute Python code in a secure sandbox.

                    The code runs with:
                    - 512MB RAM limit
                    - 5 minute timeout
                    - No network access
                    - Isolated filesystem (tmpfs only)
                    - Auto-cleanup after execution

                    Returns stdout, stderr, and exit code.
                    Perfect for data analysis, calculations, file processing, etc.""",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "code": {
                                "type": "string",
                                "description": "Python code to execute"
                            },
                            "wait": {
                                "type": "boolean",
                                "description": "Wait for completion (default: true)",
                                "default": True
                            }
                        },
                        "required": ["code"]
                    }
                ),
                Tool(
                    name="execute_javascript",
                    description="""Execute JavaScript/Node.js code in a secure sandbox.

                    Same security features as Python execution.
                    Useful for JS-specific tasks, JSON processing, etc.""",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "code": {
                                "type": "string",
                                "description": "JavaScript/Node.js code to execute"
                            },
                            "wait": {
                                "type": "boolean",
                                "description": "Wait for completion (default: true)",
                                "default": True
                            }
                        },
                        "required": ["code"]
                    }
                ),
                Tool(
                    name="execute_bash",
                    description="""Execute Bash commands in a secure sandbox.

                    Useful for file operations, text processing with standard Unix tools.
                    Same isolation and resource limits apply.""",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "script": {
                                "type": "string",
                                "description": "Bash script to execute"
                            },
                            "wait": {
                                "type": "boolean",
                                "description": "Wait for completion (default: true)",
                                "default": True
                            }
                        },
                        "required": ["script"]
                    }
                ),
                Tool(
                    name="check_job_status",
                    description="Check the status of a submitted job",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "job_id": {
                                "type": "string",
                                "description": "Job ID from execute_* call"
                            }
                        },
                        "required": ["job_id"]
                    }
                ),
                Tool(
                    name="get_job_logs",
                    description="Get stdout/stderr from a completed job",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "job_id": {
                                "type": "string",
                                "description": "Job ID to get logs for"
                            }
                        },
                        "required": ["job_id"]
                    }
                )
            ]

        @self.server.call_tool()
        async def call_tool(name: str, arguments: Any) -> Sequence[TextContent | ImageContent | EmbeddedResource]:
            """Handle tool calls."""

            if name == "execute_python":
                return await self.execute_python(
                    arguments.get("code", ""),
                    arguments.get("wait", True)
                )

            elif name == "execute_javascript":
                return await self.execute_javascript(
                    arguments.get("code", ""),
                    arguments.get("wait", True)
                )

            elif name == "execute_bash":
                return await self.execute_bash(
                    arguments.get("script", ""),
                    arguments.get("wait", True)
                )

            elif name == "check_job_status":
                return await self.check_status(arguments["job_id"])

            elif name == "get_job_logs":
                return await self.get_logs(arguments["job_id"])

            else:
                raise ValueError(f"Unknown tool: {name}")

    async def submit_job(self, code: str, interpreter: str) -> dict:
        """Submit code to Sandrun for execution."""
        async with httpx.AsyncClient() as client:
            # Create the "fake tar" format that Sandrun expects from quick code
            fake_tar = f"----Tar\nPath: main.{self._get_extension(interpreter)}\nSize: {len(code)}\n\n{code}"

            # Submit job
            response = await client.post(
                f"{self.sandrun_url}/submit",
                files={"files": ("job.tar.gz", fake_tar)},
                data={"manifest": json.dumps({
                    "entrypoint": f"main.{self._get_extension(interpreter)}",
                    "interpreter": interpreter
                })}
            )
            response.raise_for_status()
            return response.json()

    async def wait_for_completion(self, job_id: str, timeout: int = DEFAULT_TIMEOUT) -> dict:
        """Wait for job to complete and return results."""
        async with httpx.AsyncClient() as client:
            start = time.time()

            while time.time() - start < timeout:
                # Check status
                status_resp = await client.get(f"{self.sandrun_url}/status/{job_id}")
                status_data = status_resp.json()

                if "error" in status_data:
                    return {"error": status_data["error"]}

                if status_data["status"] in ["completed", "failed"]:
                    # Get logs
                    logs_resp = await client.get(f"{self.sandrun_url}/logs/{job_id}")
                    logs_data = logs_resp.json()

                    return {
                        "status": status_data["status"],
                        "stdout": logs_data.get("stdout", ""),
                        "stderr": logs_data.get("stderr", ""),
                        "metrics": status_data.get("metrics", {})
                    }

                # Wait before polling again
                await asyncio.sleep(0.5)

            return {"error": "Timeout waiting for job completion"}

    async def execute_python(self, code: str, wait: bool = True) -> Sequence[TextContent]:
        """Execute Python code."""
        result = await self.submit_job(code, "python3")
        job_id = result.get("job_id")

        if not job_id:
            return [TextContent(type="text", text=f"Error: {result.get('error', 'Unknown error')}")]

        if not wait:
            return [TextContent(
                type="text",
                text=f"Job submitted: {job_id}\nUse check_job_status to monitor progress."
            )]

        # Wait for completion
        output = await self.wait_for_completion(job_id)

        if "error" in output:
            return [TextContent(type="text", text=f"Error: {output['error']}")]

        # Format output
        text = f"Status: {output['status']}\n\n"

        if output.get("stdout"):
            text += f"Output:\n{output['stdout']}\n"

        if output.get("stderr"):
            text += f"\nErrors:\n{output['stderr']}\n"

        metrics = output.get("metrics", {})
        text += f"\nMetrics: CPU={metrics.get('cpu_seconds', 0):.3f}s, Memory={metrics.get('memory_mb', 0)}MB"

        return [TextContent(type="text", text=text)]

    async def execute_javascript(self, code: str, wait: bool = True) -> Sequence[TextContent]:
        """Execute JavaScript/Node.js code."""
        result = await self.submit_job(code, "node")
        job_id = result.get("job_id")

        if not job_id:
            return [TextContent(type="text", text=f"Error: {result.get('error', 'Unknown error')}")]

        if not wait:
            return [TextContent(
                type="text",
                text=f"Job submitted: {job_id}\nUse check_job_status to monitor progress."
            )]

        output = await self.wait_for_completion(job_id)

        if "error" in output:
            return [TextContent(type="text", text=f"Error: {output['error']}")]

        text = f"Status: {output['status']}\n\n"
        if output.get("stdout"):
            text += f"Output:\n{output['stdout']}\n"
        if output.get("stderr"):
            text += f"\nErrors:\n{output['stderr']}\n"

        metrics = output.get("metrics", {})
        text += f"\nMetrics: CPU={metrics.get('cpu_seconds', 0):.3f}s, Memory={metrics.get('memory_mb', 0)}MB"

        return [TextContent(type="text", text=text)]

    async def execute_bash(self, script: str, wait: bool = True) -> Sequence[TextContent]:
        """Execute Bash script."""
        result = await self.submit_job(script, "bash")
        job_id = result.get("job_id")

        if not job_id:
            return [TextContent(type="text", text=f"Error: {result.get('error', 'Unknown error')}")]

        if not wait:
            return [TextContent(
                type="text",
                text=f"Job submitted: {job_id}\nUse check_job_status to monitor progress."
            )]

        output = await self.wait_for_completion(job_id)

        if "error" in output:
            return [TextContent(type="text", text=f"Error: {output['error']}")]

        text = f"Status: {output['status']}\n\n"
        if output.get("stdout"):
            text += f"Output:\n{output['stdout']}\n"
        if output.get("stderr"):
            text += f"\nErrors:\n{output['stderr']}\n"

        return [TextContent(type="text", text=text)]

    async def check_status(self, job_id: str) -> Sequence[TextContent]:
        """Check job status."""
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{self.sandrun_url}/status/{job_id}")
            data = response.json()

            if "error" in data:
                return [TextContent(type="text", text=f"Error: {data['error']}")]

            text = f"Job {job_id}:\n"
            text += f"Status: {data['status']}\n"
            text += f"Queue position: {data.get('queue_position', 'N/A')}\n"

            metrics = data.get("metrics", {})
            text += f"CPU: {metrics.get('cpu_seconds', 0):.3f}s\n"
            text += f"Memory: {metrics.get('memory_mb', 0)}MB\n"

            return [TextContent(type="text", text=text)]

    async def get_logs(self, job_id: str) -> Sequence[TextContent]:
        """Get job logs."""
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{self.sandrun_url}/logs/{job_id}")
            data = response.json()

            if "error" in data:
                return [TextContent(type="text", text=f"Error: {data['error']}")]

            text = f"Logs for job {job_id}:\n\n"

            if data.get("stdout"):
                text += f"STDOUT:\n{data['stdout']}\n"

            if data.get("stderr"):
                text += f"\nSTDERR:\n{data['stderr']}\n"

            return [TextContent(type="text", text=text)]

    @staticmethod
    def _get_extension(interpreter: str) -> str:
        """Get file extension for interpreter."""
        extensions = {
            "python3": "py",
            "python": "py",
            "node": "js",
            "bash": "sh",
            "sh": "sh"
        }
        return extensions.get(interpreter, "txt")

    async def run(self):
        """Run the MCP server."""
        async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
            await self.server.run(
                read_stream,
                write_stream,
                self.server.create_initialization_options()
            )


async def main():
    """Main entry point."""
    server = SandrunMCPServer()
    await server.run()


if __name__ == "__main__":
    asyncio.run(main())
