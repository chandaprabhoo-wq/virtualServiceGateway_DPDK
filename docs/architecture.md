# vSG Design & Architecture Documentation

## Overview

The vSG (Virtual Service Gateway) is a high-performance packet processing data plane built with DPDK, optimized for telecom service provider use cases including billing, QoE monitoring, and traffic classification.

## Core Components

### 1. DPDK Packet Processing (`src/dpdk/`)

**PMD Driver** (`pmd_driver.cpp`): Poll Mode Driver abstraction
- EAL (Environment Abstraction Layer) initialization
- Port configuration and management
- Hardware offload support (checksum, TSO, etc.)
- Target: Wire-speed forwarding with minimal latency

**Memory Pool Manager** (`mempool_manager.cpp`): Packet buffer allocation
- Pre-allocated mempools per NUMA node
- Per-lcore caching for reduced contention
- Efficient buffer lifecycle management
- Memory pool size: 65K packets/core (typical ~1GB)

**Ring Queues** (`ring_queue.cpp`): Lock-free inter-core communication
- DPDK ring buffers for worker-to-worker communication
- Single-producer/consumer optimizations
- Burst enqueue/dequeue for batching
- Ring size: 4K entries (scales linearly with throughput)

**Batch Processor** (`batch_processor.cpp`): Cache-friendly packet processing
- Packet burst processing (64-pkt bursts typical)
- Prefetching for cache optimization
- Metadata extraction and tagging
- Branch prediction optimization

**Worker Pipeline** (`worker_pipeline.cpp`): Multi-stage processing
- Per-lcore worker threads
- Queue-to-core mapping strategies
- Multi-stage pipeline with backpressure
- Graceful shutdown and stats collection

### 2. UBB Metering (`src/metering/`)

**UBBMeter** (`ubb_meter.cpp`): Usage-Based Billing accounting
- Per-subscriber, per-service byte counters
- Atomic lock-free updates
- Directional tracking (uplink/downlink)
- Support for millions of active subscribers

**Counter Manager** (`counter_manager.cpp`): Lifecycle management
- Counter creation and cleanup
- Idle flow detection
- Anomaly detection for billing disputes

**Reconciliation** (`reconciliation.cpp`): Billing correctness
- Compare with external systems (AAA, DPI)
- Detect and flag discrepancies
- Audit trail generation

### 3. QoE Analytics (`src/analytics/`)

**QoE Collector** (`qoe_collector.cpp`): Quality of Experience tracking
- Flow-level KPI collection (loss, latency, throughput)
- Telemetry sample recording
- Service-level aggregation
- High-cardinality safe sampling

**Flow KPI Tracker** (`flow_kpi.cpp`): Per-flow metrics
- Packet counts and byte counts
- Loss percentage and jitter
- RTT and latency percentiles
- Flow aging and cleanup

**Telemetry Aggregator** (`telemetry_aggregator.cpp`): Time-series aggregation
- Configurable aggregation windows
- Safe high-cardinality bucketing
- Export to time-series DBs (Prometheus, InfluxDB)
- Sampling strategies to limit cardinality

### 4. DPI Classification (`src/dpi/`)

**DPI Classifier** (`classifier_pipeline.cpp`): Traffic classification
- Pattern-based signature matching
- Timeout-protected classification
- Performance guardrails and degradation mode
- Stateless classification for fast path

**Signature Manager** (`signature_manager.cpp`): Signature lifecycle
- Load signatures from files
- Runtime signature updates
- Priority-based matching
- Protocol versioning support

**DPI Worker** (`dpi_worker.cpp`): Classification pipeline stage
- Packet classification batching
- Performance isolation
- Timeout handling
- Metadata tagging with classification

## Performance Architecture

### Multi-Core Scaling

**Design**: Scale out, not up
- Each core processes independent flows
- Queue-to-core mapping via hashing
- Flow pinning to reduce cache line bouncing
- NUMA awareness for socket-local processing

**Queue Mapping Strategy**:
```
Port 0 RX Q0 → Core 0 → Port 1 TX Q0
Port 0 RX Q1 → Core 1 → Port 1 TX Q1
Port 0 RX Q2 → Core 2 → Port 1 TX Q2
Port 0 RX Q3 → Core 3 → Port 1 TX Q3
```

**Flow Hashing**: 5-tuple (src_ip, dst_ip, src_port, dst_port, proto) → consistent queue assignment

### Memory & Cache Optimization

**Hugepages**: Reduce TLB misses
- 1024 hugepages (2MB each) typical = 2GB total
- Mempool pre-allocated in hugepages
- Per-NUMA node allocation

**Cache Locality**:
- Packet descriptor (mbuf) kept in L1 cache
- Batch processing to maximize prefetch utility
- Worker-local stats to avoid false sharing
- Aligned data structures (64-byte cache lines)

**Lock Avoidance**:
- Atomic operations for counters (lock-free)
- Per-lcore statistics (no contention)
- Ring queues (lock-free by design)
- Read-heavy config via RCU or versioning

### Backpressure & Congestion

**Ring Queue Backpressure**:
- Monitor ring available space
- Pause RX if downstream queues full
- Drop lower-priority flows on overload

**Mempool Backpressure**:
- Monitor free buffer count
- Alert when low (< 10% free)
- Trigger flow eviction if necessary

### Performance Targets

- **Throughput**: 25+ Gbps per core (with e810 NIC, typical)
- **Packet Rate**: 100+ Mpps (million packets/sec)
- **Latency**: p50 < 10us, p99 < 100us
- **Memory**: ~1GB per core with pooling
- **CPU**: < 10% overhead for metering/analytics per core

## Data Flow

### Fast Path Processing

```
RX Port0 Q0 (64 pkt burst)
    ↓
Mempool Alloc (or reuse)
    ↓
Batch Prefetch & Parsing
    ↓
[Metering Stage] - Update counters (atomic)
    ↓
[DPI Stage] - Classify traffic (timeout-protected)
    ↓
[Analytics Stage] - Record telemetry (sampling)
    ↓
TX Port1 Q0 (64 pkt burst)
    ↓
Mempool Free
```

### Processing Latency Breakdown (typical)
- RX scheduling: 1-5 µs
- Mempool alloc: 0.1-1 µs
- Batch processing: 5-10 µs
- Metering (atomic): 0.5-1 µs
- DPI classification: 10-50 µs (configurable)
- Analytics (sampling): 0.1-1 µs
- TX scheduling: 1-5 µs
- **Total**: 20-75 µs p50, <100 µs p99

## Configuration

See `config/vsg.conf` for all runtime parameters:
- Worker thread count and affinity
- Port RX/TX queue counts
- Mempool sizes and cache settings
- Metering thresholds and rollup windows
- Analytics sampling rates and windows
- DPI timeout and degradation settings

## Monitoring & Observability

**Metrics** (per worker thread):
- RX/TX packet and byte counts
- Dropped packets (cause tracking)
- Processing errors
- Metering updates per second
- DPI classifications per second

**Health Checks**:
- Port up/down status
- Queue depth monitoring
- Mempool utilization
- Thread heartbeats

**Logging**:
- Structured logging with levels (DEBUG/INFO/WARN/ERROR)
- Per-component loggers
- Rotation and archival policies

## Deployment & Containerization

See Docker files in `docker/`:
- `Dockerfile`: Build with DPDK from source
- `docker-compose.yml`: Orchestration with resource limits
- `entrypoint.sh`: Runtime configuration injection

Environment variables for container:
- `VSG_CONFIG`: Config file path
- `VSG_WORKER_THREADS`: Number of worker cores
- `VSG_MAX_SUBSCRIBERS`: Max concurrent subscribers
- `VSG_HUGEPAGES`: Hugepage mount point

Resource requirements:
- **CPU**: 4-8 cores recommended (scales linearly)
- **Memory**: 2-8 GB (mempool + working set)
- **Network**: 2x 10/25G NICs (one ingress, one egress)
- **Hugepages**: 1GB minimum

## Future Enhancements

1. **Advanced QoS**: Weighted scheduling, rate limiting
2. **Stateful DPI**: Connection tracking for advanced classification
3. **In-band telemetry**: Carry metrics in packet headers
4. **Distributed metering**: Multi-node aggregation and reconciliation
5. **ML-based classification**: Anomaly detection, fraud prevention
6. **Hardware offload**: Classify on SmartNICs, offload metering
