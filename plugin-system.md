# Sandrun Plugin System Architecture

## Plugin System Overview

The plugin system provides extensibility at every layer of the Sandrun API, enabling custom middleware, authentication providers, monitoring solutions, and workflow orchestration while maintaining type safety and performance.

## Core Plugin Architecture

### Plugin Interface
```typescript
interface Plugin {
  readonly name: string
  readonly version: string
  readonly dependencies?: PluginDependency[]
  
  // Lifecycle hooks
  onInit?(context: PluginContext): Promise<void> | void
  onDestroy?(): Promise<void> | void
  
  // API hooks
  onBuild?<T>(builder: SandrunBuilder<T>, context: BuildContext): void
  onSubmit?(job: Job, context: SubmissionContext): Promise<Job> | Job
  onExecute?(execution: JobExecution, context: ExecutionContext): void
  onResult?(result: JobResult, context: ResultContext): Promise<JobResult> | JobResult
  onError?(error: Error, context: ErrorContext): Promise<Error | void> | Error | void
  
  // Custom hooks
  [key: string]: any
}

interface PluginContext {
  config: SandrunConfig
  logger: Logger
  metrics: MetricsCollector
  storage: PluginStorage
}

interface PluginDependency {
  name: string
  version?: string
  optional?: boolean
}

// Plugin metadata for discovery and validation
interface PluginManifest {
  name: string
  version: string
  description: string
  author: string
  license: string
  keywords: string[]
  dependencies: PluginDependency[]
  apis: string[] // Which APIs this plugin extends
  permissions: Permission[]
  configuration: PluginConfigSchema
}
```

### Plugin Manager
```typescript
class PluginManager {
  private plugins = new Map<string, LoadedPlugin>()
  private hooks = new Map<string, HookRegistry>()
  private config: PluginManagerConfig
  
  constructor(config: PluginManagerConfig) {
    this.config = config
    this.setupCoreHooks()
  }
  
  async loadPlugin(plugin: Plugin | string, config?: any): Promise<void> {
    let pluginInstance: Plugin
    
    if (typeof plugin === 'string') {
      pluginInstance = await this.loadFromRegistry(plugin)
    } else {
      pluginInstance = plugin
    }
    
    // Validate plugin
    await this.validatePlugin(pluginInstance)
    
    // Check dependencies
    await this.resolveDependencies(pluginInstance)
    
    // Initialize plugin
    const context = this.createPluginContext(pluginInstance.name, config)
    
    if (pluginInstance.onInit) {
      await pluginInstance.onInit(context)
    }
    
    // Register plugin hooks
    this.registerPluginHooks(pluginInstance)
    
    // Store loaded plugin
    this.plugins.set(pluginInstance.name, {
      plugin: pluginInstance,
      context,
      loaded: Date.now(),
      config
    })
    
    this.emit('plugin-loaded', { name: pluginInstance.name })
  }
  
  async unloadPlugin(name: string): Promise<void> {
    const loadedPlugin = this.plugins.get(name)
    if (!loadedPlugin) {
      throw new Error(`Plugin ${name} is not loaded`)
    }
    
    // Call cleanup
    if (loadedPlugin.plugin.onDestroy) {
      await loadedPlugin.plugin.onDestroy()
    }
    
    // Unregister hooks
    this.unregisterPluginHooks(loadedPlugin.plugin)
    
    // Remove from registry
    this.plugins.delete(name)
    
    this.emit('plugin-unloaded', { name })
  }
  
  async executeHook<T>(
    hookName: string, 
    data: T, 
    context?: any
  ): Promise<T> {
    const registry = this.hooks.get(hookName)
    if (!registry) return data
    
    let result = data
    
    // Execute hooks in priority order
    const sortedHooks = [...registry.hooks].sort((a, b) => b.priority - a.priority)
    
    for (const hook of sortedHooks) {
      try {
        const hookResult = await hook.handler(result, context)
        if (hookResult !== undefined) {
          result = hookResult
        }
      } catch (error) {
        this.handleHookError(hook, error, context)
      }
    }
    
    return result
  }
  
  private registerPluginHooks(plugin: Plugin): void {
    Object.keys(plugin).forEach(key => {
      if (key.startsWith('on') && typeof plugin[key] === 'function') {
        this.registerHook(key, plugin[key], {
          plugin: plugin.name,
          priority: 0
        })
      }
    })
  }
  
  private registerHook(
    name: string, 
    handler: Function, 
    options: HookOptions = {}
  ): void {
    if (!this.hooks.has(name)) {
      this.hooks.set(name, { hooks: new Set() })
    }
    
    this.hooks.get(name)!.hooks.add({
      handler,
      priority: options.priority || 0,
      plugin: options.plugin,
      once: options.once || false
    })
  }
}

interface LoadedPlugin {
  plugin: Plugin
  context: PluginContext
  loaded: number
  config?: any
}

interface HookRegistry {
  hooks: Set<Hook>
}

interface Hook {
  handler: Function
  priority: number
  plugin?: string
  once: boolean
}
```

## Built-in Plugins

### Retry Plugin
```typescript
interface RetryOptions {
  maxAttempts?: number
  backoff?: 'linear' | 'exponential' | 'fixed'
  baseDelay?: number
  maxDelay?: number
  retryIf?: (error: Error) => boolean
  onRetry?: (attempt: number, error: Error) => void
}

class RetryPlugin implements Plugin {
  readonly name = 'retry'
  readonly version = '1.0.0'
  
  private options: RetryOptions
  
  constructor(options: RetryOptions = {}) {
    this.options = {
      maxAttempts: 3,
      backoff: 'exponential',
      baseDelay: 1000,
      maxDelay: 30000,
      retryIf: () => true,
      ...options
    }
  }
  
  onInit(context: PluginContext): void {
    context.logger.info('Retry plugin initialized', { options: this.options })
  }
  
  async onError(
    error: Error, 
    context: ErrorContext
  ): Promise<Error | void> {
    const { attempt = 0 } = context.metadata || {}
    
    if (attempt >= this.options.maxAttempts!) {
      return error // Max attempts reached
    }
    
    if (!this.options.retryIf!(error)) {
      return error // Error not retryable
    }
    
    // Calculate delay
    const delay = this.calculateDelay(attempt)
    
    // Notify retry
    if (this.options.onRetry) {
      this.options.onRetry(attempt + 1, error)
    }
    
    // Wait and retry
    await new Promise(resolve => setTimeout(resolve, delay))
    
    // Update context for retry
    context.metadata = { ...context.metadata, attempt: attempt + 1 }
    
    // Return void to indicate retry should happen
    return undefined
  }
  
  private calculateDelay(attempt: number): number {
    switch (this.options.backoff) {
      case 'fixed':
        return this.options.baseDelay!
      case 'linear':
        return this.options.baseDelay! * (attempt + 1)
      case 'exponential':
        return Math.min(
          this.options.baseDelay! * Math.pow(2, attempt),
          this.options.maxDelay!
        )
      default:
        return this.options.baseDelay!
    }
  }
}

// Factory function for easy usage
export function retryPlugin(options?: RetryOptions): Plugin {
  return new RetryPlugin(options)
}
```

### Metrics Plugin
```typescript
interface MetricsOptions {
  collector?: MetricsCollector
  prefix?: string
  labels?: Record<string, string>
  enabledMetrics?: string[]
}

class MetricsPlugin implements Plugin {
  readonly name = 'metrics'
  readonly version = '1.0.0'
  
  private collector: MetricsCollector
  private options: MetricsOptions
  
  constructor(options: MetricsOptions = {}) {
    this.options = {
      prefix: 'sandrun_',
      enabledMetrics: ['*'], // Enable all by default
      ...options
    }
    
    this.collector = options.collector || new DefaultMetricsCollector()
  }
  
  onInit(context: PluginContext): void {
    // Register default metrics
    this.registerMetrics()
    context.metrics = this.collector
  }
  
  onSubmit(job: Job, context: SubmissionContext): Job {
    this.collector.counter(`${this.options.prefix}jobs_submitted_total`)
      .labels(this.getJobLabels(job))
      .inc()
    
    this.collector.histogram(`${this.options.prefix}job_submission_duration`)
      .labels(this.getJobLabels(job))
      .observe(context.duration || 0)
    
    return job
  }
  
  onResult(result: JobResult, context: ResultContext): JobResult {
    this.collector.counter(`${this.options.prefix}jobs_completed_total`)
      .labels({
        status: result.status,
        gpu_type: result.execution?.gpu?.type || 'none'
      })
      .inc()
    
    if (result.executionTime) {
      this.collector.histogram(`${this.options.prefix}job_execution_duration`)
        .labels(this.getResultLabels(result))
        .observe(result.executionTime)
    }
    
    if (result.cost) {
      this.collector.histogram(`${this.options.prefix}job_cost`)
        .labels({ currency: result.cost.currency })
        .observe(parseFloat(result.cost.amount))
    }
    
    return result
  }
  
  onError(error: Error, context: ErrorContext): void {
    this.collector.counter(`${this.options.prefix}errors_total`)
      .labels({
        error_type: error.constructor.name,
        operation: context.operation || 'unknown'
      })
      .inc()
  }
  
  private registerMetrics(): void {
    // Pre-register common metrics
    this.collector.counter(`${this.options.prefix}jobs_submitted_total`, {
      help: 'Total number of jobs submitted',
      labelNames: ['gpu_type', 'consensus_level']
    })
    
    this.collector.counter(`${this.options.prefix}jobs_completed_total`, {
      help: 'Total number of completed jobs',
      labelNames: ['status', 'gpu_type']
    })
    
    this.collector.histogram(`${this.options.prefix}job_execution_duration`, {
      help: 'Job execution duration in seconds',
      labelNames: ['gpu_type', 'status'],
      buckets: [1, 5, 10, 30, 60, 300, 600, 1800, 3600]
    })
  }
  
  private getJobLabels(job: Job): Record<string, string> {
    return {
      gpu_type: job.gpu?.type || 'none',
      consensus_level: job.consensus || 'none',
      ...this.options.labels
    }
  }
}

export function metricsPlugin(options?: MetricsOptions): Plugin {
  return new MetricsPlugin(options)
}
```

### Logging Plugin
```typescript
interface LoggingOptions {
  level?: 'debug' | 'info' | 'warn' | 'error'
  format?: 'json' | 'text' | 'structured'
  outputs?: LogOutput[]
  includeContext?: boolean
}

class LoggingPlugin implements Plugin {
  readonly name = 'logging'
  readonly version = '1.0.0'
  
  private logger: Logger
  private options: LoggingOptions
  
  constructor(options: LoggingOptions = {}) {
    this.options = {
      level: 'info',
      format: 'structured',
      includeContext: true,
      ...options
    }
    
    this.logger = this.createLogger()
  }
  
  onInit(context: PluginContext): void {
    context.logger = this.logger
    this.logger.info('Logging plugin initialized', { options: this.options })
  }
  
  onSubmit(job: Job, context: SubmissionContext): Job {
    this.logger.info('Job submitted', {
      jobId: job.id,
      gpu: job.gpu,
      payment: job.payment,
      context: this.options.includeContext ? context : undefined
    })
    
    return job
  }
  
  onExecute(execution: JobExecution, context: ExecutionContext): void {
    this.logger.info('Job execution started', {
      jobId: execution.id,
      nodeId: context.nodeId,
      poolId: context.poolId
    })
  }
  
  onResult(result: JobResult, context: ResultContext): JobResult {
    this.logger.info('Job completed', {
      jobId: result.id,
      status: result.status,
      executionTime: result.executionTime,
      cost: result.cost
    })
    
    return result
  }
  
  onError(error: Error, context: ErrorContext): void {
    this.logger.error('Operation failed', {
      error: {
        message: error.message,
        stack: error.stack,
        type: error.constructor.name
      },
      context
    })
  }
  
  private createLogger(): Logger {
    return new StructuredLogger({
      level: this.options.level,
      format: this.options.format,
      outputs: this.options.outputs || [new ConsoleOutput()]
    })
  }
}

export function loggingPlugin(options?: LoggingOptions): Plugin {
  return new LoggingPlugin(options)
}
```

### Authentication Plugin
```typescript
interface AuthOptions {
  provider: AuthProvider
  tokenStorage?: TokenStorage
  refreshThreshold?: number // seconds before expiry to refresh
  autoRefresh?: boolean
}

class AuthPlugin implements Plugin {
  readonly name = 'auth'
  readonly version = '1.0.0'
  
  private provider: AuthProvider
  private tokenStorage: TokenStorage
  private options: AuthOptions
  
  constructor(options: AuthOptions) {
    this.options = {
      refreshThreshold: 300, // 5 minutes
      autoRefresh: true,
      ...options
    }
    
    this.provider = options.provider
    this.tokenStorage = options.tokenStorage || new MemoryTokenStorage()
  }
  
  async onInit(context: PluginContext): Promise<void> {
    // Load existing token
    const token = await this.tokenStorage.getToken()
    if (token && !this.isTokenExpired(token)) {
      this.setAuthHeader(token)
    } else {
      await this.authenticate()
    }
    
    // Setup auto-refresh
    if (this.options.autoRefresh) {
      this.setupTokenRefresh()
    }
  }
  
  async onSubmit(job: Job, context: SubmissionContext): Promise<Job> {
    // Ensure we have valid authentication
    await this.ensureValidAuth()
    
    return job
  }
  
  private async authenticate(): Promise<void> {
    const token = await this.provider.authenticate()
    await this.tokenStorage.storeToken(token)
    this.setAuthHeader(token)
  }
  
  private async ensureValidAuth(): Promise<void> {
    const token = await this.tokenStorage.getToken()
    
    if (!token || this.isTokenExpired(token) || this.shouldRefreshToken(token)) {
      await this.authenticate()
    }
  }
  
  private isTokenExpired(token: AuthToken): boolean {
    if (!token.expiresAt) return false
    return Date.now() >= token.expiresAt * 1000
  }
  
  private shouldRefreshToken(token: AuthToken): boolean {
    if (!token.expiresAt || !this.options.refreshThreshold) return false
    const threshold = (token.expiresAt * 1000) - (this.options.refreshThreshold * 1000)
    return Date.now() >= threshold
  }
}

// Built-in auth providers
export class WalletAuthProvider implements AuthProvider {
  constructor(private wallet: Wallet) {}
  
  async authenticate(): Promise<AuthToken> {
    const message = `Authenticate with Sandrun at ${Date.now()}`
    const signature = await this.wallet.signMessage(message)
    
    const response = await fetch('/auth/wallet', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        address: this.wallet.address,
        message,
        signature
      })
    })
    
    const data = await response.json()
    
    return {
      token: data.token,
      expiresAt: data.expiresAt,
      type: 'bearer'
    }
  }
}

export function authPlugin(options: AuthOptions): Plugin {
  return new AuthPlugin(options)
}
```

## Custom Plugin Development

### Plugin Development Kit
```typescript
// Base classes for plugin development
export abstract class BasePlugin implements Plugin {
  abstract readonly name: string
  abstract readonly version: string
  
  protected logger?: Logger
  protected metrics?: MetricsCollector
  protected config?: any
  
  onInit(context: PluginContext): void {
    this.logger = context.logger.child({ plugin: this.name })
    this.metrics = context.metrics
    this.config = context.config
  }
  
  protected log(level: string, message: string, data?: any): void {
    if (this.logger) {
      this.logger[level](message, { plugin: this.name, ...data })
    }
  }
  
  protected metric(name: string): any {
    return this.metrics?.counter(`${this.name}_${name}`)
  }
}

// Utility decorators for plugin methods
export function withMetrics(metricName: string) {
  return function(target: any, propertyKey: string, descriptor: PropertyDescriptor) {
    const original = descriptor.value
    
    descriptor.value = async function(...args: any[]) {
      const start = Date.now()
      
      try {
        const result = await original.apply(this, args)
        
        if (this.metrics) {
          this.metrics.histogram(`${this.name}_${metricName}_duration`)
            .observe(Date.now() - start)
          this.metrics.counter(`${this.name}_${metricName}_total`)
            .labels({ status: 'success' })
            .inc()
        }
        
        return result
      } catch (error) {
        if (this.metrics) {
          this.metrics.counter(`${this.name}_${metricName}_total`)
            .labels({ status: 'error' })
            .inc()
        }
        throw error
      }
    }
  }
}

export function withLogging(message?: string) {
  return function(target: any, propertyKey: string, descriptor: PropertyDescriptor) {
    const original = descriptor.value
    
    descriptor.value = function(...args: any[]) {
      if (this.logger) {
        this.logger.debug(message || `Executing ${propertyKey}`, { 
          plugin: this.name,
          args: args.length 
        })
      }
      
      return original.apply(this, args)
    }
  }
}
```

### Example Custom Plugin
```typescript
// Example: Job Scheduling Plugin
class SchedulingPlugin extends BasePlugin {
  readonly name = 'scheduling'
  readonly version = '1.0.0'
  
  private scheduler: JobScheduler
  
  constructor(options: SchedulingOptions) {
    super()
    this.scheduler = new JobScheduler(options)
  }
  
  @withLogging('Scheduling job submission')
  @withMetrics('job_scheduled')
  async onSubmit(job: Job, context: SubmissionContext): Promise<Job> {
    // Apply scheduling logic
    const scheduledJob = await this.scheduler.schedule(job, context)
    
    this.log('info', 'Job scheduled', {
      jobId: job.id,
      scheduledTime: scheduledJob.scheduledTime,
      priority: scheduledJob.priority
    })
    
    return scheduledJob
  }
  
  @withMetrics('job_queue_processed')
  async processQueue(): Promise<void> {
    const jobs = await this.scheduler.getQueuedJobs()
    
    for (const job of jobs) {
      if (this.scheduler.shouldExecute(job)) {
        await this.executeScheduledJob(job)
      }
    }
  }
}

// Usage
const schedulingPlugin = new SchedulingPlugin({
  strategy: 'priority-first',
  maxConcurrent: 10,
  queueSize: 1000
})
```

This plugin system provides comprehensive extensibility while maintaining type safety, performance, and ease of development for custom integrations and workflow enhancements.