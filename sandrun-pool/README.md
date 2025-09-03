# Sandrun-Pool: Decentralized Compute Orchestrator

Sandrun-Pool is the orchestration layer that creates a decentralized marketplace for sandrun compute nodes. It enables anonymous job submission via blockchain, proof-of-compute verification, and automatic micropayments.

## Architecture

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   Client    │────▶│  Blockchain  │◀────│ Sandrun Node│
└─────────────┘     └──────────────┘     └─────────────┘
                            │                     │
                            ▼                     ▼
                      ┌──────────┐         ┌──────────┐
                      │   IPFS   │         │   Pool   │
                      └──────────┘         │  Manager │
                                          └──────────┘
```

## Components

### 1. Pool Manager (`cmd/pool/`)
- Monitors blockchain for new jobs
- Manages node registry
- Coordinates job assignment
- Verifies proof-of-compute consensus

### 2. Node Agent (`cmd/agent/`)
- Runs on each sandrun node
- Registers capabilities (CPU/GPU/memory)
- Claims and executes jobs
- Generates proof-of-compute
- Submits results

### 3. Smart Contracts (`contracts/`)
- Job posting with escrow
- Proof verification
- Automatic payment distribution
- Reputation tracking

### 4. Client SDK (`client/`)
- Submit jobs anonymously
- Monitor execution
- Download results
- Handle payments

## Features

- **Anonymous Job Submission**: No accounts, just cryptographic proofs
- **GPU Support**: Nodes advertise GPU capabilities, jobs specify requirements
- **Proof-of-Compute**: Deterministic execution traces for verification
- **Consensus**: 2-of-3 node verification for high-value jobs
- **Micropayments**: Automatic payment on proof submission
- **IPFS Storage**: Code and results stored off-chain
- **Checkpoint/Restore**: Long jobs can suspend and resume

## Quick Start

### Run a Pool Node
```bash
# Start a sandrun node with pool agent
./sandrun-pool agent \
  --sandrun-url http://localhost:8443 \
  --blockchain-rpc https://arbitrum.infura.io \
  --ipfs-api http://localhost:5001
```

### Submit a Job
```bash
# Submit job via CLI
./sandrun-pool submit \
  --code ./my-script.py \
  --manifest job.json \
  --payment 0.1 \
  --redundancy 2
```

### Monitor Network
```bash
# View pool statistics
./sandrun-pool status

# List active nodes
./sandrun-pool nodes

# View job queue
./sandrun-pool jobs
```

## Job Pricing

| Resource | Price | Example |
|----------|-------|---------|
| CPU | $0.001/second | Simple script: $0.01 |
| GPU (T4) | $0.01/second | SD inference: $0.10 |
| GPU (A100) | $0.05/second | LLM inference: $0.50 |
| Memory | +$0.0001/MB | 2GB job: +$0.20 |
| Priority | 2x multiplier | Urgent: 2x cost |

## Security

- Sandboxed execution via sandrun
- Encrypted job storage on IPFS
- Proof verification before payment
- Slashing for malicious nodes
- No persistent user data

## Development

```bash
# Build all components
make build

# Run tests
make test

# Deploy contracts
make deploy-contracts

# Start local testnet
make testnet
```

## License

MIT