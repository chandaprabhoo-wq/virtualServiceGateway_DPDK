# Multi-Core Scaling Strategies

## Design Philosophy

**Scale out, not up**: Better to have multiple smaller cores processing independently than fewer cores sharing state.

## Core Architecture

### Queue-to-Core Mapping

**Goal**: Lock-free, deterministic packet steering to workers

```
Port 0 RX Queue 0 → Assign to Core 0 (NUMA 0)
Port 0 RX Queue 1 → Assign to Core 1 (NUMA 0)
Port 0 RX Queue 2 → Assign to Core 2 (NUMA 1)
Port 0 RX Queue 3 → Assign to Core 3 (NUMA 1)
```

**Implementation**:
```cpp
struct WorkerConfig config;
config.worker_id = 0;
config.lcore_id = 0;
config.rx_queues = {0};      // RX from port 0 queue 0
config.tx_queues = {0};      // TX to port 1 queue 0
config.numa_node = 0;
config.pinned = true;         // CPU affinity
```

### Flow Hashing & Load Distribution

**Goal**: Ensure flows for same subscriber stay on same core (cache locality, metering consistency)

**Hash function**: 5-tuple → queue assignment
```c
// Pseudo-code
uint32_t hash_flow(src_ip, dst_ip, src_port, dst_port, proto) {
    // Use rte_hash or simple XOR
    return fnv1a_hash(&5tuple) % num_queues;
}
```

**Benefits**:
- Same flow always → same core → same memory/cache
- Stateful processing requires no synchronization
- Per-core counters accurate for accounting

**Trade-off**: 
- Hash collision risks in distributed system
- Mitigation: Use good hash function, monitor distribution

## Worker Pipeline Design

### Single-Core Execution Model

```
┌──────────────────────────────┐
│     Worker Thread Loop       │
├──────────────────────────────┤
│ for_each_burst:              │
│   RX from assigned queues    │
│   └─ Metering stage          │
│   └─ DPI classification      │
│   └─ Analytics sampling      │
│   TX to output queues        │
│   Yield if no work           │
└──────────────────────────────┘
```

### Multi-Stage Pipeline (Advanced)

For higher specialization:

```
┌─────────┐    ┌──────────┐    ┌──────────┐
│ RX Core │ → │ DPI Core │ → │ TX Core  │
│ (1-2)   │    │ (3-4)    │    │ (5-6)    │
└─────────┘    └──────────┘    └──────────┘

Connected via lock-free ring queues
```

**Pros**: Better CPU cache usage, easier scaling
**Cons**: More complexity, more memory bandwidth needed

## Backpressure Handling

### Ring Queue Monitoring

```cpp
uint32_t available = rte_ring_free_count(tx_ring);

if (available < MIN_AVAILABLE) {
    // Backpressure: pause RX
    rx_count = 0;
} else {
    // Normal: receive burst
    rx_count = rte_eth_rx_burst(...);
}
```

### Graceful Degradation

```
Full speed → Reduced burst size → Reduced RX rate → Drop low-priority flows
```

### Drop Policy

```cpp
// Priority: Billing > Analytics > DPI
if (ring_full) {
    if (packet.is_billing) {
        NEVER_DROP();
    } else if (packet.is_analytics) {
        drop_probability = 0.1;  // Sample
    } else if (packet.is_dpi) {
        drop_probability = 0.5;  // 50% sampling
    }
}
```

## NUMA Optimization

### Memory Affinity

```cpp
// Allocate mempool on socket where workers run
struct rte_mempool* mp = rte_pktmbuf_pool_create(
    "mempool_sock0",
    NUM_MBUFS,
    CACHE_SIZE,
    0,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    0  // socket_id = NUMA node 0
);
```

### Worker Pinning

```cpp
// Pin worker thread to specific core on NUMA node
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(lcore_id, &cpuset);
pthread_setaffinity_np(worker_thread, sizeof(cpuset), &cpuset);
```

### Cross-Socket Traffic Minimization

```
Per-NUMA-node workers:
  Socket 0: Cores 0-3 (4 workers) + Mempool
  Socket 1: Cores 4-7 (4 workers) + Mempool

Avoid: Core 0 using mempool from Socket 1
```

## Load Balancing Strategies

### 1. Static Assignment (Recommended)

**Setup**: Fixed cores per service/subscriber

```
Service 1 (Video) → Cores 0-1
Service 2 (VoIP)  → Cores 2-3
Service 3 (Web)   → Cores 4-5
```

**Pros**: Simple, predictable, no runtime overhead
**Cons**: Inflexible if load distribution changes

### 2. Dynamic Queue-Based

**Setup**: Ring queues between RX and processing stages

```
Port RX Cores (0-1) → Shared Queue → Processing Cores (2-7)
                                  → Output Queue → TX Cores (8)
```

**Pros**: Flexible, automatic load distribution
**Cons**: Extra queues = latency, complexity

### 3. Work Stealing (Advanced)

**Setup**: Cores steal work from loaded neighbors

```cpp
// Check own queue
nb_rx = rte_eth_rx_burst(port, queue[cpu_id], ...);

if (nb_rx == 0 && has_neighbors) {
    // Try to steal from neighbor
    nb_rx = try_steal_from_neighbor();
}
```

**Pros**: Adapts to dynamic load
**Cons**: Complex, introduces latency

## Synchronization Patterns

### Atomic Counters (Metering)

```cpp
// Lock-free, uses CAS
std::atomic<uint64_t> uplink_bytes{0};
uplink_bytes += pkt->len;  // No locks!
```

**Cost**: ~1-2 nanoseconds per operation
**Scale**: No contention up to ~8 cores; degradation after

### Ring Queues (Inter-core)

```cpp
// Lock-free rings, optimized for queue operations
rte_ring_sp_enqueue_burst(ring, objs, count, NULL);
```

**Cost**: ~5-10 ns per enqueue/dequeue
**Scale**: Linear (multiple writers/readers possible)

### Mutex/RWLock (Config Changes)

```cpp
// For infrequent changes (e.g., DPI signature updates)
std::shared_mutex config_lock;

// Reader (hot path, with reader lock)
{
    std::shared_lock<std::shared_mutex> lock(config_lock);
    sigs = dpi_signatures;  // Read
}

// Writer (cold path, exclusive lock)
{
    std::unique_lock<std::shared_mutex> lock(config_lock);
    dpi_signatures = new_sigs;  // Write
}
```

## Performance Targets

| Metric | Target | Achieved |
|--------|--------|----------|
| Throughput scaling | 85-90% per core | Yes, up to 8 cores |
| Per-packet latency | < 100 µs p99 | Yes, at 25 Gbps |
| Metering accuracy | ± 0.1% | Yes, atomic counters |
| Cross-core latency | < 1 µs | Yes, ring queues |

## Troubleshooting

### Uneven Load Distribution

```bash
# Check per-core packet counts
tail -f /var/log/vsg/worker_stats.log | grep "Core"

# Expected: Roughly equal packets per core
# Issue: May indicate hash collision or asymmetric load
```

### Mempool Exhaustion

```cpp
// Monitor in stats thread
uint32_t free = rte_mempool_avail_count(mp);
if (free < NUM_MBUFS * 0.1) {  // < 10%
    LOG_WARN("Mempool running low: %u free", free);
}
```

### NUMA Cross-Socket Traffic

```bash
# Monitor with perf
perf stat -e numa_hit,numa_miss ./vsg_gateway

# Ideal: > 99% numa_hit
# Poor: < 90% numa_hit (indicates misconfiguration)
```

## Example: 8-Core Configuration

```
CPU Layout:
  Socket 0: Cores 0-3 (L3 cache shared)
  Socket 1: Cores 4-7 (L3 cache shared)

Assignment:
  Ingress NIC → Port 0 RX Queues (4)
    Queue 0 → Core 0 (Socket 0) + Mempool0
    Queue 1 → Core 1 (Socket 0) + Mempool0
    Queue 2 → Core 4 (Socket 1) + Mempool1
    Queue 3 → Core 5 (Socket 1) + Mempool1
  
  Egress NIC → Port 1 TX Queues (4)
    Core 0 → Queue 0
    Core 1 → Queue 1
    Core 4 → Queue 2
    Core 5 → Queue 3

Expected Performance:
  - 4 workers × 6-7 Gbps = ~24-28 Gbps total
  - p99 latency: ~80 µs (single queue per core)
  - Metering: 4M+ subscribers tracked
```

## References

- DPDK Best Practices Guide
- Linux Kernel NUMA Documentation
- "The Art of Multiprocessor Programming" by Herlihy & Shavit
