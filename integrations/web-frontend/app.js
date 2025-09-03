// Sandrun Web Frontend - JavaScript Application

class SandrunClient {
    constructor() {
        this.serverUrl = localStorage.getItem('sandrun-server') || 'http://localhost:8443';
        this.currentJob = null;
        this.files = new Map();
        this.activeJobs = new Map();
        this.pollInterval = null;
        
        this.init();
    }
    
    init() {
        // Set server URL
        document.getElementById('server-url').value = this.serverUrl;
        
        // Test connection on load
        this.testConnection();
        
        // Setup event listeners
        this.setupEventListeners();
        
        // Load any active jobs from localStorage
        this.loadActiveJobs();
    }
    
    setupEventListeners() {
        // Server configuration
        document.getElementById('server-url').addEventListener('change', (e) => {
            this.serverUrl = e.target.value;
            localStorage.setItem('sandrun-server', this.serverUrl);
        });
        
        document.getElementById('test-connection').addEventListener('click', () => {
            this.testConnection();
        });
        
        // Tab switching
        document.querySelectorAll('.tab').forEach(tab => {
            tab.addEventListener('click', (e) => {
                const tabName = e.target.dataset.tab || e.target.dataset.output;
                this.switchTab(e.target, tabName);
            });
        });
        
        // File upload
        const dropZone = document.getElementById('drop-zone');
        const fileInput = document.getElementById('file-input');
        
        dropZone.addEventListener('click', () => fileInput.click());
        
        dropZone.addEventListener('dragover', (e) => {
            e.preventDefault();
            dropZone.classList.add('dragging');
        });
        
        dropZone.addEventListener('dragleave', () => {
            dropZone.classList.remove('dragging');
        });
        
        dropZone.addEventListener('drop', (e) => {
            e.preventDefault();
            dropZone.classList.remove('dragging');
            this.handleFiles(e.dataTransfer.items);
        });
        
        fileInput.addEventListener('change', (e) => {
            this.handleFiles(e.target.files);
        });
        
        // Job submission
        document.getElementById('submit-btn').addEventListener('click', () => {
            this.submitJob();
        });
        
        // Job details actions
        document.getElementById('download-btn').addEventListener('click', () => {
            this.downloadOutputs();
        });
        
        document.getElementById('new-job-btn').addEventListener('click', () => {
            this.resetToSubmit();
        });
        
        // API docs modal
        const modal = document.getElementById('api-modal');
        document.getElementById('api-docs-link').addEventListener('click', (e) => {
            e.preventDefault();
            modal.style.display = 'block';
        });
        
        document.querySelector('.close').addEventListener('click', () => {
            modal.style.display = 'none';
        });
        
        window.addEventListener('click', (e) => {
            if (e.target === modal) {
                modal.style.display = 'none';
            }
        });
    }
    
    async testConnection() {
        const indicator = document.getElementById('connection-status');
        indicator.className = 'status-indicator';
        
        try {
            const response = await fetch(this.serverUrl);
            const data = await response.json();
            
            if (data.service === 'sandrun') {
                indicator.classList.add('connected');
                this.showMessage('Connected to Sandrun server', 'success');
            } else {
                throw new Error('Invalid server response');
            }
        } catch (error) {
            indicator.classList.add('error');
            this.showMessage('Failed to connect to server', 'error');
        }
    }
    
    switchTab(tabElement, tabName) {
        const parent = tabElement.parentElement;
        const isOutput = parent.querySelector('[data-output]');
        
        // Update active tab
        parent.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        tabElement.classList.add('active');
        
        if (isOutput) {
            // Output tabs
            document.querySelectorAll('.log-output, .files-list').forEach(content => {
                content.classList.remove('active');
            });
            document.getElementById(`${tabName}-output`).classList.add('active');
        } else {
            // Main tabs
            document.querySelectorAll('.tab-content').forEach(content => {
                content.classList.remove('active');
            });
            document.getElementById(`${tabName}-tab`).classList.add('active');
        }
    }
    
    handleFiles(fileList) {
        this.files.clear();
        const fileListDiv = document.getElementById('file-list');
        fileListDiv.innerHTML = '';
        
        // Convert FileList to array and process
        const files = Array.from(fileList);
        
        files.forEach(file => {
            // Handle both File objects and DataTransferItem
            if (file.getAsFile) {
                file = file.getAsFile();
            }
            
            if (file) {
                const relativePath = file.webkitRelativePath || file.name;
                this.files.set(relativePath, file);
                
                const fileItem = document.createElement('div');
                fileItem.className = 'file-item';
                fileItem.innerHTML = `
                    <span>${relativePath}</span>
                    <button onclick="sandrun.removeFile('${relativePath}')">×</button>
                `;
                fileListDiv.appendChild(fileItem);
            }
        });
        
        // Update drop zone text
        const dropText = document.querySelector('#drop-zone p');
        if (this.files.size > 0) {
            dropText.textContent = `${this.files.size} files selected`;
        } else {
            dropText.textContent = '📁 Drop folder here or click to select files';
        }
    }
    
    removeFile(path) {
        this.files.delete(path);
        this.handleFiles(Array.from(this.files.values()));
    }
    
    async createTarGz() {
        // Simple tar.gz creation for browser
        // For production, use a proper library like pako or tar-js
        const formData = new FormData();
        
        // Create a simple tar-like structure (simplified for demo)
        const boundary = '----TarBoundary' + Date.now();
        let tarContent = '';
        
        for (const [path, file] of this.files) {
            const content = await file.text();
            tarContent += `${boundary}\n`;
            tarContent += `Path: ${path}\n`;
            tarContent += `Size: ${file.size}\n`;
            tarContent += `\n${content}\n`;
        }
        
        // Convert to blob
        const blob = new Blob([tarContent], { type: 'application/x-tar' });
        return blob;
    }
    
    async submitJob() {
        const activeTab = document.querySelector('.tab-content.active').id;
        
        try {
            let formData = new FormData();
            let manifest = {};
            
            if (activeTab === 'files-tab') {
                // File upload mode
                if (this.files.size === 0) {
                    this.showMessage('Please select files to upload', 'error');
                    return;
                }
                
                // Create tar.gz from files
                const tarBlob = await this.createTarGz();
                formData.append('files', tarBlob, 'job.tar.gz');
                
                // Build manifest
                manifest = {
                    entrypoint: document.getElementById('entrypoint').value,
                    interpreter: document.getElementById('interpreter').value,
                    args: document.getElementById('args').value.split(' ').filter(a => a),
                    outputs: document.getElementById('outputs').value.split(' ').filter(o => o)
                };
            } else {
                // Quick code mode
                const code = document.getElementById('code-editor').value;
                if (!code.trim()) {
                    this.showMessage('Please enter some code', 'error');
                    return;
                }
                
                // Create a simple Python file
                const codeBlob = new Blob([code], { type: 'text/plain' });
                const tarContent = `----Tar\nPath: main.py\nSize: ${codeBlob.size}\n\n${code}`;
                const tarBlob = new Blob([tarContent], { type: 'application/x-tar' });
                
                formData.append('files', tarBlob, 'job.tar.gz');
                manifest = {
                    entrypoint: 'main.py',
                    interpreter: document.getElementById('quick-interpreter').value
                };
            }
            
            formData.append('manifest', JSON.stringify(manifest));
            
            // Submit to server
            document.getElementById('submit-btn').disabled = true;
            document.getElementById('submit-btn').innerHTML = '<span class="spinner"></span> Submitting...';
            
            const response = await fetch(`${this.serverUrl}/submit`, {
                method: 'POST',
                body: formData
            });
            
            if (!response.ok) {
                throw new Error(`Server error: ${response.status}`);
            }
            
            const data = await response.json();
            this.currentJob = data.job_id;
            
            // Store in active jobs
            this.activeJobs.set(data.job_id, {
                id: data.job_id,
                status: 'queued',
                submitted: new Date().toISOString()
            });
            this.saveActiveJobs();
            
            // Show job details
            this.showJobDetails(data.job_id);
            
            // Start polling for status
            this.startPolling(data.job_id);
            
        } catch (error) {
            this.showMessage(`Failed to submit job: ${error.message}`, 'error');
        } finally {
            document.getElementById('submit-btn').disabled = false;
            document.getElementById('submit-btn').innerHTML = 'Submit Job';
        }
    }
    
    showJobDetails(jobId) {
        document.getElementById('submit-section').style.display = 'none';
        document.getElementById('job-details').style.display = 'block';
        document.getElementById('job-id').textContent = jobId;
        
        // Reset outputs
        document.getElementById('stdout-output').textContent = 'Waiting for output...';
        document.getElementById('stderr-output').textContent = '';
        document.getElementById('files-output').innerHTML = '';
    }
    
    startPolling(jobId) {
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
        }
        
        this.pollInterval = setInterval(() => {
            this.updateJobStatus(jobId);
        }, 2000); // Poll every 2 seconds
        
        // Initial poll
        this.updateJobStatus(jobId);
    }
    
    async updateJobStatus(jobId) {
        try {
            // Get status
            const statusResp = await fetch(`${this.serverUrl}/status/${jobId}`);
            const status = await statusResp.json();
            
            // Update status badge
            const badge = document.getElementById('job-status');
            badge.textContent = status.status || 'unknown';
            badge.className = `status-badge ${status.status}`;
            
            // Get logs
            const logsResp = await fetch(`${this.serverUrl}/logs/${jobId}`);
            if (logsResp.ok) {
                const logs = await logsResp.json();
                document.getElementById('stdout-output').textContent = logs.stdout || 'No output';
                document.getElementById('stderr-output').textContent = logs.stderr || 'No errors';
            }
            
            // If completed, get outputs and stop polling
            if (status.status === 'completed' || status.status === 'failed') {
                clearInterval(this.pollInterval);
                
                // Update metrics if available
                if (status.cpu_seconds !== undefined) {
                    document.getElementById('cpu-time').textContent = `${status.cpu_seconds.toFixed(3)}s`;
                }
                if (status.memory_bytes !== undefined) {
                    document.getElementById('memory-usage').textContent = `${(status.memory_bytes / 1024 / 1024).toFixed(1)}MB`;
                }
                if (status.wall_time !== undefined) {
                    document.getElementById('wall-time').textContent = `${status.wall_time}ms`;
                }
                
                // Get output files
                const filesResp = await fetch(`${this.serverUrl}/outputs/${jobId}`);
                if (filesResp.ok) {
                    const files = await filesResp.json();
                    this.displayOutputFiles(jobId, files.files || []);
                }
            }
        } catch (error) {
            console.error('Failed to update job status:', error);
        }
    }
    
    displayOutputFiles(jobId, files) {
        const container = document.getElementById('files-output');
        container.innerHTML = '';
        
        if (files.length === 0) {
            container.innerHTML = '<p>No output files</p>';
            return;
        }
        
        files.forEach(file => {
            const fileDiv = document.createElement('div');
            fileDiv.className = 'file-download';
            fileDiv.innerHTML = `
                <span>${file}</span>
                <button onclick="sandrun.downloadFile('${jobId}', '${file}')" class="btn-small">Download</button>
            `;
            container.appendChild(fileDiv);
        });
    }
    
    async downloadFile(jobId, filename) {
        try {
            const response = await fetch(`${this.serverUrl}/download/${jobId}/${filename}`);
            if (!response.ok) throw new Error('Download failed');
            
            const blob = await response.blob();
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            a.click();
            window.URL.revokeObjectURL(url);
        } catch (error) {
            this.showMessage(`Failed to download ${filename}`, 'error');
        }
    }
    
    async downloadOutputs() {
        // Download all output files
        const filesContainer = document.getElementById('files-output');
        const downloadButtons = filesContainer.querySelectorAll('button');
        downloadButtons.forEach(btn => btn.click());
    }
    
    resetToSubmit() {
        document.getElementById('job-details').style.display = 'none';
        document.getElementById('submit-section').style.display = 'block';
        
        // Clear form
        this.files.clear();
        document.getElementById('file-list').innerHTML = '';
        document.getElementById('code-editor').value = '';
        document.querySelector('#drop-zone p').textContent = '📁 Drop folder here or click to select files';
        
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
        }
    }
    
    loadActiveJobs() {
        const stored = localStorage.getItem('sandrun-active-jobs');
        if (stored) {
            const jobs = JSON.parse(stored);
            jobs.forEach(job => this.activeJobs.set(job.id, job));
        }
        
        if (this.activeJobs.size > 0) {
            this.showJobsList();
        }
    }
    
    saveActiveJobs() {
        const jobs = Array.from(this.activeJobs.values());
        localStorage.setItem('sandrun-active-jobs', JSON.stringify(jobs));
    }
    
    showJobsList() {
        const section = document.getElementById('jobs-section');
        const list = document.getElementById('jobs-list');
        
        if (this.activeJobs.size === 0) {
            section.style.display = 'none';
            return;
        }
        
        section.style.display = 'block';
        list.innerHTML = '';
        
        this.activeJobs.forEach(job => {
            const item = document.createElement('div');
            item.className = 'job-item';
            item.innerHTML = `
                <div class="job-info">
                    <span class="job-id">${job.id}</span>
                    <span class="job-time">${new Date(job.submitted).toLocaleString()}</span>
                </div>
                <span class="status-badge ${job.status}">${job.status}</span>
            `;
            item.addEventListener('click', () => {
                this.showJobDetails(job.id);
                this.startPolling(job.id);
            });
            list.appendChild(item);
        });
    }
    
    showMessage(message, type = 'info') {
        // Simple message display - could be enhanced with toast notifications
        console.log(`[${type}] ${message}`);
        
        // Create temporary message element
        const msg = document.createElement('div');
        msg.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 1rem 1.5rem;
            background: ${type === 'error' ? 'var(--error)' : type === 'success' ? 'var(--accent)' : 'var(--info)'};
            color: white;
            border-radius: 4px;
            z-index: 9999;
            animation: slideIn 0.3s;
        `;
        msg.textContent = message;
        document.body.appendChild(msg);
        
        setTimeout(() => {
            msg.remove();
        }, 3000);
    }
}

// Initialize application
const sandrun = new SandrunClient();

// Add slide-in animation
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }
`;
document.head.appendChild(style);