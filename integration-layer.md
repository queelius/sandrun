# Sandrun Integration Layer Design

## Architecture Overview

The integration layer provides seamless abstractions over the underlying infrastructure components while maintaining backward compatibility and enabling extensibility.

```
┌─────────────────────────────────────────────────────────────┐
│                    Fluidic API Layer                       │
├─────────────────────────────────────────────────────────────┤
│                  Integration Layer                          │
│  ┌─────────────────┐ ┌─────────────────┐ ┌───────────────┐ │
│  │  HTTP Adapter   │ │ Blockchain API  │ │  IPFS Gateway │ │
│  │                 │ │                 │ │               │ │
│  │ • REST mapping  │ │ • Web3 provider │ │ • File upload │ │
│  │ • WebSocket     │ │ • Smart contracts│ │ • Pin management│ │
│  │ • Upload/Download│ │ • Event listening│ │ • Retrieval   │ │
│  └─────────────────┘ └─────────────────┘ └───────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                   Transport Layer                           │
│  ┌─────────────────┐ ┌─────────────────┐ ┌───────────────┐ │
│  │  HTTP Client    │ │   Web3.js/Ethers│ │   IPFS HTTP   │ │
│  │                 │ │                 │ │               │ │
│  │ • Axios/Fetch   │ │ • Wallet connect│ │ • Kubo API    │ │
│  │ • Request queue │ │ • Transaction   │ │ • Pinata/Web3 │ │
│  │ • Auth handling │ │   signing       │ │   Storage     │ │
│  └─────────────────┘ └─────────────────┘ └───────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## HTTP API Integration

### HTTPAdapter - Core REST API Wrapper
```typescript
interface HTTPAdapter {
  // Job management
  submitJob(job: JobSubmission): Promise<JobResponse>
  getJobStatus(jobId: string): Promise<JobStatus>
  getJobLogs(jobId: string, stream?: boolean): Promise<LogResponse>
  downloadResults(jobId: string): Promise<DownloadResponse>
  cancelJob(jobId: string): Promise<void>
  
  // Pool operations
  discoverNodes(criteria: DiscoveryCriteria): Promise<Node[]>
  getNodeInfo(nodeId: string): Promise<NodeInfo>
  registerNode(node: NodeRegistration): Promise<NodeResponse>
  
  // System operations
  getSystemStatus(): Promise<SystemStatus>
  getMetrics(): Promise<SystemMetrics>
}

class SandrunHTTPAdapter implements HTTPAdapter {
  private client: HTTPClient
  private config: HTTPConfig
  
  constructor(config: HTTPConfig) {
    this.client = new HTTPClient(config)
    this.config = config
  }
  
  async submitJob(job: JobSubmission): Promise<JobResponse> {
    // Handle multipart upload for large files
    const formData = this.createMultipartData(job)
    
    const response = await this.client.post('/submit', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      timeout: this.config.uploadTimeout || 300000, // 5 minutes
      onUploadProgress: job.onProgress
    })
    
    return this.mapJobResponse(response.data)
  }
  
  async getJobLogs(jobId: string, stream = false): Promise<LogResponse> {
    if (stream) {
      return this.streamLogs(jobId)
    }
    
    const response = await this.client.get(`/logs/${jobId}`)
    return response.data
  }
  
  private async streamLogs(jobId: string): Promise<LogResponse> {
    // WebSocket connection for real-time logs
    const ws = new WebSocket(`${this.config.wsEndpoint}/logs/${jobId}`)
    
    return new Promise((resolve, reject) => {
      const logs: string[] = []
      
      ws.onmessage = (event) => {
        const logEntry = JSON.parse(event.data)
        logs.push(logEntry.message)
        
        if (logEntry.type === 'completion') {
          resolve({ logs, complete: true })
        }
      }
      
      ws.onerror = reject
      ws.onclose = () => resolve({ logs, complete: false })
    })
  }
  
  private createMultipartData(job: JobSubmission): FormData {
    const form = new FormData()
    
    // Add job metadata
    form.append('metadata', JSON.stringify({
      gpu: job.gpu,
      memory: job.memory,
      timeout: job.timeout,
      environment: job.environment
    }))
    
    // Add code/files
    if (job.code) {
      form.append('code', new Blob([job.code], { type: 'text/plain' }))
    }
    
    if (job.files) {
      job.files.forEach((file, index) => {
        form.append(`file_${index}`, file)
      })
    }
    
    return form
  }
}

interface HTTPConfig {
  baseURL: string
  wsEndpoint?: string
  timeout?: number
  uploadTimeout?: number
  retries?: number
  auth?: AuthConfig
  rateLimit?: RateLimitConfig
}

interface AuthConfig {
  type: 'bearer' | 'api-key' | 'oauth' | 'custom'
  credentials: Record<string, string>
  refreshToken?: () => Promise<string>
}
```

### Request Queue and Rate Limiting
```typescript
class RequestQueue {
  private queue: QueueItem[] = []
  private processing = false
  private rateLimit: RateLimit
  
  constructor(config: RateLimitConfig) {
    this.rateLimit = new RateLimit(config)
  }
  
  async enqueue<T>(
    request: () => Promise<T>, 
    priority = 0,
    retryPolicy?: RetryPolicy
  ): Promise<T> {
    return new Promise((resolve, reject) => {
      this.queue.push({
        request,
        resolve,
        reject,
        priority,
        retryPolicy,
        attempts: 0,
        timestamp: Date.now()
      })
      
      this.queue.sort((a, b) => b.priority - a.priority)
      this.processQueue()
    })
  }
  
  private async processQueue() {
    if (this.processing || this.queue.length === 0) return
    
    this.processing = true
    
    while (this.queue.length > 0) {
      await this.rateLimit.wait()
      
      const item = this.queue.shift()!
      
      try {
        const result = await item.request()
        item.resolve(result)
      } catch (error) {
        if (this.shouldRetry(item, error)) {
          item.attempts++
          this.queue.unshift(item) // Retry at front of queue
        } else {
          item.reject(error)
        }
      }
    }
    
    this.processing = false
  }
}

class RateLimit {
  private tokens: number
  private lastRefill: number
  
  constructor(private config: RateLimitConfig) {
    this.tokens = config.maxTokens
    this.lastRefill = Date.now()
  }
  
  async wait(): Promise<void> {
    this.refillTokens()
    
    if (this.tokens >= 1) {
      this.tokens -= 1
      return
    }
    
    // Wait for next token
    const waitTime = (1000 / this.config.tokensPerSecond)
    await new Promise(resolve => setTimeout(resolve, waitTime))
    return this.wait()
  }
}
```

## Blockchain Integration

### Web3 Provider Abstraction
```typescript
interface BlockchainAdapter {
  // Job marketplace
  submitJobToMarketplace(job: MarketplaceJob): Promise<TransactionReceipt>
  claimJob(jobId: string, nodeId: string): Promise<TransactionReceipt>
  submitProof(jobId: string, proof: Proof): Promise<TransactionReceipt>
  
  // Consensus and verification
  voteOnConsensus(jobId: string, vote: ConsensusVote): Promise<TransactionReceipt>
  challengeResult(jobId: string, challenge: Challenge): Promise<TransactionReceipt>
  
  // Payments and escrow
  escrowPayment(jobId: string, amount: string): Promise<TransactionReceipt>
  releasePayment(jobId: string): Promise<TransactionReceipt>
  distributeRewards(jobId: string, distribution: PaymentDistribution): Promise<TransactionReceipt>
  
  // Events
  onJobSubmitted(callback: (event: JobSubmittedEvent) => void): void
  onJobCompleted(callback: (event: JobCompletedEvent) => void): void
  onPaymentReleased(callback: (event: PaymentEvent) => void): void
}

class SandrunBlockchainAdapter implements BlockchainAdapter {
  private web3: Web3
  private contracts: ContractRegistry
  private wallet: Wallet
  
  constructor(config: BlockchainConfig) {
    this.web3 = new Web3(config.provider)
    this.contracts = new ContractRegistry(config.contracts)
    this.wallet = new Wallet(config.privateKey || config.walletConnect)
  }
  
  async submitJobToMarketplace(job: MarketplaceJob): Promise<TransactionReceipt> {
    const jobContract = this.contracts.get('JobMarketplace')
    
    // Prepare job data for blockchain
    const jobData = {
      requirements: this.encodeRequirements(job.requirements),
      paymentAmount: this.web3.utils.toWei(job.payment.amount, 'ether'),
      consensusLevel: job.consensusLevel,
      timeout: job.timeout,
      codeHash: await this.calculateCodeHash(job.code)
    }
    
    // Estimate gas and submit transaction
    const gasEstimate = await jobContract.methods
      .submitJob(jobData)
      .estimateGas({ from: this.wallet.address })
    
    const tx = await jobContract.methods
      .submitJob(jobData)
      .send({
        from: this.wallet.address,
        gas: Math.floor(gasEstimate * 1.2), // 20% buffer
        gasPrice: await this.getOptimalGasPrice()
      })
    
    return tx
  }
  
  async submitProof(jobId: string, proof: Proof): Promise<TransactionReceipt> {
    const proofContract = this.contracts.get('ProofVerification')
    
    // Compress and hash proof data
    const proofData = {
      executionTrace: await this.compressTrace(proof.executionTrace),
      outputHash: proof.outputHash,
      resourceUsage: proof.resourceUsage,
      signature: await this.signProof(proof)
    }
    
    return await proofContract.methods
      .submitProof(jobId, proofData)
      .send({ from: this.wallet.address })
  }
  
  async voteOnConsensus(jobId: string, vote: ConsensusVote): Promise<TransactionReceipt> {
    const consensusContract = this.contracts.get('ConsensusManager')
    
    return await consensusContract.methods
      .submitVote(jobId, vote.accept, vote.proofHash, vote.signature)
      .send({ from: this.wallet.address })
  }
  
  // Event listening with reconnection
  onJobSubmitted(callback: (event: JobSubmittedEvent) => void): void {
    const jobContract = this.contracts.get('JobMarketplace')
    
    const subscription = jobContract.events.JobSubmitted()
      .on('data', callback)
      .on('error', (error) => {
        console.error('Event subscription error:', error)
        // Attempt reconnection
        setTimeout(() => this.onJobSubmitted(callback), 5000)
      })
    
    // Store subscription for cleanup
    this.eventSubscriptions.set('JobSubmitted', subscription)
  }
  
  private async getOptimalGasPrice(): Promise<string> {
    // Use EIP-1559 if available, otherwise legacy gas pricing
    const baseFee = await this.web3.eth.getBlock('pending')
      .then(block => block.baseFeePerGas)
    
    if (baseFee) {
      const priorityFee = this.web3.utils.toWei('2', 'gwei')
      return baseFee + priorityFee
    }
    
    return await this.web3.eth.getGasPrice()
  }
}

interface BlockchainConfig {
  provider: string | Web3Provider
  contracts: ContractAddresses
  privateKey?: string
  walletConnect?: WalletConnectConfig
  gasStrategy?: 'fast' | 'standard' | 'slow' | 'custom'
  confirmations?: number
}
```

### Smart Contract Interaction
```typescript
class ContractRegistry {
  private contracts = new Map<string, Contract>()
  private abis: Record<string, ABI>
  
  constructor(config: ContractConfig) {
    this.abis = config.abis
    this.loadContracts(config.addresses)
  }
  
  get(contractName: string): Contract {
    const contract = this.contracts.get(contractName)
    if (!contract) {
      throw new Error(`Contract ${contractName} not found`)
    }
    return contract
  }
  
  private loadContracts(addresses: ContractAddresses) {
    Object.entries(addresses).forEach(([name, address]) => {
      const abi = this.abis[name]
      if (abi) {
        const contract = new this.web3.eth.Contract(abi, address)
        this.contracts.set(name, contract)
      }
    })
  }
}

// Transaction management with retry and monitoring
class TransactionManager {
  private pendingTxs = new Map<string, PendingTransaction>()
  
  async submitTransaction(
    contract: Contract, 
    method: string, 
    args: any[],
    options: TransactionOptions = {}
  ): Promise<TransactionReceipt> {
    const txData = contract.methods[method](...args)
    
    // Estimate gas with buffer
    const gasEstimate = await txData.estimateGas({ from: options.from })
    const gas = Math.floor(gasEstimate * (options.gasBuffer || 1.2))
    
    // Submit transaction
    const tx = await txData.send({
      from: options.from,
      gas,
      gasPrice: options.gasPrice || await this.getGasPrice(),
      nonce: options.nonce || await this.getNonce(options.from)
    })
    
    // Monitor transaction
    this.pendingTxs.set(tx.transactionHash, {
      hash: tx.transactionHash,
      timestamp: Date.now(),
      retries: 0
    })
    
    return this.waitForConfirmation(tx.transactionHash, options.confirmations || 1)
  }
  
  private async waitForConfirmation(
    txHash: string, 
    confirmations: number
  ): Promise<TransactionReceipt> {
    return new Promise((resolve, reject) => {
      const checkConfirmation = async () => {
        try {
          const receipt = await this.web3.eth.getTransactionReceipt(txHash)
          
          if (receipt) {
            const currentBlock = await this.web3.eth.getBlockNumber()
            const confirmationCount = currentBlock - receipt.blockNumber
            
            if (confirmationCount >= confirmations) {
              this.pendingTxs.delete(txHash)
              resolve(receipt)
            } else {
              setTimeout(checkConfirmation, 2000) // Check every 2 seconds
            }
          } else {
            setTimeout(checkConfirmation, 2000)
          }
        } catch (error) {
          reject(error)
        }
      }
      
      checkConfirmation()
    })
  }
}
```

## IPFS Integration

### IPFS Gateway Abstraction
```typescript
interface IPFSAdapter {
  // File operations
  add(content: Buffer | string | File): Promise<IPFSAddResult>
  get(hash: string): Promise<Buffer>
  pin(hash: string): Promise<void>
  unpin(hash: string): Promise<void>
  
  // Directory operations
  addDirectory(files: FileTree): Promise<IPFSAddResult>
  getDirectory(hash: string): Promise<FileTree>
  
  // Metadata and information
  stat(hash: string): Promise<IPFSStat>
  list(hash: string): Promise<IPFSListResult>
  
  // Distributed storage
  replicate(hash: string, peers: string[]): Promise<void>
  findProviders(hash: string): Promise<string[]>
}

class SandrunIPFSAdapter implements IPFSAdapter {
  private client: IPFSHTTPClient
  private pinningService?: PinningService
  
  constructor(config: IPFSConfig) {
    this.client = create({
      url: config.apiEndpoint || 'http://localhost:5001',
      timeout: config.timeout || 60000
    })
    
    if (config.pinningService) {
      this.pinningService = new PinningService(config.pinningService)
    }
  }
  
  async add(content: Buffer | string | File): Promise<IPFSAddResult> {
    try {
      const result = await this.client.add(content, {
        pin: true,
        progress: (bytes) => console.log(`Uploaded: ${bytes} bytes`),
        cidVersion: 1 // Use CIDv1 for better compatibility
      })
      
      // Also pin to external service for redundancy
      if (this.pinningService) {
        await this.pinningService.pin(result.cid.toString())
      }
      
      return {
        hash: result.cid.toString(),
        size: result.size,
        path: result.path
      }
    } catch (error) {
      throw new IPFSError(`Failed to add content: ${error.message}`, error)
    }
  }
  
  async get(hash: string): Promise<Buffer> {
    try {
      // Try local node first
      const chunks = []
      for await (const chunk of this.client.cat(hash, { timeout: 10000 })) {
        chunks.push(chunk)
      }
      return Buffer.concat(chunks)
    } catch (localError) {
      // Fallback to gateway
      return this.getFromGateway(hash)
    }
  }
  
  private async getFromGateway(hash: string): Promise<Buffer> {
    const gateways = [
      'https://ipfs.io/ipfs/',
      'https://cloudflare-ipfs.com/ipfs/',
      'https://gateway.pinata.cloud/ipfs/'
    ]
    
    for (const gateway of gateways) {
      try {
        const response = await fetch(`${gateway}${hash}`, {
          timeout: 30000
        })
        
        if (response.ok) {
          return Buffer.from(await response.arrayBuffer())
        }
      } catch (error) {
        console.warn(`Gateway ${gateway} failed:`, error.message)
      }
    }
    
    throw new IPFSError(`Failed to retrieve content from all gateways: ${hash}`)
  }
  
  async addDirectory(files: FileTree): Promise<IPFSAddResult> {
    const entries = []
    
    for (const [path, content] of Object.entries(files)) {
      entries.push({
        path,
        content: typeof content === 'string' ? Buffer.from(content) : content
      })
    }
    
    const results = []
    for await (const result of this.client.addAll(entries, { 
      pin: true,
      wrapWithDirectory: true 
    })) {
      results.push(result)
    }
    
    // Return the root directory hash
    const rootResult = results.find(r => r.path === '')
    if (!rootResult) {
      throw new IPFSError('Failed to create directory structure')
    }
    
    return {
      hash: rootResult.cid.toString(),
      size: rootResult.size,
      path: rootResult.path
    }
  }
}

// Pinning service integration (Pinata, Web3.Storage, etc.)
class PinningService {
  private provider: string
  private config: PinningConfig
  
  constructor(config: PinningConfig) {
    this.provider = config.provider
    this.config = config
  }
  
  async pin(hash: string): Promise<void> {
    switch (this.provider) {
      case 'pinata':
        return this.pinToPinata(hash)
      case 'web3storage':
        return this.pinToWeb3Storage(hash)
      default:
        throw new Error(`Unknown pinning provider: ${this.provider}`)
    }
  }
  
  private async pinToPinata(hash: string): Promise<void> {
    const response = await fetch('https://api.pinata.cloud/pinning/pinByHash', {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${this.config.apiKey}`,
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        hashToPin: hash,
        pinataMetadata: {
          name: `sandrun-${hash.slice(0, 8)}`,
          keyvalues: {
            service: 'sandrun',
            timestamp: new Date().toISOString()
          }
        }
      })
    })
    
    if (!response.ok) {
      throw new IPFSError(`Pinning failed: ${response.statusText}`)
    }
  }
}
```

## Integration Orchestration

### Service Coordinator
```typescript
class SandrunServiceCoordinator {
  private http: HTTPAdapter
  private blockchain: BlockchainAdapter
  private ipfs: IPFSAdapter
  
  constructor(config: SandrunConfig) {
    this.http = new SandrunHTTPAdapter(config.http)
    this.blockchain = new SandrunBlockchainAdapter(config.blockchain)
    this.ipfs = new SandrunIPFSAdapter(config.ipfs)
  }
  
  async submitJob(job: JobSubmission): Promise<JobExecution> {
    // 1. Upload code/data to IPFS
    const codeHash = await this.ipfs.add(job.code)
    
    // 2. Submit to blockchain marketplace
    const marketplaceJob = {
      ...job,
      codeHash: codeHash.hash,
      timestamp: Date.now()
    }
    
    const blockchainTx = await this.blockchain.submitJobToMarketplace(marketplaceJob)
    
    // 3. Submit to HTTP API with blockchain reference
    const httpJob = {
      ...job,
      blockchainTxHash: blockchainTx.transactionHash,
      ipfsHash: codeHash.hash
    }
    
    const httpResponse = await this.http.submitJob(httpJob)
    
    // 4. Return unified execution interface
    return new IntegratedJobExecution(
      httpResponse.jobId,
      this.http,
      this.blockchain,
      this.ipfs
    )
  }
}

class IntegratedJobExecution implements JobExecution {
  constructor(
    public readonly id: string,
    private http: HTTPAdapter,
    private blockchain: BlockchainAdapter,
    private ipfs: IPFSAdapter
  ) {}
  
  async wait(): Promise<JobResult> {
    // Monitor both HTTP API and blockchain events
    const [httpResult, blockchainEvents] = await Promise.all([
      this.http.getJobStatus(this.id),
      this.waitForBlockchainCompletion()
    ])
    
    // Verify consensus between sources
    if (httpResult.status === 'completed' && blockchainEvents.completed) {
      const result = await this.downloadResults()
      return result
    }
    
    throw new Error('Job completion consensus failed')
  }
  
  private async waitForBlockchainCompletion(): Promise<BlockchainJobResult> {
    return new Promise((resolve) => {
      this.blockchain.onJobCompleted((event) => {
        if (event.jobId === this.id) {
          resolve(event)
        }
      })
    })
  }
}
```

This integration layer provides seamless abstraction over all infrastructure components while maintaining flexibility and reliability through multiple redundant pathways and consensus mechanisms.