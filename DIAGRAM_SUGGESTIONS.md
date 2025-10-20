# Visual Diagram Suggestions for Sandrun Documentation

This document provides specific suggestions for diagrams that would enhance the documentation.

## Priority 1: Essential Diagrams

### 1. Job Execution Flow

**Location:** `/docs/architecture.md` and `/docs/getting-started.md`

**Purpose:** Show complete lifecycle of a job from submission to cleanup

**Suggested Mermaid Code:**
```mermaid
sequenceDiagram
    participant Client
    participant HTTP Server
    participant Queue
    participant Sandbox
    participant tmpfs

    Client->>HTTP Server: POST /submit (tarball + manifest)
    HTTP Server->>HTTP Server: Parse manifest
    HTTP Server->>Queue: Enqueue job
    HTTP Server->>Client: {job_id, status: queued}

    Queue->>Sandbox: Dequeue job
    Sandbox->>Sandbox: Create namespaces (PID, net, mount)
    Sandbox->>tmpfs: Extract tarball to /tmp
    Sandbox->>tmpfs: Install dependencies (if any)
    Sandbox->>Sandbox: Apply seccomp filter
    Sandbox->>Sandbox: Drop capabilities
    Sandbox->>Sandbox: Execute entrypoint

    Client->>HTTP Server: GET /status/{job_id}
    HTTP Server->>Client: {status: running}

    Client->>HTTP Server: WebSocket /logs/{job_id}/stream
    Sandbox->>HTTP Server: Stream stdout/stderr
    HTTP Server->>Client: Log lines

    Sandbox->>tmpfs: Write output files
    Sandbox->>Queue: Mark complete

    Client->>HTTP Server: GET /download/{job_id}/output.txt
    HTTP Server->>tmpfs: Read output
    HTTP Server->>Client: File content
    tmpfs->>tmpfs: Delete job data
```

### 2. Security Isolation Layers

**Location:** `/docs/security.md` and `/docs/architecture.md`

**Purpose:** Visual representation of defense-in-depth layers

**Suggested Mermaid Code:**
```mermaid
graph TB
    subgraph "Untrusted Code"
        A[User Application]
    end

    subgraph "Isolation Layer 1: Seccomp-BPF"
        B[Syscall Filter<br/>~60 allowed syscalls]
    end

    subgraph "Isolation Layer 2: Linux Namespaces"
        C1[PID Namespace]
        C2[Network Namespace]
        C3[Mount Namespace]
        C4[IPC Namespace]
        C5[UTS Namespace]
    end

    subgraph "Isolation Layer 3: Capabilities"
        D[Drop all CAP_*<br/>No privileged operations]
    end

    subgraph "Isolation Layer 4: Cgroups"
        E1[CPU Quota: 10s/min]
        E2[Memory: 512MB max]
        E3[Processes: 100 max]
    end

    subgraph "Isolation Layer 5: tmpfs"
        F[RAM-only storage<br/>No disk persistence]
    end

    subgraph "Host System"
        G[Linux Kernel]
        H[Hardware]
    end

    A --> B
    B --> C1 & C2 & C3 & C4 & C5
    C1 & C2 & C3 & C4 & C5 --> D
    D --> E1 & E2 & E3
    E1 & E2 & E3 --> F
    F --> G
    G --> H

    style A fill:#ff6b6b
    style B fill:#4ecdc4
    style C1 fill:#95e1d3
    style C2 fill:#95e1d3
    style C3 fill:#95e1d3
    style C4 fill:#95e1d3
    style C5 fill:#95e1d3
    style D fill:#45b7d1
    style E1 fill:#f9ca24
    style E2 fill:#f9ca24
    style E3 fill:#f9ca24
    style F fill:#ff9ff3
    style G fill:#a29bfe
    style H fill:#636e72
```

### 3. Pool Coordinator Architecture

**Location:** `/docs/integrations/trusted-pool.md`

**Purpose:** Show how pool coordinator distributes jobs across workers

**Suggested Mermaid Code:**
```mermaid
graph TB
    subgraph "Client Applications"
        C1[Client 1]
        C2[Client 2]
        C3[Client 3]
    end

    subgraph "Pool Coordinator"
        PC[Coordinator<br/>Port 9000]
        Q[Job Queue]
        HM[Health Monitor]
        LB[Load Balancer]
    end

    subgraph "Worker Pool"
        W1[Worker 1<br/>Port 8443<br/>Ed25519 Key]
        W2[Worker 2<br/>Port 8443<br/>Ed25519 Key]
        W3[Worker 3<br/>Port 8443<br/>Ed25519 Key]
    end

    subgraph "Allowlist"
        AL[workers.json<br/>Public Keys]
    end

    C1 & C2 & C3 -->|POST /submit| PC
    PC --> Q
    Q --> LB

    HM -.->|Health Check<br/>Every 30s| W1 & W2 & W3
    W1 & W2 & W3 -.->|{status:healthy,<br/>worker_id}| HM

    LB -->|Route to<br/>Least Loaded| W1 & W2 & W3

    AL -.->|Verify<br/>Identity| HM

    W1 & W2 & W3 -->|Results +<br/>Signature| PC
    PC -->|GET /status<br/>GET /download| C1 & C2 & C3

    style C1 fill:#74b9ff
    style C2 fill:#74b9ff
    style C3 fill:#74b9ff
    style PC fill:#fdcb6e
    style Q fill:#e17055
    style HM fill:#00b894
    style LB fill:#00cec9
    style W1 fill:#6c5ce7
    style W2 fill:#6c5ce7
    style W3 fill:#6c5ce7
    style AL fill:#fd79a8
```

## Priority 2: Helpful Diagrams

### 4. Rate Limiting Flow

**Location:** `/docs/getting-started.md` and `/docs/faq.md`

**Suggested Mermaid Code:**
```mermaid
graph LR
    A[Job Submission] --> B{Check IP Quota}
    B -->|< 10 CPU-sec used| C{Check Concurrent Jobs}
    B -->|≥ 10 CPU-sec used| D[429 Rate Limited]

    C -->|< 2 active jobs| E[Accept Job]
    C -->|≥ 2 active jobs| D

    E --> F[Execute in Sandbox]
    F --> G[Track CPU Usage]
    G --> H{Job Complete}

    H -->|Success| I[Update Quota]
    H -->|Failed| I

    I --> J[Free Concurrent Slot]

    D --> K[Wait for Quota Reset]
    K -->|After 1 hour idle| A

    style A fill:#74b9ff
    style E fill:#00b894
    style F fill:#fdcb6e
    style D fill:#d63031
    style I fill:#00cec9
```

### 5. Worker Identity & Verification

**Location:** `/docs/integrations/trusted-pool.md` and `/docs/security.md`

**Suggested Mermaid Code:**
```mermaid
sequenceDiagram
    participant W as Worker
    participant K as Private Key
    participant J as Job
    participant C as Client
    participant V as Verifier

    Note over W,K: Worker Startup
    W->>K: Load Ed25519 Private Key
    K-->>W: Private Key (32 bytes)
    W->>W: Derive Public Key
    W->>W: Worker ID = Base64(Public Key)

    Note over J,W: Job Execution
    J->>W: Execute job
    W->>W: Calculate job_hash = SHA256(inputs + outputs)
    W->>K: Sign(job_hash)
    K-->>W: Signature (64 bytes)

    Note over W,C: Result Delivery
    W->>C: {job_hash, worker_id, signature, outputs}

    Note over C,V: Verification
    C->>V: Verify signature
    V->>V: Public Key = Base64Decode(worker_id)
    V->>V: Verify(job_hash, signature, public_key)
    alt Signature Valid
        V-->>C: ✅ Result Verified
    else Signature Invalid
        V-->>C: ❌ Tampered or Wrong Worker
    end
```

### 6. Environment Templates

**Location:** `/docs/getting-started.md` and `/docs/job-manifest.md`

**Suggested Mermaid Code:**
```mermaid
graph TB
    subgraph "Base System"
        BASE[Ubuntu 20.04<br/>Python 3.10<br/>Node.js 18<br/>Bash, Ruby, R]
    end

    subgraph "Pre-built Environments"
        DEFAULT[default<br/>Base only]
        ML[ml-basic<br/>+ NumPy, SciPy<br/>+ pandas, sklearn]
        VISION[vision<br/>+ OpenCV, Pillow<br/>+ scikit-image]
        NLP[nlp<br/>+ NLTK, spaCy<br/>+ transformers]
        DS[data-science<br/>+ Jupyter, matplotlib<br/>+ seaborn, plotly]
        SCI[scientific<br/>+ SymPy, NetworkX<br/>+ statsmodels]
    end

    subgraph "Custom Environments"
        CUSTOM[Your Job<br/>+ requirements.txt<br/>+ package.json]
    end

    BASE --> DEFAULT
    BASE --> ML
    BASE --> VISION
    BASE --> NLP
    BASE --> DS
    BASE --> SCI

    ML --> CUSTOM
    VISION --> CUSTOM
    NLP --> CUSTOM
    DS --> CUSTOM
    SCI --> CUSTOM
    DEFAULT --> CUSTOM

    style BASE fill:#95e1d3
    style DEFAULT fill:#dfe6e9
    style ML fill:#74b9ff
    style VISION fill:#a29bfe
    style NLP fill:#fd79a8
    style DS fill:#fdcb6e
    style SCI fill:#00b894
    style CUSTOM fill:#ff7675
```

## Priority 3: Advanced Diagrams

### 7. Complete System Architecture

**Location:** `/docs/architecture.md`

**Suggested Mermaid Code:**
```mermaid
graph TB
    subgraph "Client Layer"
        WEB[Web Frontend<br/>Vue.js]
        CLI[CLI Client<br/>curl/Python]
        MCP[MCP Integration<br/>Claude Desktop]
    end

    subgraph "API Gateway Layer"
        LB[Load Balancer<br/>nginx/Caddy]
        TLS[TLS Termination]
    end

    subgraph "Sandrun Core"
        HTTP[HTTP Server<br/>C++ Minimal]
        QUEUE[Job Queue<br/>FIFO + Priority]
        EXEC[Executor<br/>Worker Threads]
        WI[Worker Identity<br/>Ed25519 Signing]
    end

    subgraph "Isolation Layer"
        NS[Namespace Manager]
        SC[Seccomp Filter]
        CG[Cgroup Manager]
        TMP[tmpfs Manager]
    end

    subgraph "Storage Layer"
        RAM[RAM/tmpfs<br/>Job Data]
        ENV[Environment Cache<br/>Templates]
    end

    subgraph "Extensions"
        POOL[Pool Coordinator<br/>Python]
        BROKER[Job Broker<br/>SQLite]
    end

    WEB & CLI & MCP --> LB
    LB --> TLS
    TLS --> HTTP

    HTTP --> QUEUE
    QUEUE --> EXEC
    EXEC --> WI
    EXEC --> NS

    NS --> SC
    SC --> CG
    CG --> TMP

    TMP --> RAM
    EXEC --> ENV

    HTTP -.-> POOL
    HTTP -.-> BROKER

    style WEB fill:#74b9ff
    style CLI fill:#74b9ff
    style MCP fill:#74b9ff
    style HTTP fill:#fdcb6e
    style QUEUE fill:#e17055
    style EXEC fill:#00b894
    style NS fill:#6c5ce7
    style SC fill:#a29bfe
    style CG fill:#fd79a8
    style TMP fill:#ff7675
    style RAM fill:#d63031
```

### 8. CI/CD Integration Example

**Location:** `/docs/faq.md` or new `/docs/integrations/cicd.md`

**Suggested Mermaid Code:**
```mermaid
graph LR
    subgraph "GitHub Actions"
        A[Code Push]
        B[Checkout Code]
        C[Package Tests]
    end

    subgraph "Sandrun Cluster"
        D[Submit Job]
        E[Execute in Sandbox]
        F[Get Results]
    end

    subgraph "Report Results"
        G{Tests Pass?}
        H[✅ Mark Build Success]
        I[❌ Mark Build Failed]
        J[Post Comment on PR]
    end

    A --> B
    B --> C
    C -->|tar czf tests.tar.gz| D
    D --> E
    E --> F
    F --> G
    G -->|Yes| H
    G -->|No| I
    H --> J
    I --> J

    style A fill:#74b9ff
    style E fill:#fdcb6e
    style H fill:#00b894
    style I fill:#d63031
```

## Implementation Instructions

### Using Mermaid in MkDocs

Mermaid is already supported in your MkDocs Material configuration. Simply add diagrams like this:

```markdown
## Architecture Overview

```mermaid
graph LR
    A[Client] --> B[Server]
    B --> C[Sandbox]
```
\`\`\`
```

### Creating Static Images

For more complex diagrams, consider using:

1. **Draw.io** (diagrams.net)
   - Free, web-based
   - Export as SVG for best quality
   - Can embed in Markdown: `![Diagram](images/architecture.svg)`

2. **Excalidraw**
   - Hand-drawn style
   - Great for informal diagrams
   - Export as PNG or SVG

3. **PlantUML**
   - Text-based diagram generation
   - Can integrate with MkDocs via plugin

### Directory Structure

```
docs/
├── images/
│   ├── job-flow.png
│   ├── pool-architecture.svg
│   ├── security-layers.svg
│   └── ...
└── *.md (documentation files)
```

### Embedding Images

```markdown
## Architecture

![Job Execution Flow](images/job-flow.png)

*Figure 1: Complete job lifecycle from submission to cleanup*
```

## Next Steps

1. **Review these suggestions** and prioritize which diagrams add most value
2. **Create diagrams** using Mermaid (for simple) or Draw.io (for complex)
3. **Test locally** with `mkdocs serve` to preview
4. **Commit images** to `docs/images/` directory
5. **Update documentation** to embed diagrams

---

**Questions?** These are suggestions based on documentation review. Feel free to modify or add your own diagrams based on what users find most helpful!
