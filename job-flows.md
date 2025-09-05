# Sandrun Job Submission Flow Patterns

## Flow Categories

### 1. Simple Script Execution
**Use Case**: Quick one-off computations, testing, prototyping
**Complexity**: Low - single node, basic resources

```typescript
// Basic CPU job
const result = await sandrun
  .job()
  .code(`
    const data = Array.from({length: 1000000}, () => Math.random())
    console.log(data.reduce((a, b) => a + b) / data.length)
  `)
  .submit()
  .then(exec => exec.wait())

// GPU acceleration
const gpuResult = await sandrun
  .job()
  .code(`
    import cupy as cp
    x = cp.random.random((10000, 10000))
    result = cp.linalg.norm(x)
    print(f"Matrix norm: {result}")
  `)
  .gpu({ type: 'cuda', memory: '4GB' })
  .payment('0.05', 'ETH')
  .submit()
```

### 2. Repository-Based Jobs
**Use Case**: Running existing codebases, ML training, complex applications
**Complexity**: Medium - version control, dependencies, configuration

```typescript
// ML training pipeline
const training = await sandrun
  .job()
  .repository('https://github.com/user/pytorch-training', 'v1.2.0')
  .gpu({ type: 'a100', memory: '40GB' })
  .environment({
    DATASET_URL: 'ipfs://QmX...',
    EPOCHS: '100',
    LEARNING_RATE: '0.001',
    WANDB_API_KEY: process.env.WANDB_API_KEY
  })
  .args(['--config', 'experiments/config.yaml'])
  .timeout(7200) // 2 hours
  .payment('2.5', 'ETH')
  .consensus('medium')
  .submit()

// Monitor training progress
training.logs().then(stream => {
  stream.on('data', chunk => {
    if (chunk.includes('Epoch')) {
      console.log(`Training: ${chunk}`)
    }
  })
})

const model = await training.wait()
```

### 3. Multi-Stage Pipeline
**Use Case**: Data preprocessing → Training → Inference → Post-processing
**Complexity**: High - job dependencies, data flow, conditional execution

```typescript
// Pipeline with explicit stages
class MLPipeline {
  constructor(private sandrun: Sandrun) {}
  
  async run(datasetUrl: string) {
    // Stage 1: Data preprocessing
    const preprocessing = await this.sandrun
      .job()
      .code(`
        import pandas as pd
        import numpy as np
        from dataset_utils import clean_data
        
        df = pd.read_csv('${datasetUrl}')
        cleaned = clean_data(df)
        cleaned.to_parquet('/tmp/processed.parquet')
      `)
      .memory(8192) // 8GB RAM
      .payment('0.1', 'ETH')
      .submit()
    
    const preprocessed = await preprocessing.wait()
    if (preprocessed.status !== 'completed') {
      throw new Error('Preprocessing failed')
    }
    
    // Stage 2: Model training (uses preprocessed data)
    const training = await this.sandrun
      .job()
      .repository('https://github.com/user/ml-models')
      .gpu({ type: 'a100', count: 2, memory: '80GB' })
      .environment({
        PREPROCESSED_DATA: preprocessed.output.files['processed.parquet'],
        MODEL_TYPE: 'transformer'
      })
      .payment('5.0', 'ETH')
      .consensus('high')
      .submit()
    
    // Stage 3: Model evaluation (parallel to training completion)
    const model = await training.wait()
    
    const evaluation = await this.sandrun
      .job()
      .code(`
        from model_eval import evaluate_model
        model_path = '${model.output.files['model.pt']}'
        metrics = evaluate_model(model_path)
        print(f"Accuracy: {metrics.accuracy}")
        print(f"F1 Score: {metrics.f1}")
      `)
      .gpu({ type: 'rtx4090', memory: '16GB' })
      .payment('0.5', 'ETH')
      .submit()
    
    return {
      model: model,
      metrics: await evaluation.wait()
    }
  }
}

// Usage
const pipeline = new MLPipeline(sandrun)
const results = await pipeline.run('ipfs://QmDataset123...')
```

### 4. Distributed Multi-Node Jobs
**Use Case**: Large-scale training, simulation, parallel processing
**Complexity**: Very High - node coordination, fault tolerance, consensus

```typescript
// Distributed training across multiple nodes
const distributedJob = await sandrun
  .job()
  .repository('https://github.com/user/distributed-training')
  .distributed({
    nodes: 4,
    coordination: 'parameter-server',
    communication: 'nccl'
  })
  .gpu({ 
    type: 'a100', 
    count: 4, // per node
    memory: '40GB',
    interconnect: 'nvlink' 
  })
  .environment({
    MASTER_PORT: '29500',
    WORLD_SIZE: '16', // 4 nodes × 4 GPUs
    DATASET_SHARDING: 'auto'
  })
  .payment('20.0', 'ETH')
  .consensus('3-of-5') // High reliability for expensive job
  .redundancy(2) // Run on 2 independent pools
  .submit()

// Advanced monitoring for distributed jobs
distributedJob.onNodeUpdate(nodeId, status => {
  console.log(`Node ${nodeId}: ${status}`)
})

distributedJob.onConsensus(result => {
  if (result.agreement < 0.8) {
    console.warn('Low consensus agreement:', result)
  }
})
```

### 5. Interactive/Streaming Jobs
**Use Case**: Real-time processing, interactive notebooks, streaming data
**Complexity**: Medium - persistent connections, state management

```typescript
// Jupyter-style interactive execution
const session = await sandrun
  .job()
  .interactive()
  .gpu({ type: 'rtx4090', memory: '16GB' })
  .environment({
    JUPYTER_TOKEN: 'secure-token-123'
  })
  .payment('1.0', 'ETH') // Pay per hour
  .billing('hourly')
  .submit()

// Execute cells interactively
const cell1 = await session.execute(`
  import torch
  device = torch.device('cuda')
  print(f"Using device: {device}")
`)

const cell2 = await session.execute(`
  x = torch.randn(1000, 1000, device=device)
  y = torch.mm(x, x.t())
  print(f"Result shape: {y.shape}")
`)

// Streaming data processing
const streamJob = await sandrun
  .job()
  .streaming()
  .code(`
    import asyncio
    import aioredis
    
    async def process_stream():
        redis = await aioredis.connect('redis://stream-source')
        async for message in redis.stream('data-stream'):
            result = process_message(message)
            await redis.publish('results', result)
  `)
  .payment('0.1', 'ETH')
  .billing('per-message')
  .submit()
```

## Job State Management

```typescript
interface JobExecution {
  // State tracking
  readonly state: JobState
  readonly transitions: StateTransition[]
  
  // Lifecycle methods
  async pause(): Promise<void>
  async resume(): Promise<void>
  async checkpoint(): Promise<Checkpoint>
  async restoreFrom(checkpoint: Checkpoint): Promise<void>
  
  // Resource scaling
  async scaleUp(resources: ResourceRequest): Promise<void>
  async scaleDown(): Promise<void>
  
  // Dependencies
  async waitFor(...dependencies: JobExecution[]): Promise<void>
  dependsOn(...jobs: JobExecution[]): JobExecution
}

type JobState = 
  | 'queued'
  | 'scheduling' 
  | 'downloading'
  | 'running'
  | 'paused'
  | 'checkpointing'
  | 'completing'
  | 'completed'
  | 'failed'
  | 'cancelled'
```

## Advanced Job Patterns

### Conditional Execution
```typescript
const pipeline = sandrun
  .job()
  .code('/* preprocessing */')
  .submit()
  .then(async preprocessing => {
    const result = await preprocessing.wait()
    
    // Conditional branching based on results
    if (result.output.includes('large_dataset')) {
      return sandrun
        .job()
        .gpu({ type: 'a100', count: 8 })
        .code('/* heavy processing */')
        .submit()
    } else {
      return sandrun
        .job()
        .gpu({ type: 'rtx4090', count: 2 })
        .code('/* light processing */')
        .submit()
    }
  })
```

### Error Recovery and Retries
```typescript
const robustJob = await sandrun
  .job()
  .repository('https://github.com/user/flaky-training')
  .gpu({ type: 'a100', memory: '40GB' })
  .payment('3.0', 'ETH')
  .use(retryPlugin({
    maxAttempts: 3,
    backoff: 'exponential',
    retryIf: error => error.code === 'CUDA_OUT_OF_MEMORY'
  }))
  .use(checkpointPlugin({
    interval: 300, // every 5 minutes
    strategy: 'incremental'
  }))
  .onFailure(async (error, execution) => {
    // Automatic downgrade on GPU memory issues
    if (error.code === 'GPU_MEMORY_ERROR') {
      return execution
        .clone()
        .gpu({ type: 'rtx4090', memory: '16GB' })
        .submit()
    }
    throw error
  })
  .submit()
```

### Batch Processing
```typescript
// Process multiple datasets in parallel
const datasets = ['dataset1.csv', 'dataset2.csv', 'dataset3.csv']

const batchResults = await Promise.all(
  datasets.map(dataset => 
    sandrun
      .job()
      .code(`process_dataset('${dataset}')`)
      .gpu({ type: 'rtx4090', memory: '8GB' })
      .payment('0.2', 'ETH')
      .submit()
      .then(exec => exec.wait())
  )
)

// Aggregate results
const finalResult = await sandrun
  .job()
  .code(`
    results = ${JSON.stringify(batchResults.map(r => r.output))}
    aggregate_and_analyze(results)
  `)
  .payment('0.1', 'ETH')
  .submit()
  .then(exec => exec.wait())
```

This comprehensive flow system supports everything from simple scripts to complex distributed workflows while maintaining consistent API patterns and providing powerful composition capabilities.