# Sandrun Pool Management System

## Pool Architecture Overview

The pool management system handles the distributed marketplace of compute nodes, providing abstractions for node discovery, capability matching, consensus verification, and payment distribution.

## Core Pool Management Classes

### PoolBuilder - Pool Discovery and Configuration
```typescript
class PoolBuilder extends SandrunBuilder<Pool> {
  // Discovery methods
  discover(criteria?: DiscoveryCriteria): PoolBuilder
  nodes(nodeIds: string[]): PoolBuilder
  region(region: string | string[]): PoolBuilder
  
  // Capability filtering
  minGPUs(count: number, type?: string): PoolBuilder
  minMemory(gb: number): PoolBuilder
  minBandwidth(mbps: number): PoolBuilder
  
  // Consensus and reliability
  consensus(strategy: ConsensusStrategy): PoolBuilder
  redundancy(factor: number): PoolBuilder
  verification(level: VerificationLevel): PoolBuilder
  
  // Economics
  maxCostPerHour(amount: string, token?: string): PoolBuilder
  preferredPricing(model: PricingModel): PoolBuilder
  
  // Build pool
  async join(): Promise<Pool>
  async estimate(): Promise<PoolEstimate>
}

interface DiscoveryCriteria {
  minNodes?: number
  maxNodes?: number
  regions?: string[]
  capabilities?: Capability[]
  reputation?: number // minimum reputation score
  latency?: number // maximum latency in ms
  availability?: number // minimum uptime percentage
  pricing?: {
    max: string
    currency: string
    model: 'per-hour' | 'per-job' | 'per-compute'
  }
}

type ConsensusStrategy = 
  | 'fastest-wins'
  | 'majority-rule' 
  | '2-of-3'
  | '3-of-5'
  | 'byzantine-fault-tolerant'
  | 'proof-of-stake-weighted'

type VerificationLevel = 'basic' | 'standard' | 'strict' | 'cryptographic'
```

### Pool - Active Pool Management
```typescript
class Pool {
  readonly id: string
  readonly nodes: Node[]
  readonly capabilities: AggregatedCapabilities
  readonly consensus: ConsensusStrategy
  
  // Job distribution
  async distribute(job: Job, strategy?: DistributionStrategy): Promise<JobExecution[]>
  async schedule(jobs: Job[]): Promise<ScheduleResult>
  
  // Node management
  async addNode(node: Node): Promise<void>
  async removeNode(nodeId: string): Promise<void>
  async replaceNode(oldNodeId: string, newNode: Node): Promise<void>
  
  // Health and monitoring
  async healthCheck(): Promise<PoolHealth>
  async getMetrics(): Promise<PoolMetrics>
  
  // Consensus and verification
  async verify(proofs: Proof[]): Promise<ConsensusResult>
  async resolveDispute(disputeId: string): Promise<DisputeResolution>
  
  // Economic operations
  async distributePayments(jobResult: JobResult): Promise<PaymentDistribution>
  async escrowFunds(amount: string, jobId: string): Promise<EscrowTransaction>
  
  // Events
  on(event: PoolEvent, handler: (data: any) => void): void
  off(event: PoolEvent, handler: (data: any) => void): void
}

type PoolEvent = 
  | 'node-joined' 
  | 'node-left' 
  | 'job-started' 
  | 'job-completed' 
  | 'consensus-achieved'
  | 'dispute-raised'
  | 'payment-distributed'

interface PoolHealth {
  status: 'healthy' | 'degraded' | 'critical'
  activeNodes: number
  failedNodes: string[]
  avgLatency: number
  consensusReliability: number
}
```

### NodeBuilder - Node Registration and Management
```typescript
class NodeBuilder extends SandrunBuilder<Node> {
  // Node identity
  identity(privateKey: string): NodeBuilder
  alias(name: string): NodeBuilder
  
  // Capabilities declaration
  capabilities(caps: NodeCapabilities): NodeBuilder
  gpu(specs: GPUSpecs): NodeBuilder
  cpu(specs: CPUSpecs): NodeBuilder
  memory(gb: number): NodeBuilder
  storage(specs: StorageSpecs): NodeBuilder
  network(bandwidth: number, latency: number): NodeBuilder
  
  // Geographic and network
  region(region: string): NodeBuilder
  availability(schedule: AvailabilitySchedule): NodeBuilder
  
  // Economic model
  pricing(model: PricingModel): NodeBuilder
  paymentMethods(methods: PaymentMethod[]): NodeBuilder
  
  // Reliability and reputation
  bondAmount(amount: string, token: string): NodeBuilder
  insurance(policy: InsurancePolicy): NodeBuilder
  
  async register(): Promise<Node>
  async validate(): Promise<ValidationResult>
}

interface NodeCapabilities {
  gpu?: {
    type: string // 'rtx4090', 'a100', etc.
    memory: string
    count: number
    computeCapability: number
    interconnect?: 'nvlink' | 'infiniband'
  }
  cpu: {
    architecture: 'x86_64' | 'arm64'
    cores: number
    threads: number
    frequency: number
    features: string[] // ['avx2', 'sse4', etc.]
  }
  memory: {
    total: number // GB
    type: 'ddr4' | 'ddr5' | 'hbm'
    speed: number // MHz
  }
  storage: {
    type: 'ssd' | 'nvme' | 'hdd'
    capacity: number // GB
    iops: number
  }
  network: {
    bandwidth: number // Mbps
    latency: number // ms to major regions
  }
  specialized?: {
    fpga?: FPGASpecs
    asic?: ASICSpecs
    quantum?: QuantumSpecs
  }
}

interface PricingModel {
  gpu?: {
    perHour: string
    perJob?: string
    perCompute?: string // per GFLOP or similar
  }
  cpu: {
    perCoreHour: string
  }
  memory: {
    perGBHour: string
  }
  storage: {
    perGBHour: string
  }
  network: {
    perGBTransfer: string
  }
  minimumJob?: string
  currency: string
  discounts?: {
    volume?: VolumeDiscount[]
    duration?: DurationDiscount[]
  }
}
```

### Node - Active Node Operations
```typescript
class Node {
  readonly id: string
  readonly publicKey: string
  readonly capabilities: NodeCapabilities
  readonly reputation: ReputationScore
  
  // Job execution
  async claimJob(jobId: string): Promise<JobClaim>
  async executeJob(job: Job): Promise<JobExecution>
  async generateProof(execution: JobExecution): Promise<Proof>
  
  // Pool participation
  async joinPool(poolId: string): Promise<void>
  async leavePool(poolId: string): Promise<void>
  async heartbeat(): Promise<HeartbeatResponse>
  
  // Resource management
  async getUtilization(): Promise<ResourceUtilization>
  async reserveResources(requirements: ResourceRequest): Promise<Reservation>
  async releaseResources(reservationId: string): Promise<void>
  
  // Economic operations
  async submitInvoice(jobId: string, amount: string): Promise<Invoice>
  async claimPayment(invoiceId: string): Promise<PaymentClaim>
  
  // Monitoring and health
  async status(): Promise<NodeStatus>
  async benchmark(): Promise<BenchmarkResults>
  
  // Events
  on(event: NodeEvent, handler: (data: any) => void): void
  
  // Lifecycle
  async startListening(): Promise<void>
  async stopListening(): Promise<void>
  async shutdown(): Promise<void>
}

type NodeEvent = 
  | 'job-claimed'
  | 'job-started' 
  | 'job-completed'
  | 'resource-low'
  | 'payment-received'
  | 'dispute-raised'

interface NodeStatus {
  online: boolean
  utilization: ResourceUtilization
  activeJobs: number
  queuedJobs: number
  reputation: ReputationScore
  earnings: {
    today: string
    week: string
    month: string
    total: string
  }
  health: {
    temperature: number
    powerDraw: number
    errors: string[]
  }
}
```

## Pool Usage Patterns

### 1. Simple Pool Discovery
```typescript
// Find any available GPU nodes
const quickPool = await sandrun
  .pool()
  .discover({ minNodes: 1 })
  .minGPUs(1)
  .maxCostPerHour('1.0', 'ETH')
  .join()

// Submit job to pool
const execution = await sandrun
  .job()
  .code('gpu_computation()')
  .with(quickPool)
  .submit()
```

### 2. High-Reliability Pool
```typescript
// Create pool for mission-critical workload
const reliablePool = await sandrun
  .pool()
  .discover({
    minNodes: 5,
    reputation: 0.95,
    availability: 0.99
  })
  .region(['us-east-1', 'us-west-2']) // Multi-region
  .minGPUs(4, 'a100')
  .consensus('byzantine-fault-tolerant')
  .redundancy(3) // Triple redundancy
  .verification('cryptographic')
  .join()

// Monitor pool health
reliablePool.on('node-left', async (nodeId) => {
  console.log(`Node ${nodeId} left, finding replacement...`)
  const replacement = await sandrun
    .pool()
    .discover({ minNodes: 1, capabilities: lostNode.capabilities })
    .join()
  
  await reliablePool.addNode(replacement.nodes[0])
})
```

### 3. Cost-Optimized Pool
```typescript
// Build cheapest viable pool
const budgetPool = await sandrun
  .pool()
  .discover({
    pricing: { max: '0.5', currency: 'ETH', model: 'per-hour' }
  })
  .preferredPricing({
    strategy: 'spot-pricing', // Use spot instances
    maxPrice: '0.3'
  })
  .consensus('fastest-wins') // Lower consensus for cost savings
  .join()

// Batch multiple jobs for efficiency
const jobs = await Promise.all([
  createJob1(), createJob2(), createJob3()
])

const schedule = await budgetPool.schedule(jobs)
console.log(`Total estimated cost: ${schedule.totalCost}`)
```

### 4. Specialized Hardware Pool
```typescript
// Find nodes with specific hardware
const specializedPool = await sandrun
  .pool()
  .discover({
    capabilities: [{
      type: 'gpu',
      specs: { 
        type: 'h100', 
        memory: '80GB',
        interconnect: 'nvlink' 
      }
    }, {
      type: 'storage',
      specs: { 
        type: 'nvme', 
        capacity: '4TB',
        iops: 500000 
      }
    }]
  })
  .minBandwidth(10000) // 10 Gbps
  .region('us-east-1') // Low latency required
  .join()
```

## Node Operation Patterns

### 1. Basic Node Registration
```typescript
// Register a simple GPU node
const myNode = await sandrun
  .node()
  .alias('my-rtx4090-rig')
  .capabilities({
    gpu: { type: 'rtx4090', memory: '24GB', count: 2 },
    cpu: { cores: 16, threads: 32, frequency: 3600 },
    memory: { total: 64, type: 'ddr4', speed: 3200 },
    storage: { type: 'nvme', capacity: 2000, iops: 100000 },
    network: { bandwidth: 1000, latency: 10 }
  })
  .region('us-west-2')
  .pricing({
    gpu: { perHour: '0.5' },
    cpu: { perCoreHour: '0.02' },
    currency: 'ETH'
  })
  .register()

await myNode.startListening()
```

### 2. Professional Node Setup
```typescript
// High-end node with bonding and insurance
const proNode = await sandrun
  .node()
  .identity(process.env.NODE_PRIVATE_KEY)
  .alias('datacenter-a100-cluster')
  .capabilities({
    gpu: { 
      type: 'a100', 
      memory: '80GB', 
      count: 8,
      interconnect: 'nvlink'
    },
    cpu: { cores: 128, threads: 256, frequency: 2400 },
    memory: { total: 1024, type: 'ddr5', speed: 4800 },
    storage: { type: 'nvme', capacity: 20000, iops: 1000000 },
    network: { bandwidth: 25000, latency: 1 }
  })
  .region('us-east-1')
  .availability({
    schedule: '24/7',
    plannedMaintenance: ['2024-12-25T00:00:00Z']
  })
  .bondAmount('10.0', 'ETH') // Security deposit
  .insurance({
    provider: 'NodeGuard',
    coverage: '100.0',
    currency: 'ETH'
  })
  .pricing({
    gpu: { 
      perHour: '4.0',
      perJob: '0.1',
      discounts: {
        volume: [
          { threshold: 100, discount: 0.1 },
          { threshold: 1000, discount: 0.2 }
        ],
        duration: [
          { hours: 24, discount: 0.05 },
          { hours: 168, discount: 0.15 } // Weekly
        ]
      }
    },
    currency: 'ETH'
  })
  .register()

// Advanced monitoring
proNode.on('resource-low', async (resource) => {
  await sendAlert(`Low ${resource} on node ${proNode.id}`)
})

proNode.on('payment-received', (payment) => {
  console.log(`Received ${payment.amount} ${payment.currency}`)
})
```

### 3. Node Pool Management
```typescript
// Manage multiple nodes as a fleet
class NodeFleet {
  private nodes: Node[] = []
  
  async addNode(config: NodeCapabilities) {
    const node = await sandrun
      .node()
      .capabilities(config)
      .pricing(this.getOptimalPricing(config))
      .register()
    
    this.nodes.push(node)
    await node.startListening()
  }
  
  async rebalanceLoad() {
    const utilizations = await Promise.all(
      this.nodes.map(node => node.getUtilization())
    )
    
    // Move jobs from overloaded to underutilized nodes
    const overloaded = utilizations
      .filter(u => u.overall > 0.9)
      .map(u => u.nodeId)
      
    const underutilized = utilizations
      .filter(u => u.overall < 0.5)
      .map(u => u.nodeId)
    
    if (overloaded.length > 0 && underutilized.length > 0) {
      await this.redistributeJobs(overloaded, underutilized)
    }
  }
  
  async getFleetStatus() {
    const statuses = await Promise.all(
      this.nodes.map(node => node.status())
    )
    
    return {
      totalNodes: this.nodes.length,
      activeNodes: statuses.filter(s => s.online).length,
      totalEarnings: statuses.reduce((sum, s) => 
        sum + parseFloat(s.earnings.total), 0
      ),
      avgUtilization: statuses.reduce((sum, s) => 
        sum + s.utilization.overall, 0
      ) / statuses.length
    }
  }
}
```

This pool management system provides comprehensive abstractions for both pool operators and node providers, enabling efficient resource discovery, reliable job execution, and fair economic distribution in the decentralized compute marketplace.