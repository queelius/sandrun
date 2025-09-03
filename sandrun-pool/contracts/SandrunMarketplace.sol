// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract SandrunMarketplace {
    struct Job {
        bytes32 codeHash;       // IPFS hash of code/data
        uint256 payment;        // Payment amount in wei
        uint8 redundancy;       // Number of nodes to verify (1-3)
        address submitter;      // Anonymous address
        uint256 deadline;       // Unix timestamp
        JobStatus status;
        bytes32[] proofHashes;  // Submitted proofs
        address[] executors;    // Nodes that executed
    }
    
    struct NodeInfo {
        uint256 stake;          // Collateral staked
        uint256 reputation;     // Reputation score (0-1000)
        uint256 jobsCompleted;
        uint256 jobsFailed;
        bool isActive;
    }
    
    enum JobStatus {
        Pending,
        Assigned,
        Executing,
        Verifying,
        Completed,
        Failed
    }
    
    mapping(bytes32 => Job) public jobs;
    mapping(address => NodeInfo) public nodes;
    mapping(bytes32 => mapping(address => bytes32)) public proofs;
    
    uint256 public constant MIN_STAKE = 0.01 ether;
    uint256 public constant MIN_PAYMENT = 0.001 ether;
    uint256 public constant VERIFICATION_REWARD_PERCENT = 10; // 10% bonus for verification nodes
    
    event JobSubmitted(bytes32 indexed jobId, address indexed submitter, uint256 payment);
    event JobAssigned(bytes32 indexed jobId, address[] nodes);
    event ProofSubmitted(bytes32 indexed jobId, address indexed node, bytes32 proofHash);
    event JobCompleted(bytes32 indexed jobId, bytes32 consensusProof);
    event PaymentDistributed(bytes32 indexed jobId, address indexed node, uint256 amount);
    
    // Submit a new job
    function submitJob(
        bytes32 _codeHash,
        uint8 _redundancy,
        uint256 _deadline
    ) external payable returns (bytes32) {
        require(msg.value >= MIN_PAYMENT, "Payment too low");
        require(_redundancy >= 1 && _redundancy <= 3, "Invalid redundancy");
        require(_deadline > block.timestamp, "Invalid deadline");
        
        bytes32 jobId = keccak256(abi.encodePacked(
            _codeHash,
            msg.sender,
            block.timestamp,
            block.number
        ));
        
        Job storage job = jobs[jobId];
        job.codeHash = _codeHash;
        job.payment = msg.value;
        job.redundancy = _redundancy;
        job.submitter = msg.sender;
        job.deadline = _deadline;
        job.status = JobStatus.Pending;
        
        emit JobSubmitted(jobId, msg.sender, msg.value);
        return jobId;
    }
    
    // Node registers to join the pool
    function registerNode() external payable {
        require(msg.value >= MIN_STAKE, "Insufficient stake");
        
        NodeInfo storage node = nodes[msg.sender];
        node.stake += msg.value;
        node.isActive = true;
        
        if (node.reputation == 0) {
            node.reputation = 500; // Start with neutral reputation
        }
    }
    
    // Node claims a job
    function claimJob(bytes32 _jobId) external {
        Job storage job = jobs[_jobId];
        NodeInfo storage node = nodes[msg.sender];
        
        require(job.status == JobStatus.Pending, "Job not available");
        require(node.isActive, "Node not registered");
        require(node.stake >= MIN_STAKE, "Insufficient stake");
        require(job.executors.length < job.redundancy, "Job fully assigned");
        
        // Check if node already assigned
        for (uint i = 0; i < job.executors.length; i++) {
            require(job.executors[i] != msg.sender, "Already assigned");
        }
        
        job.executors.push(msg.sender);
        
        if (job.executors.length == job.redundancy) {
            job.status = JobStatus.Assigned;
            emit JobAssigned(_jobId, job.executors);
        }
    }
    
    // Submit proof of computation
    function submitProof(bytes32 _jobId, bytes32 _proofHash) external {
        Job storage job = jobs[_jobId];
        
        require(job.status == JobStatus.Assigned || job.status == JobStatus.Executing, "Invalid job state");
        require(isExecutor(_jobId, msg.sender), "Not assigned to job");
        require(proofs[_jobId][msg.sender] == bytes32(0), "Proof already submitted");
        
        proofs[_jobId][msg.sender] = _proofHash;
        job.proofHashes.push(_proofHash);
        job.status = JobStatus.Executing;
        
        emit ProofSubmitted(_jobId, msg.sender, _proofHash);
        
        // Check if all proofs submitted
        if (job.proofHashes.length == job.redundancy) {
            job.status = JobStatus.Verifying;
            verifyAndComplete(_jobId);
        }
    }
    
    // Verify consensus and distribute payment
    function verifyAndComplete(bytes32 _jobId) internal {
        Job storage job = jobs[_jobId];
        
        // Find consensus proof (majority agreement)
        bytes32 consensusProof = findConsensusProof(job.proofHashes);
        
        if (consensusProof != bytes32(0)) {
            job.status = JobStatus.Completed;
            emit JobCompleted(_jobId, consensusProof);
            
            // Distribute payments to nodes that agreed with consensus
            distributePayments(_jobId, consensusProof);
        } else {
            // No consensus, mark as failed
            job.status = JobStatus.Failed;
            // Refund submitter
            payable(job.submitter).transfer(job.payment);
        }
    }
    
    // Find the proof that majority agrees on
    function findConsensusProof(bytes32[] memory proofHashes) internal pure returns (bytes32) {
        if (proofHashes.length == 1) {
            return proofHashes[0];
        }
        
        // Count occurrences of each proof
        for (uint i = 0; i < proofHashes.length; i++) {
            uint count = 1;
            for (uint j = i + 1; j < proofHashes.length; j++) {
                if (proofHashes[i] == proofHashes[j]) {
                    count++;
                }
            }
            
            // If majority agrees (>50%), this is consensus
            if (count > proofHashes.length / 2) {
                return proofHashes[i];
            }
        }
        
        return bytes32(0); // No consensus
    }
    
    // Distribute payments to honest nodes
    function distributePayments(bytes32 _jobId, bytes32 consensusProof) internal {
        Job storage job = jobs[_jobId];
        uint256 totalPayment = job.payment;
        uint256 honestNodes = 0;
        
        // Count honest nodes (those who agreed with consensus)
        for (uint i = 0; i < job.executors.length; i++) {
            if (proofs[_jobId][job.executors[i]] == consensusProof) {
                honestNodes++;
            }
        }
        
        if (honestNodes == 0) return;
        
        // Calculate payment per honest node
        uint256 paymentPerNode = totalPayment / honestNodes;
        
        // Distribute payments and update reputation
        for (uint i = 0; i < job.executors.length; i++) {
            address executor = job.executors[i];
            NodeInfo storage node = nodes[executor];
            
            if (proofs[_jobId][executor] == consensusProof) {
                // Honest node - pay and increase reputation
                payable(executor).transfer(paymentPerNode);
                node.jobsCompleted++;
                if (node.reputation < 1000) {
                    node.reputation += 10;
                }
                
                emit PaymentDistributed(_jobId, executor, paymentPerNode);
            } else {
                // Dishonest node - slash stake and decrease reputation
                node.jobsFailed++;
                if (node.reputation > 50) {
                    node.reputation -= 50;
                }
                
                // Slash 10% of stake
                uint256 slashAmount = node.stake / 10;
                if (slashAmount > 0) {
                    node.stake -= slashAmount;
                    // Slashed stake goes to honest nodes
                    totalPayment += slashAmount;
                }
            }
        }
    }
    
    // Check if address is assigned executor
    function isExecutor(bytes32 _jobId, address _node) internal view returns (bool) {
        Job storage job = jobs[_jobId];
        for (uint i = 0; i < job.executors.length; i++) {
            if (job.executors[i] == _node) {
                return true;
            }
        }
        return false;
    }
    
    // Node withdraws stake (if not active in jobs)
    function withdrawStake(uint256 _amount) external {
        NodeInfo storage node = nodes[msg.sender];
        require(node.stake >= _amount, "Insufficient stake");
        
        // TODO: Check no active jobs
        
        node.stake -= _amount;
        if (node.stake < MIN_STAKE) {
            node.isActive = false;
        }
        
        payable(msg.sender).transfer(_amount);
    }
    
    // Get job details
    function getJob(bytes32 _jobId) external view returns (
        bytes32 codeHash,
        uint256 payment,
        uint8 redundancy,
        address submitter,
        uint256 deadline,
        JobStatus status,
        address[] memory executors
    ) {
        Job storage job = jobs[_jobId];
        return (
            job.codeHash,
            job.payment,
            job.redundancy,
            job.submitter,
            job.deadline,
            job.status,
            job.executors
        );
    }
}