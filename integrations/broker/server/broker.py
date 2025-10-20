#!/usr/bin/env python3
"""
Sandrun Broker Server - Simple job distribution coordinator
"""

import os
import json
import sqlite3
import hashlib
import threading
from datetime import datetime, timedelta
from typing import Optional, Dict, Any
from flask import Flask, request, jsonify
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# Configuration
DB_PATH = os.environ.get('BROKER_DB', 'broker.db')
JOB_TIMEOUT = int(os.environ.get('JOB_TIMEOUT', 300))  # seconds
RESULT_TTL = int(os.environ.get('RESULT_TTL', 3600))  # seconds
NODE_TIMEOUT = int(os.environ.get('NODE_TIMEOUT', 60))  # seconds
CLEANUP_INTERVAL = 30  # seconds

# Database initialization
def init_db():
    """Initialize database tables"""
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    
    # Jobs table
    c.execute('''
        CREATE TABLE IF NOT EXISTS jobs (
            id TEXT PRIMARY KEY,
            code TEXT NOT NULL,
            interpreter TEXT DEFAULT 'python3',
            args TEXT DEFAULT '[]',
            status TEXT DEFAULT 'pending',
            node_id TEXT,
            output TEXT,
            error TEXT,
            exit_code INTEGER,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            assigned_at TIMESTAMP,
            completed_at TIMESTAMP
        )
    ''')
    
    # Nodes table
    c.execute('''
        CREATE TABLE IF NOT EXISTS nodes (
            id TEXT PRIMARY KEY,
            endpoint TEXT NOT NULL,
            capabilities TEXT DEFAULT '{}',
            last_heartbeat TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            jobs_completed INTEGER DEFAULT 0,
            jobs_failed INTEGER DEFAULT 0,
            active BOOLEAN DEFAULT 1
        )
    ''')
    
    conn.commit()
    conn.close()

def generate_job_id() -> str:
    """Generate unique job ID"""
    timestamp = datetime.now().isoformat()
    random_bytes = os.urandom(8).hex()
    return hashlib.sha256(f"{timestamp}{random_bytes}".encode()).hexdigest()[:16]

def get_db():
    """Get database connection"""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def cleanup_stale_data():
    """Clean up old jobs and dead nodes"""
    conn = get_db()
    c = conn.cursor()
    
    # Mark dead nodes as inactive
    dead_time = datetime.now() - timedelta(seconds=NODE_TIMEOUT)
    c.execute('''
        UPDATE nodes 
        SET active = 0 
        WHERE last_heartbeat < ? AND active = 1
    ''', (dead_time,))
    
    # Reassign jobs from dead nodes
    c.execute('''
        UPDATE jobs 
        SET status = 'pending', node_id = NULL 
        WHERE status IN ('assigned', 'running') 
        AND node_id IN (SELECT id FROM nodes WHERE active = 0)
    ''')
    
    # Delete old completed jobs
    old_time = datetime.now() - timedelta(seconds=RESULT_TTL)
    c.execute('''
        DELETE FROM jobs 
        WHERE status IN ('completed', 'failed') 
        AND completed_at < ?
    ''', (old_time,))
    
    conn.commit()
    conn.close()

# Background cleanup thread
def cleanup_worker():
    """Background thread for cleanup"""
    while True:
        try:
            cleanup_stale_data()
        except Exception as e:
            print(f"Cleanup error: {e}")
        threading.Event().wait(CLEANUP_INTERVAL)

# API Endpoints

@app.route('/submit', methods=['POST'])
def submit_job():
    """Submit new job to queue"""
    data = request.json
    
    if not data or 'code' not in data:
        return jsonify({'error': 'Missing code'}), 400
    
    job_id = generate_job_id()
    
    conn = get_db()
    c = conn.cursor()
    c.execute('''
        INSERT INTO jobs (id, code, interpreter, args, status)
        VALUES (?, ?, ?, ?, 'pending')
    ''', (
        job_id,
        data['code'],
        data.get('interpreter', 'python3'),
        json.dumps(data.get('args', []))
    ))
    conn.commit()
    conn.close()
    
    return jsonify({
        'job_id': job_id,
        'status': 'pending'
    })

@app.route('/status/<job_id>', methods=['GET'])
def get_status(job_id):
    """Get job status"""
    conn = get_db()
    c = conn.cursor()
    c.execute('SELECT * FROM jobs WHERE id = ?', (job_id,))
    job = c.fetchone()
    conn.close()
    
    if not job:
        return jsonify({'error': 'Job not found'}), 404
    
    return jsonify({
        'job_id': job['id'],
        'status': job['status'],
        'node_id': job['node_id'],
        'created_at': job['created_at'],
        'completed_at': job['completed_at']
    })

@app.route('/results/<job_id>', methods=['GET'])
def get_results(job_id):
    """Get job results"""
    conn = get_db()
    c = conn.cursor()
    c.execute('SELECT * FROM jobs WHERE id = ?', (job_id,))
    job = c.fetchone()
    conn.close()
    
    if not job:
        return jsonify({'error': 'Job not found'}), 404
    
    if job['status'] not in ['completed', 'failed']:
        return jsonify({'error': 'Job not completed'}), 202
    
    return jsonify({
        'job_id': job['id'],
        'status': job['status'],
        'output': job['output'],
        'error': job['error'],
        'exit_code': job['exit_code']
    })

@app.route('/nodes', methods=['GET'])
def list_nodes():
    """List registered nodes"""
    conn = get_db()
    c = conn.cursor()
    c.execute('''
        SELECT id, endpoint, capabilities, last_heartbeat, 
               jobs_completed, jobs_failed, active
        FROM nodes 
        WHERE active = 1
        ORDER BY last_heartbeat DESC
    ''')
    nodes = [dict(row) for row in c.fetchall()]
    conn.close()
    
    for node in nodes:
        node['capabilities'] = json.loads(node['capabilities'])
    
    return jsonify({'nodes': nodes})

# Node API (internal)

@app.route('/register', methods=['POST'])
def register_node():
    """Register new node"""
    data = request.json
    
    if not data or 'endpoint' not in data:
        return jsonify({'error': 'Missing endpoint'}), 400
    
    node_id = hashlib.sha256(data['endpoint'].encode()).hexdigest()[:16]
    
    conn = get_db()
    c = conn.cursor()
    c.execute('''
        INSERT OR REPLACE INTO nodes (id, endpoint, capabilities, last_heartbeat, active)
        VALUES (?, ?, ?, CURRENT_TIMESTAMP, 1)
    ''', (
        node_id,
        data['endpoint'],
        json.dumps(data.get('capabilities', {}))
    ))
    conn.commit()
    conn.close()
    
    return jsonify({'node_id': node_id})

@app.route('/heartbeat', methods=['POST'])
def heartbeat():
    """Node heartbeat/keepalive"""
    data = request.json
    
    if not data or 'node_id' not in data:
        return jsonify({'error': 'Missing node_id'}), 400
    
    conn = get_db()
    c = conn.cursor()
    c.execute('''
        UPDATE nodes 
        SET last_heartbeat = CURRENT_TIMESTAMP, active = 1
        WHERE id = ?
    ''', (data['node_id'],))
    conn.commit()
    conn.close()
    
    return jsonify({'status': 'ok'})

@app.route('/claim', methods=['POST'])
def claim_job():
    """Node claims next available job"""
    data = request.json
    
    if not data or 'node_id' not in data:
        return jsonify({'error': 'Missing node_id'}), 400
    
    conn = get_db()
    c = conn.cursor()
    
    # Find next pending job
    c.execute('''
        SELECT id, code, interpreter, args 
        FROM jobs 
        WHERE status = 'pending'
        ORDER BY created_at ASC
        LIMIT 1
    ''')
    job = c.fetchone()
    
    if not job:
        conn.close()
        return jsonify({'job': None})
    
    # Assign job to node
    c.execute('''
        UPDATE jobs 
        SET status = 'assigned', 
            node_id = ?, 
            assigned_at = CURRENT_TIMESTAMP
        WHERE id = ? AND status = 'pending'
    ''', (data['node_id'], job['id']))
    
    if c.rowcount == 0:
        # Job was already claimed
        conn.close()
        return jsonify({'job': None})
    
    conn.commit()
    conn.close()
    
    return jsonify({
        'job': {
            'id': job['id'],
            'code': job['code'],
            'interpreter': job['interpreter'],
            'args': json.loads(job['args'])
        }
    })

@app.route('/complete', methods=['POST'])
def complete_job():
    """Node reports job completion"""
    data = request.json
    
    required = ['node_id', 'job_id', 'output', 'error', 'exit_code']
    if not data or not all(k in data for k in required):
        return jsonify({'error': 'Missing required fields'}), 400
    
    conn = get_db()
    c = conn.cursor()
    
    # Update job with results
    status = 'completed' if data['exit_code'] == 0 else 'failed'
    c.execute('''
        UPDATE jobs 
        SET status = ?, 
            output = ?, 
            error = ?, 
            exit_code = ?,
            completed_at = CURRENT_TIMESTAMP
        WHERE id = ? AND node_id = ?
    ''', (
        status,
        data['output'],
        data['error'],
        data['exit_code'],
        data['job_id'],
        data['node_id']
    ))
    
    # Update node stats
    if status == 'completed':
        c.execute('''
            UPDATE nodes 
            SET jobs_completed = jobs_completed + 1
            WHERE id = ?
        ''', (data['node_id'],))
    else:
        c.execute('''
            UPDATE nodes 
            SET jobs_failed = jobs_failed + 1
            WHERE id = ?
        ''', (data['node_id'],))
    
    conn.commit()
    conn.close()
    
    return jsonify({'status': 'ok'})

@app.route('/health', methods=['GET'])
def health_check():
    """Health check endpoint"""
    try:
        conn = get_db()
        c = conn.cursor()
        c.execute('SELECT COUNT(*) FROM jobs')
        job_count = c.fetchone()[0]
        c.execute('SELECT COUNT(*) FROM nodes WHERE active = 1')
        node_count = c.fetchone()[0]
        conn.close()
        
        return jsonify({
            'status': 'healthy',
            'jobs': job_count,
            'active_nodes': node_count
        })
    except Exception as e:
        return jsonify({
            'status': 'unhealthy',
            'error': str(e)
        }), 500

if __name__ == '__main__':
    # Initialize database
    init_db()
    
    # Start cleanup thread
    cleanup_thread = threading.Thread(target=cleanup_worker, daemon=True)
    cleanup_thread.start()
    
    # Start server
    port = int(os.environ.get('BROKER_PORT', 8000))
    debug = os.environ.get('BROKER_DEBUG', 'false').lower() == 'true'
    
    print(f"Starting Sandrun Broker on port {port}")
    print(f"Database: {DB_PATH}")
    
    app.run(host='0.0.0.0', port=port, debug=debug)