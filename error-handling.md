# Sandrun Error Handling and Recovery System

## Error Handling Philosophy

The Sandrun fluidic API implements comprehensive error handling that provides graceful degradation, intelligent retry policies, and transparent recovery mechanisms while maintaining system reliability in a distributed environment.

## Error Hierarchy and Classification

### Core Error Types
```typescript
// Base error class with enhanced context
abstract class SandrunError extends Error {
  readonly code: string
  readonly category: ErrorCategory
  readonly severity: ErrorSeverity
  readonly retryable: boolean
  readonly context: ErrorContext
  readonly timestamp: number
  
  constructor(
    message: string,
    code: string,
    category: ErrorCategory,
    options: ErrorOptions = {}
  ) {
    super(message)
    this.name = this.constructor.name
    this.code = code
    this.category = category
    this.severity = options.severity || 'medium'
    this.retryable = options.retryable ?? true
    this.context = options.context || {}
    this.timestamp = Date.now()
    
    // Preserve stack trace
    if (Error.captureStackTrace) {
      Error.captureStackTrace(this, this.constructor)
    }
  }
  
  toJSON() {
    return {
      name: this.name,
      message: this.message,
      code: this.code,
      category: this.category,
      severity: this.severity,
      retryable: this.retryable,
      context: this.context,
      timestamp: this.timestamp,
      stack: this.stack
    }
  }
}

type ErrorCategory = 
  | 'network'      // Connection, timeout, DNS issues
  | 'authentication' // Auth failures, token expiry
  | 'authorization'  // Permission denied, quota exceeded  
  | 'validation'     // Input validation, schema errors
  | 'resource'      // GPU unavailable, memory exhaustion
  | 'blockchain'    // Transaction failures, gas issues
  | 'consensus'     // Verification failures, disputes
  | 'system'        // Internal errors, unexpected failures
  | 'user'          // User-caused errors, invalid requests

type ErrorSeverity = 'low' | 'medium' | 'high' | 'critical'

interface ErrorOptions {
  severity?: ErrorSeverity
  retryable?: boolean
  context?: ErrorContext
  cause?: Error
}

interface ErrorContext {
  operation?: string
  jobId?: string
  nodeId?: string
  poolId?: string
  transactionHash?: string
  requestId?: string
  metadata?: Record<string, any>
}
```

### Specific Error Classes
```typescript
// Network-related errors
class NetworkError extends SandrunError {
  constructor(message: string, code: string, options?: ErrorOptions) {
    super(message, code, 'network', options)
  }
}

class TimeoutError extends NetworkError {
  readonly timeout: number
  
  constructor(operation: string, timeout: number, options?: ErrorOptions) {
    super(`Operation '${operation}' timed out after ${timeout}ms`, 'TIMEOUT', {
      ...options,
      context: { ...options?.context, operation, timeout }
    })
    this.timeout = timeout
  }
}

class ConnectionError extends NetworkError {
  readonly endpoint: string
  
  constructor(endpoint: string, cause?: Error, options?: ErrorOptions) {
    super(`Failed to connect to ${endpoint}`, 'CONNECTION_FAILED', {
      ...options,
      cause,
      context: { ...options?.context, endpoint }
    })
    this.endpoint = endpoint
  }
}

// Resource-related errors
class ResourceError extends SandrunError {
  constructor(message: string, code: string, options?: ErrorOptions) {
    super(message, code, 'resource', options)
  }
}

class GPUUnavailableError extends ResourceError {
  readonly requiredGPU: GPURequirements
  readonly availableNodes: string[]
  
  constructor(
    requirements: GPURequirements, 
    availableNodes: string[] = [],
    options?: ErrorOptions
  ) {
    super(
      `No GPU matching requirements: ${JSON.stringify(requirements)}`,
      'GPU_UNAVAILABLE',
      {
        ...options,
        retryable: availableNodes.length === 0, // Only retry if no nodes at all
        context: { 
          ...options?.context, 
          requiredGPU: requirements,
          availableNodes: availableNodes.length 
        }
      }
    )
    this.requiredGPU = requirements
    this.availableNodes = availableNodes
  }
}

class InsufficientFundsError extends ResourceError {
  readonly required: string
  readonly available: string
  readonly currency: string
  
  constructor(
    required: string, 
    available: string, 
    currency: string,
    options?: ErrorOptions
  ) {
    super(
      `Insufficient funds: need ${required} ${currency}, have ${available} ${currency}`,
      'INSUFFICIENT_FUNDS',
      {
        ...options,
        retryable: false, // User needs to add funds
        severity: 'high'
      }
    )
    this.required = required
    this.available = available
    this.currency = currency
  }
}

// Blockchain-related errors
class BlockchainError extends SandrunError {
  readonly transactionHash?: string
  readonly blockNumber?: number
  
  constructor(message: string, code: string, options?: ErrorOptions & {
    transactionHash?: string
    blockNumber?: number
  }) {
    super(message, code, 'blockchain', options)
    this.transactionHash = options?.transactionHash
    this.blockNumber = options?.blockNumber
  }
}

class GasEstimationError extends BlockchainError {
  constructor(cause: Error, options?: ErrorOptions) {
    super('Failed to estimate gas for transaction', 'GAS_ESTIMATION_FAILED', {
      ...options,
      cause,
      retryable: true,
      severity: 'medium'
    })
  }
}

class TransactionFailedError extends BlockchainError {
  readonly gasUsed: number
  readonly reason?: string
  
  constructor(
    txHash: string, 
    gasUsed: number, 
    reason?: string,
    options?: ErrorOptions
  ) {
    super(
      `Transaction failed: ${reason || 'Unknown reason'}`,
      'TRANSACTION_FAILED',
      {
        ...options,
        transactionHash: txHash,
        retryable: false, // Don't retry failed transactions
        severity: 'high'
      }
    )
    this.gasUsed = gasUsed
    this.reason = reason
  }
}

// Consensus and verification errors
class ConsensusError extends SandrunError {
  readonly jobId: string
  readonly agreementLevel: number
  readonly requiredLevel: number
  
  constructor(
    jobId: string,
    agreementLevel: number,
    requiredLevel: number,
    options?: ErrorOptions
  ) {
    super(
      `Consensus failed: ${agreementLevel}% agreement, ${requiredLevel}% required`,
      'CONSENSUS_FAILED',
      {
        ...options,
        category: 'consensus',
        retryable: true,
        severity: 'high',
        context: { 
          ...options?.context, 
          jobId, 
          agreementLevel, 
          requiredLevel 
        }
      }
    )
    this.jobId = jobId
    this.agreementLevel = agreementLevel
    this.requiredLevel = requiredLevel
  }
}

class ProofVerificationError extends SandrunError {
  readonly proofHash: string
  readonly nodeId: string
  readonly reason: string
  
  constructor(
    proofHash: string,
    nodeId: string, 
    reason: string,
    options?: ErrorOptions
  ) {
    super(
      `Proof verification failed: ${reason}`,
      'PROOF_VERIFICATION_FAILED',
      {
        ...options,
        category: 'consensus',
        retryable: false, // Proof is either valid or not
        severity: 'high'
      }
    )
    this.proofHash = proofHash
    this.nodeId = nodeId
    this.reason = reason
  }
}
```

## Retry Policy System

### Retry Policy Configuration
```typescript
interface RetryPolicy {
  maxAttempts: number
  backoff: BackoffStrategy
  retryIf: (error: Error, attempt: number) => boolean
  onRetry?: (error: Error, attempt: number, delay: number) => void
  onFailure?: (error: Error, attempts: number) => void
  jitter?: boolean // Add randomness to prevent thundering herd
}

interface BackoffStrategy {
  type: 'fixed' | 'linear' | 'exponential' | 'custom'
  baseDelay: number
  maxDelay?: number
  multiplier?: number // For exponential/linear
  customFn?: (attempt: number) => number
}

// Built-in retry policies
const RetryPolicies = {
  // Conservative policy for critical operations
  conservative: {
    maxAttempts: 3,
    backoff: {
      type: 'exponential',
      baseDelay: 1000,
      maxDelay: 30000,
      multiplier: 2
    },
    retryIf: (error) => error instanceof NetworkError || error instanceof TimeoutError,
    jitter: true
  } as RetryPolicy,
  
  // Aggressive policy for non-critical operations
  aggressive: {
    maxAttempts: 5,
    backoff: {
      type: 'exponential',
      baseDelay: 500,
      maxDelay: 15000,
      multiplier: 1.5
    },
    retryIf: (error) => {
      if (!error.retryable) return false
      if (error instanceof InsufficientFundsError) return false
      return error.category === 'network' || 
             error.category === 'resource' ||
             error.category === 'blockchain'
    },
    jitter: true
  } as RetryPolicy,
  
  // Blockchain-specific policy
  blockchain: {
    maxAttempts: 4,
    backoff: {
      type: 'linear',
      baseDelay: 2000,
      maxDelay: 20000,
      multiplier: 1.5
    },
    retryIf: (error) => {
      if (error instanceof TransactionFailedError) return false
      return error instanceof GasEstimationError ||
             error instanceof ConnectionError
    },
    jitter: false // Blockchain timing is important
  } as RetryPolicy
}
```

### Retry Execution Engine
```typescript
class RetryExecutor {
  private policies = new Map<string, RetryPolicy>()
  
  constructor() {
    // Register default policies
    Object.entries(RetryPolicies).forEach(([name, policy]) => {
      this.policies.set(name, policy)
    })
  }
  
  registerPolicy(name: string, policy: RetryPolicy): void {
    this.policies.set(name, policy)
  }
  
  async execute<T>(
    operation: () => Promise<T>,
    policyName: string = 'conservative',
    context?: ExecutionContext
  ): Promise<T> {
    const policy = this.policies.get(policyName)
    if (!policy) {
      throw new Error(`Unknown retry policy: ${policyName}`)
    }
    
    let lastError: Error
    let attempt = 0
    
    while (attempt < policy.maxAttempts) {
      try {
        return await operation()
      } catch (error) {
        lastError = error
        attempt++
        
        // Check if we should retry
        if (attempt >= policy.maxAttempts || !policy.retryIf(error, attempt)) {
          break
        }
        
        // Calculate delay
        const delay = this.calculateDelay(policy.backoff, attempt)
        
        // Notify retry
        if (policy.onRetry) {
          policy.onRetry(error, attempt, delay)
        }
        
        // Wait before retry
        await this.delay(delay)
      }
    }
    
    // All retries exhausted
    if (policy.onFailure) {
      policy.onFailure(lastError, attempt)
    }
    
    throw new RetryExhaustedError(lastError, attempt, policyName)
  }
  
  private calculateDelay(backoff: BackoffStrategy, attempt: number): number {
    let delay: number
    
    switch (backoff.type) {
      case 'fixed':
        delay = backoff.baseDelay
        break
      case 'linear':
        delay = backoff.baseDelay * attempt * (backoff.multiplier || 1)
        break
      case 'exponential':
        delay = backoff.baseDelay * Math.pow(backoff.multiplier || 2, attempt - 1)
        break
      case 'custom':
        delay = backoff.customFn!(attempt)
        break
      default:
        delay = backoff.baseDelay
    }
    
    // Apply max delay limit
    if (backoff.maxDelay) {
      delay = Math.min(delay, backoff.maxDelay)
    }
    
    return delay
  }
  
  private delay(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms))
  }
}

class RetryExhaustedError extends SandrunError {
  readonly originalError: Error
  readonly attempts: number
  readonly policy: string
  
  constructor(originalError: Error, attempts: number, policy: string) {
    super(
      `Retry exhausted after ${attempts} attempts using policy '${policy}': ${originalError.message}`,
      'RETRY_EXHAUSTED',
      'system',
      {
        cause: originalError,
        retryable: false,
        severity: 'high',
        context: { originalError: originalError.message, attempts, policy }
      }
    )
    this.originalError = originalError
    this.attempts = attempts
    this.policy = policy
  }
}
```

## Timeout Management

### Timeout Configuration and Enforcement
```typescript
interface TimeoutConfig {
  connection?: number    // Connection establishment timeout
  upload?: number       // File upload timeout
  execution?: number    // Job execution timeout
  download?: number     // Result download timeout
  consensus?: number    // Consensus formation timeout
  blockchain?: number   // Blockchain transaction timeout
}

class TimeoutManager {
  private defaultTimeouts: Required<TimeoutConfig> = {
    connection: 10000,    // 10 seconds
    upload: 300000,      // 5 minutes
    execution: 3600000,  // 1 hour
    download: 120000,    // 2 minutes
    consensus: 600000,   // 10 minutes
    blockchain: 300000   // 5 minutes
  }
  
  constructor(customTimeouts: Partial<TimeoutConfig> = {}) {
    this.defaultTimeouts = { ...this.defaultTimeouts, ...customTimeouts }
  }
  
  async withTimeout<T>(
    promise: Promise<T>,
    timeoutMs: number,
    operation: string,
    onTimeout?: () => void
  ): Promise<T> {
    let timeoutHandle: NodeJS.Timeout
    
    const timeoutPromise = new Promise<never>((_, reject) => {
      timeoutHandle = setTimeout(() => {
        if (onTimeout) {
          onTimeout()
        }
        reject(new TimeoutError(operation, timeoutMs))
      }, timeoutMs)
    })
    
    try {
      const result = await Promise.race([promise, timeoutPromise])
      clearTimeout(timeoutHandle)
      return result
    } catch (error) {
      clearTimeout(timeoutHandle)
      throw error
    }
  }
  
  getTimeout(operation: keyof TimeoutConfig): number {
    return this.defaultTimeouts[operation]
  }
  
  // Adaptive timeout based on historical performance
  getAdaptiveTimeout(
    operation: keyof TimeoutConfig, 
    context?: AdaptiveContext
  ): number {
    const baseTimeout = this.getTimeout(operation)
    
    if (!context) {
      return baseTimeout
    }
    
    // Adjust based on historical performance
    let multiplier = 1.0
    
    if (context.avgExecutionTime) {
      // If average execution time is known, use it as basis
      multiplier = Math.max(1.0, context.avgExecutionTime / baseTimeout * 2)
    }
    
    if (context.nodeReliability !== undefined) {
      // Less reliable nodes get more time
      multiplier *= (2.0 - context.nodeReliability)
    }
    
    if (context.networkLatency) {
      // High latency networks get more time
      multiplier *= Math.max(1.0, context.networkLatency / 100)
    }
    
    return Math.min(baseTimeout * multiplier, baseTimeout * 5) // Cap at 5x
  }
}

interface AdaptiveContext {
  avgExecutionTime?: number
  nodeReliability?: number // 0-1 scale
  networkLatency?: number  // milliseconds
  previousFailures?: number
}
```

## Circuit Breaker Pattern

### Circuit Breaker for Fault Tolerance
```typescript
enum CircuitState {
  CLOSED = 'closed',     // Normal operation
  OPEN = 'open',         // Failing fast
  HALF_OPEN = 'half_open' // Testing recovery
}

interface CircuitBreakerConfig {
  failureThreshold: number    // Number of failures to open circuit
  recoveryTimeout: number     // Time to wait before attempting recovery
  successThreshold: number    // Successes needed to close circuit in half-open
  monitoringWindow: number    // Time window to track failures
}

class CircuitBreaker {
  private state = CircuitState.CLOSED
  private failures = 0
  private successes = 0
  private lastFailureTime = 0
  private nextAttemptTime = 0
  
  constructor(
    private name: string,
    private config: CircuitBreakerConfig
  ) {}
  
  async execute<T>(operation: () => Promise<T>): Promise<T> {
    if (this.state === CircuitState.OPEN) {
      if (Date.now() < this.nextAttemptTime) {
        throw new CircuitOpenError(this.name, this.nextAttemptTime - Date.now())
      }
      // Transition to half-open
      this.state = CircuitState.HALF_OPEN
      this.successes = 0
    }
    
    try {
      const result = await operation()
      this.onSuccess()
      return result
    } catch (error) {
      this.onFailure()
      throw error
    }
  }
  
  private onSuccess(): void {
    this.failures = 0
    
    if (this.state === CircuitState.HALF_OPEN) {
      this.successes++
      if (this.successes >= this.config.successThreshold) {
        this.state = CircuitState.CLOSED
      }
    }
  }
  
  private onFailure(): void {
    this.failures++
    this.lastFailureTime = Date.now()
    
    if (this.state === CircuitState.HALF_OPEN) {
      // Failed during recovery attempt
      this.state = CircuitState.OPEN
      this.nextAttemptTime = Date.now() + this.config.recoveryTimeout
    } else if (this.failures >= this.config.failureThreshold) {
      // Too many failures, open the circuit
      this.state = CircuitState.OPEN
      this.nextAttemptTime = Date.now() + this.config.recoveryTimeout
    }
  }
  
  getState(): CircuitState {
    return this.state
  }
  
  getFailureCount(): number {
    return this.failures
  }
  
  reset(): void {
    this.state = CircuitState.CLOSED
    this.failures = 0
    this.successes = 0
    this.nextAttemptTime = 0
  }
}

class CircuitOpenError extends SandrunError {
  constructor(circuitName: string, retryAfterMs: number) {
    super(
      `Circuit breaker '${circuitName}' is open, retry after ${retryAfterMs}ms`,
      'CIRCUIT_OPEN',
      'system',
      {
        retryable: true,
        severity: 'medium',
        context: { circuitName, retryAfterMs }
      }
    )
  }
}
```

## Integration with Fluidic API

### Error-Aware Builder Pattern
```typescript
class ErrorAwareBuilder<T> extends SandrunBuilder<T> {
  protected retryPolicy: string = 'conservative'
  protected timeoutConfig: Partial<TimeoutConfig> = {}
  protected circuitBreakers = new Map<string, CircuitBreaker>()
  
  retry(policy: string | RetryPolicy): this {
    if (typeof policy === 'string') {
      this.retryPolicy = policy
    } else {
      // Register custom policy
      const policyName = `custom_${Date.now()}`
      retryExecutor.registerPolicy(policyName, policy)
      this.retryPolicy = policyName
    }
    return this.clone()
  }
  
  timeout(config: Partial<TimeoutConfig>): this {
    const newBuilder = this.clone()
    newBuilder.timeoutConfig = { ...this.timeoutConfig, ...config }
    return newBuilder
  }
  
  circuitBreaker(name: string, config: CircuitBreakerConfig): this {
    const newBuilder = this.clone()
    newBuilder.circuitBreakers.set(name, new CircuitBreaker(name, config))
    return newBuilder
  }
  
  protected async executeWithResilience<R>(
    operation: () => Promise<R>,
    operationType: keyof TimeoutConfig = 'execution'
  ): Promise<R> {
    const timeout = this.timeoutConfig[operationType] || 
                   timeoutManager.getTimeout(operationType)
    
    const timedOperation = () => timeoutManager.withTimeout(
      operation(),
      timeout,
      operationType
    )
    
    // Apply circuit breaker if configured
    const circuitBreaker = this.circuitBreakers.get(operationType)
    const finalOperation = circuitBreaker ? 
      () => circuitBreaker.execute(timedOperation) :
      timedOperation
    
    // Apply retry policy
    return retryExecutor.execute(finalOperation, this.retryPolicy)
  }
}

// Usage examples
const resilientJob = await sandrun
  .job()
  .code('gpu_computation()')
  .retry('aggressive')
  .timeout({ execution: 7200000 }) // 2 hours
  .circuitBreaker('gpu-nodes', {
    failureThreshold: 3,
    recoveryTimeout: 30000,
    successThreshold: 2,
    monitoringWindow: 300000
  })
  .submit()
```

### Global Error Handling
```typescript
class GlobalErrorHandler {
  private handlers = new Map<string, ErrorHandler[]>()
  private fallbackHandler?: ErrorHandler
  
  register(errorCode: string, handler: ErrorHandler): void {
    if (!this.handlers.has(errorCode)) {
      this.handlers.set(errorCode, [])
    }
    this.handlers.get(errorCode)!.push(handler)
  }
  
  setFallbackHandler(handler: ErrorHandler): void {
    this.fallbackHandler = handler
  }
  
  async handle(error: SandrunError): Promise<void> {
    const handlers = this.handlers.get(error.code) || []
    
    for (const handler of handlers) {
      try {
        await handler(error)
      } catch (handlerError) {
        console.error('Error handler failed:', handlerError)
      }
    }
    
    if (handlers.length === 0 && this.fallbackHandler) {
      await this.fallbackHandler(error)
    }
  }
}

type ErrorHandler = (error: SandrunError) => Promise<void> | void

// Built-in error handlers
const builtInHandlers = {
  logError: (error: SandrunError) => {
    console.error('Sandrun Error:', error.toJSON())
  },
  
  notifyUser: async (error: SandrunError) => {
    if (error.severity === 'high' || error.severity === 'critical') {
      // Send notification to user
      await sendNotification({
        type: 'error',
        message: error.message,
        code: error.code
      })
    }
  },
  
  reportMetrics: (error: SandrunError) => {
    metrics.counter('sandrun_errors_total')
      .labels({
        code: error.code,
        category: error.category,
        severity: error.severity
      })
      .inc()
  }
}
```

This comprehensive error handling system provides robust fault tolerance while maintaining transparency and control for users of the Sandrun fluidic API.