# Sandrun Fluidic API Architecture

## Core Design Principles

### 1. Immutable Builder Pattern
- Each method returns a new instance (no mutation)
- Enables safe method chaining and composition
- Supports middleware injection at any point

### 2. Layered Architecture
```
┌─────────────────────────────────────┐
│           Client SDK                │
├─────────────────────────────────────┤
│       Fluidic API Layer             │
│  ┌─────────────┐ ┌─────────────────┐│
│  │   JobFlow   │ │   PoolManager   ││
│  └─────────────┘ └─────────────────┘│
├─────────────────────────────────────┤
│         Plugin System               │
│  ┌─────────┐ ┌─────────┐ ┌────────┐ │
│  │ Logging │ │ Metrics │ │ Retry  │ │
│  └─────────┘ └─────────┘ └────────┘ │
├─────────────────────────────────────┤
│        Integration Layer            │
│  ┌─────────┐ ┌─────────┐ ┌────────┐ │
│  │HTTP API │ │Blockchain│ │  IPFS  │ │
│  └─────────┘ └─────────┘ └────────┘ │
└─────────────────────────────────────┘
```

## Core Class Hierarchy

### Base Classes

```typescript
// Base builder with plugin support
abstract class SandrunBuilder<T> {
  protected config: SandrunConfig
  protected plugins: Plugin[]
  protected middleware: Middleware[]
  
  abstract build(): T
  
  use(plugin: Plugin): this
  with(middleware: Middleware): this
  configure(config: Partial<SandrunConfig>): this
}

// Main entry point
class Sandrun extends SandrunBuilder<SandrunClient> {
  static create(config?: SandrunConfig): Sandrun
  
  // Factory methods for different workflows
  job(): JobBuilder
  pool(): PoolBuilder  
  node(): NodeBuilder
  proof(): ProofBuilder
}
```

### Job Management Chain

```typescript
class JobBuilder extends SandrunBuilder<Job> {
  // Code specification
  code(source: string | File | Buffer): JobBuilder
  script(path: string): JobBuilder
  repository(url: string, ref?: string): JobBuilder
  
  // Resource requirements
  gpu(requirements: GPURequirements): JobBuilder
  memory(mb: number): JobBuilder
  timeout(seconds: number): JobBuilder
  
  // Execution context
  environment(vars: Record<string, string>): JobBuilder
  args(arguments: string[]): JobBuilder
  
  // Blockchain integration
  payment(amount: string, token?: string): JobBuilder
  consensus(level: ConsensusLevel): JobBuilder
  
  // Async operations
  async submit(): Promise<JobExecution>
  async estimate(): Promise<JobEstimate>
}

class JobExecution {
  readonly id: string
  readonly status: JobStatus
  
  // Monitoring
  async wait(): Promise<JobResult>
  async logs(): Promise<LogStream>
  async cancel(): Promise<void>
  
  // Results
  async download(): Promise<JobResult>
  async downloadTo(path: string): Promise<void>
}
```

### Pool Management Chain

```typescript
class PoolBuilder extends SandrunBuilder<Pool> {
  // Node discovery
  discover(criteria?: DiscoveryCriteria): PoolBuilder
  nodes(nodeIds: string[]): PoolBuilder
  
  // Pool configuration
  consensus(strategy: ConsensusStrategy): PoolBuilder
  redundancy(factor: number): PoolBuilder
  
  async join(): Promise<Pool>
}

class Pool {
  readonly nodes: Node[]
  readonly capabilities: Capability[]
  
  // Job distribution
  async distribute(job: Job): Promise<JobExecution[]>
  async verify(proofs: Proof[]): Promise<ConsensusResult>
}
```

## Method Chaining Examples

### Simple Job Submission
```typescript
const result = await sandrun
  .job()
  .code('console.log("Hello from GPU!")')
  .gpu({ type: 'cuda', memory: '2GB' })
  .payment('0.1', 'ETH')
  .submit()
  .then(execution => execution.wait())
```

### Complex Multi-Node Workflow
```typescript
const pool = await sandrun
  .pool()
  .discover({ minGPUs: 4, region: 'us-east' })
  .consensus('2-of-3')
  .use(retryPlugin({ maxAttempts: 3 }))
  .use(metricsPlugin())
  .join()

const execution = await sandrun
  .job()
  .repository('https://github.com/user/ml-training')
  .gpu({ type: 'a100', count: 4, memory: '40GB' })
  .environment({ DATASET_URL: 'ipfs://...' })
  .payment('5.0', 'ETH')
  .consensus('high')
  .with(pool)
  .submit()
```

### Node Registration
```typescript
const node = await sandrun
  .node()
  .capabilities({
    gpu: { type: 'rtx4090', memory: '24GB', count: 2 },
    cpu: { cores: 16, memory: '64GB' },
    storage: '1TB'
  })
  .region('us-west-2')
  .pricing({ gpu: '0.5/hour', cpu: '0.1/hour' })
  .register()
  
await node.startListening()
```

## Type System

```typescript
interface SandrunConfig {
  apiEndpoint?: string
  web3Provider?: Web3Provider
  ipfsGateway?: string
  authentication?: AuthConfig
  defaults?: {
    timeout?: number
    retries?: number
    consensus?: ConsensusLevel
  }
}

interface GPURequirements {
  type?: 'cuda' | 'opencl' | 'a100' | 'rtx4090' | string
  memory?: string | number
  count?: number
  compute?: number // compute capability
}

interface JobResult {
  id: string
  status: 'completed' | 'failed' | 'cancelled'
  output?: Buffer
  logs: string[]
  proof?: Proof
  cost: PaymentInfo
  executionTime: number
}

type ConsensusLevel = 'none' | 'low' | 'medium' | 'high' | '2-of-3' | '3-of-5'
```

## Plugin System Architecture

```typescript
interface Plugin {
  name: string
  version: string
  
  // Lifecycle hooks
  onInit?(config: SandrunConfig): Promise<void> | void
  onBuild?<T>(builder: SandrunBuilder<T>): void
  onSubmit?(job: Job): Promise<Job> | Job
  onResult?(result: JobResult): Promise<JobResult> | JobResult
  onError?(error: Error, context: any): Promise<Error> | Error
}

interface Middleware {
  execute<T>(
    next: () => Promise<T>,
    context: MiddlewareContext
  ): Promise<T>
}

// Built-in plugins
const retryPlugin = (options: RetryOptions): Plugin
const metricsPlugin = (collector?: MetricsCollector): Plugin
const loggingPlugin = (logger?: Logger): Plugin
const authPlugin = (provider: AuthProvider): Plugin
```

This architecture provides:

1. **Fluent Interface**: Natural method chaining that reads like domain language
2. **Immutability**: Safe composition without side effects
3. **Extensibility**: Plugin system for custom behavior
4. **Type Safety**: Full TypeScript support with intelligent autocomplete
5. **Async/Await**: Modern promise-based API for all operations
6. **Backward Compatibility**: Wraps existing HTTP API seamlessly

The design scales from simple single-job submissions to complex multi-node distributed workflows while maintaining consistent patterns throughout.