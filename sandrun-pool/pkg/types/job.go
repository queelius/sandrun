package types

import (
	"crypto/sha256"
	"encoding/hex"
	"time"
)

// Job represents a compute job in the network
type Job struct {
	ID          string    `json:"id"`
	CodeHash    string    `json:"code_hash"`     // IPFS hash of code/data
	Manifest    Manifest  `json:"manifest"`      // Execution requirements
	Payment     uint64    `json:"payment"`       // Payment in smallest unit
	Redundancy  int       `json:"redundancy"`    // How many nodes should verify (1-3)
	Submitter   string    `json:"submitter"`     // Anonymous blockchain address
	SubmittedAt time.Time `json:"submitted_at"`
	Deadline    time.Time `json:"deadline"`
	Status      JobStatus `json:"status"`
}

// Manifest specifies job execution requirements
type Manifest struct {
	Entrypoint  string            `json:"entrypoint"`
	Interpreter string            `json:"interpreter"`
	Args        []string          `json:"args,omitempty"`
	Env         map[string]string `json:"env,omitempty"`
	Outputs     []string          `json:"outputs,omitempty"`
	Timeout     int               `json:"timeout"`     // seconds
	MemoryMB    int               `json:"memory_mb"`
	CPUSeconds  int               `json:"cpu_seconds"`
	GPU         *GPURequirements  `json:"gpu,omitempty"`
}

// GPURequirements for ML/compute workloads
type GPURequirements struct {
	Required           bool   `json:"required"`
	MinVRAMGB         int    `json:"min_vram_gb"`
	CUDAVersion       string `json:"cuda_version,omitempty"`
	ComputeCapability string `json:"compute_capability,omitempty"`
}

// JobStatus tracks job lifecycle
type JobStatus string

const (
	StatusPending   JobStatus = "pending"
	StatusAssigned  JobStatus = "assigned"
	StatusRunning   JobStatus = "running"
	StatusVerifying JobStatus = "verifying"
	StatusCompleted JobStatus = "completed"
	StatusFailed    JobStatus = "failed"
)

// ProofOfCompute represents execution verification
type ProofOfCompute struct {
	JobID           string            `json:"job_id"`
	NodeID          string            `json:"node_id"`
	ExecutionHash   string            `json:"execution_hash"`   // Hash of execution trace
	OutputHash      string            `json:"output_hash"`       // Hash of outputs
	CheckpointHashes []string         `json:"checkpoint_hashes"` // For long-running jobs
	CPUTime         float64           `json:"cpu_time"`          // Actual CPU seconds used
	GPUTime         float64           `json:"gpu_time"`          // GPU seconds if applicable
	MemoryPeak      uint64            `json:"memory_peak"`       // Peak memory in bytes
	Timestamp       time.Time         `json:"timestamp"`
	Signature       string            `json:"signature"`         // Node's cryptographic signature
}

// CalculateProofHash generates deterministic proof hash
func (p *ProofOfCompute) CalculateHash() string {
	data := p.JobID + p.NodeID + p.ExecutionHash + p.OutputHash
	for _, checkpoint := range p.CheckpointHashes {
		data += checkpoint
	}
	hash := sha256.Sum256([]byte(data))
	return hex.EncodeToString(hash[:])
}

// VerifyConsensus checks if multiple proofs match
func VerifyConsensus(proofs []ProofOfCompute) bool {
	if len(proofs) < 2 {
		return false
	}
	
	// Check if execution hashes match (deterministic execution)
	firstHash := proofs[0].ExecutionHash
	for _, proof := range proofs[1:] {
		if proof.ExecutionHash != firstHash {
			return false
		}
	}
	
	return true
}

// Node represents a sandrun compute node
type Node struct {
	ID           string           `json:"id"`
	Address      string           `json:"address"`      // Network address
	Capabilities NodeCapabilities `json:"capabilities"`
	Reputation   float64          `json:"reputation"`   // 0.0 to 1.0
	ActiveJobs   []string         `json:"active_jobs"`
	LastSeen     time.Time        `json:"last_seen"`
	Stake        uint64           `json:"stake"`        // Collateral staked
}

// NodeCapabilities describes what a node can execute
type NodeCapabilities struct {
	CPUCores     int              `json:"cpu_cores"`
	MemoryGB     int              `json:"memory_gb"`
	GPUs         []GPUInfo        `json:"gpus,omitempty"`
	MaxJobs      int              `json:"max_jobs"`
	Interpreters []string         `json:"interpreters"` // python3, node, etc
}

// GPUInfo describes available GPU
type GPUInfo struct {
	Model             string  `json:"model"`              // e.g., "NVIDIA RTX 3090"
	VRAMGB           int     `json:"vram_gb"`
	CUDAVersion      string  `json:"cuda_version"`
	ComputeCapability string  `json:"compute_capability"`
	Utilization      float64 `json:"utilization"`        // Current usage 0-1
}

// JobAssignment tracks which nodes are executing a job
type JobAssignment struct {
	JobID     string    `json:"job_id"`
	NodeIDs   []string  `json:"node_ids"`
	AssignedAt time.Time `json:"assigned_at"`
	ExpiresAt  time.Time `json:"expires_at"`
}