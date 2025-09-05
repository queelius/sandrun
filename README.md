# Sandrun Fluidic API Architecture

A comprehensive TypeScript SDK that transforms the Sandrun distributed compute marketplace into an elegant, composable, and powerful development platform.

## Overview

Sandrun is a secure anonymous code execution service that has evolved into a distributed compute marketplace. This fluidic API provides elegant abstractions over the complex distributed infrastructure while maintaining backward compatibility and extensibility.

## Key Features

- **Fluent Interface**: Natural method chaining that reads like domain language
- **Immutable Builders**: Safe composition without side effects  
- **Plugin System**: Extensible middleware for custom behavior
- **Comprehensive Error Handling**: Graceful degradation with intelligent retry policies
- **Multi-Service Integration**: Seamless orchestration of HTTP API, blockchain, and IPFS
- **Type Safety**: Full TypeScript support with intelligent autocomplete
- **Cross-Platform**: Works in both browser and Node.js environments

## Quick Start

```typescript
import { sandrun } from '@sandrun/api'

// Simple GPU computation
const result = await sandrun
  .job()
  .code('console.log("Hello from GPU!")')
  .gpu({ type: 'cuda', memory: '2GB' })
  .payment('0.1', 'ETH')
  .submit()
  .then(execution => execution.wait())

// Advanced multi-node workflow
const pool = await sandrun
  .pool()
  .discover({ minGPUs: 4, region: 'us-east' })
  .consensus('2-of-3')
  .use(retryPlugin({ maxAttempts: 3 }))
  .join()

const training = await sandrun
  .job()
  .repository('https://github.com/user/ml-training')
  .gpu({ type: 'a100', count: 4, memory: '40GB' })
  .environment({ DATASET_URL: 'ipfs://...' })
  .payment('5.0', 'ETH')
  .with(pool)
  .submit()
```

## Architecture Documents

This repository contains the complete architectural design for the Sandrun Fluidic API:

### 1. [Core API Architecture](/home/spinoza/github/repos/sandrun/api-design.md)
**Foundation of the fluidic API with immutable builder patterns**
- Base class hierarchy and method chaining
- Type system and configuration management
- Plugin integration points
- Examples of fluent interface usage

### 2. [Job Submission Flows](/home/spinoza/github/repos/sandrun/job-flows.md)
**Comprehensive patterns for all types of compute workloads**
- Simple script execution
- Repository-based jobs with ML training
- Multi-stage pipelines with dependencies
- Distributed multi-node jobs
- Interactive and streaming workflows

### 3. [Pool Management System](/home/spinoza/github/repos/sandrun/pool-management.md)
**Distributed marketplace abstractions for nodes and pools**
- Node discovery and capability matching
- Pool orchestration and consensus management
- Node registration and lifecycle management
- Economic operations and payment distribution
- Health monitoring and fault tolerance

### 4. [Integration Layer](/home/spinoza/github/repos/sandrun/integration-layer.md)
**Seamless abstractions over infrastructure components**
- HTTP API wrapper with multipart uploads and WebSocket streaming
- Blockchain integration with Web3 providers and smart contracts
- IPFS gateway with pinning services and redundancy
- Service coordination and unified execution interface

### 5. [Plugin System](/home/spinoza/github/repos/sandrun/plugin-system.md)
**Extensibility framework for middleware and custom behavior**
- Plugin architecture with lifecycle hooks
- Built-in plugins: retry, logging, metrics, authentication
- Plugin development kit with decorators and utilities
- Examples of custom plugin development

### 6. [Error Handling](/home/spinoza/github/repos/sandrun/error-handling.md)
**Comprehensive fault tolerance and recovery mechanisms**
- Error hierarchy with intelligent classification
- Retry policies with exponential backoff
- Timeout management with adaptive strategies
- Circuit breaker pattern for fault isolation
- Global error handling and reporting

### 7. [Implementation Plan](/home/spinoza/github/repos/sandrun/implementation-plan.md)
**20-week roadmap with clear milestones and deliverables**
- Phase 1: Foundation (Weeks 1-4)
- Phase 2: Integration Layer (Weeks 5-8)  
- Phase 3: Advanced Features (Weeks 9-12)
- Phase 4: Testing and Optimization (Weeks 13-16)
- Phase 5: Production Readiness (Weeks 17-20)

## Design Principles

### 1. **Problem Definition First**
Every API component is designed with clear understanding of user problems and edge cases, supported by comprehensive test scenarios.

### 2. **Immutable Builder Pattern**
All builders return new instances, enabling safe method chaining and composition without side effects:

```typescript
const baseJob = sandrun.job().gpu({ type: 'cuda' })
const job1 = baseJob.memory(8).payment('1.0', 'ETH')
const job2 = baseJob.memory(16).payment('2.0', 'ETH')
// baseJob remains unchanged
```

### 3. **Layered Architecture**
Clean separation of concerns with well-defined interfaces between layers:
- Fluidic API Layer (user-facing builders)
- Plugin System (extensibility and middleware) 
- Integration Layer (service abstractions)
- Transport Layer (HTTP, Web3, IPFS clients)

### 4. **Comprehensive Error Handling**
Intelligent error classification, retry policies, and graceful degradation:

```typescript
const resilientJob = await sandrun
  .job()
  .retry('aggressive')
  .timeout({ execution: 7200000 })
  .circuitBreaker('gpu-nodes', { failureThreshold: 3 })
  .submit()
```

### 5. **Plugin-Based Extensibility**
Every aspect of the system can be extended through the plugin architecture:

```typescript
const enhanced = sandrun
  .use(retryPlugin({ maxAttempts: 3 }))
  .use(metricsPlugin())
  .use(customAuthPlugin())
```

## Target Use Cases

### 1. **Simple GPU Computation**
Quick one-off computations with minimal configuration

### 2. **ML Training Pipelines** 
Complex multi-stage workflows with data preprocessing, training, and evaluation

### 3. **Distributed Computing**
Large-scale parallel processing across multiple nodes with fault tolerance

### 4. **Interactive Development**
Jupyter-style interactive execution and streaming data processing

### 5. **Node Operation**
Professional compute node management with economics and monitoring

## Technical Specifications

- **Language**: TypeScript with full type safety
- **Build Targets**: ESM, CommonJS, UMD for universal compatibility
- **Bundle Size**: <100KB gzipped for core functionality
- **Performance**: <200ms P95 API response time
- **Test Coverage**: >95% with comprehensive integration tests
- **Documentation**: Complete API docs with interactive examples

## Success Metrics

- **Developer Experience**: <10 minutes from install to first job
- **API Adoption**: 80% of features used within 3 months
- **Performance**: Meets all established benchmarks
- **Reliability**: <5 bug reports per month post-launch
- **Community**: 100+ GitHub stars and 5+ contributors in first quarter

## Getting Started

The implementation plan provides a clear 20-week roadmap for building this comprehensive fluidic API. Each phase has specific deliverables, acceptance criteria, and risk mitigation strategies.

This architecture transforms the complexity of distributed compute into an elegant, discoverable API that scales from simple scripts to enterprise distributed workflows while maintaining the power and flexibility of the underlying Sandrun marketplace.

---

*This architecture document represents a comprehensive design for the Sandrun Fluidic API, providing the foundation for a transformative developer experience in distributed computing.*