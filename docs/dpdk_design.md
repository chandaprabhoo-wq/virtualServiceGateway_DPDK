# vSG DPDK Core Design & Optimization

## Overview

The DPDK core layer provides high-performance packet processing with:
- **Multi-queue RX/TX** for parallel processing
- **Memory pooling** with NUMA awareness
- **Burst processing** for cache efficiency
- **Lock-free rings** for inter-core communication
- **PMD abstraction** for hardware independence

## Architecture

### Component Stack

```
┌─────────────────────────────────────────┐
│      Application (Worker Pipeline)      │
├─────────────────────────────────────────┤
│   Batch Processor │ Ring Queues          │
├─────────────────────────────────────────┤
│   Mempool Manager │ PMD Driver           │
├─────────────────────────────────────────┤
│      DPDK Libraries & EAL               │
├─────────────────────────────────────────┤
│      Hardware (NICs, CPUs, Memory)      │
└─────────────────────────────────────────┘
```

## Packet Processing Flow

### RX Path
```
1. Port RX Queue (NIC driver)
2. rte_eth_rx_burst() - Poll mode RX (32-64 packets)
3. Mempool allocation (per-lcore cache)
4. Batch prefetch & parsing
5. Pass to processing pipeline
```

### TX Path
```
1. Processing pipeline output
2. Batch accumulation (coalesce for efficiency)
3. rte_eth_tx_burst() - Poll mode TX
4. Mempool free (return to pool)
5. Port TX Queue → NIC hardware
```

## Key Optimizations

### 1. Burst Processing

**Rationale**: Amortize fixed costs across multiple packets

```c
// Instead of:
for (int i = 0; i < pkts; i++) {
    process(pkts[i]);  // High per-packet overhead
}

// Do:
process_burst(pkts, count);  // Amortized overhead
```

**Benefits**:
- 2-3x throughput improvement
- Better cache prefetching
- Reduced function call overhead

### 2. Mempool Pre-allocation

**Rationale**: Avoid malloc/free in fast path

```c
// Pool size = 64K mbufs per core
// Each mbuf = 2KB + metadata
// Total = ~130MB per core

// Per-lcore cache = 256 entries
// Reduces contention on pool spinlock
```

**Benefits**:
- O(1) allocation from lcore cache
- Predictable memory footprint
- No fragmentation

### 3. Lock-Free Ring Queues

**Rationale**: Avoid locks in critical path

```c
// DPDK rings use CAS (Compare-And-Swap)
// Instead of mutex locks

// Single-producer/consumer variants
// Even faster (no CAS needed)
```

**Performance**: 
- 10-50 ns per enqueue/dequeue
- Scales linearly with core count
- No priority inversion

### 4. Per-Core Statistics

**Rationale**: Avoid false sharing on cache lines

```c
// ✗ Bad: Shared counter with atomic
volatile uint64_t global_counter;  // Contention

// ✓ Good: Per-lcore then aggregate
uint64_t counters[MAX_LCORES];  // No contention
```

**Impact**:
- 0.1% stat collection overhead vs 5%+ with globals

### 5. NUMA Awareness

**Rationale**: Minimize remote memory access

```c
// Allocate mempools on same NUMA node as cores
// Pin workers to specific cores
// Use rte_socket_id() for memory allocation
```

**Impact**:
- 30% latency improvement
- 50% throughput improvement on NUMA systems

## Configuration Tuning

### RX/TX Queue Counts

```
Formula: Queues = min(cores, NIC_max_queues) / 2
Example: 16 cores, Intel i40e (128 queues) → 8 RX + 8 TX
```

**Rationale**: 
- One queue per core for lock-free scheduling
- Avoid queue contention
- Balance RX and TX processing

### Mempool Size

```
Formula: num_mbufs = burst_size * 2 * queues + cache_size
Example: 64 * 2 * 8 + 256 = 1,280 minimum

Recommended: 64K per core = 65,536 total
```

**Tuning**:
- Increase if mempool exhaustion errors
- Decrease to save memory (careful!)
- Monitor with `rte_mempool_avail_count()`

### Batch/Burst Sizes

| Setting | Value | Rationale |
|---------|-------|-----------|
| RX burst | 64 | Typical NIC descriptor ring |
| TX burst | 32-64 | Balance latency vs throughput |
| Processing batch | 16-32 | L1 cache working set |

## Performance Profiling

### Collect Metrics

```bash
# Packet throughput
pktgen> stats  # Shows Mpps, Gbps per port

# CPU cycles per packet
perf record -c 10000 ./vsg_gateway
perf report -i perf.data

# Cache behavior
perf stat -e cache-references,cache-misses ./vsg_gateway
```

### Expected Results

| Metric | Target | Notes |
|--------|--------|-------|
| Throughput | 25+ Gbps/core | Depends on packet size |
| Packet rate | 100+ Mpps | At 256-byte packets |
| L1 hit % | > 95% | Indicates good locality |
| Branch miss % | < 5% | Low mispredictions |
| IPC | > 2.0 | Instructions per cycle |

## Debugging

### Common Issues

**1. Low throughput**
- Check RX/TX queue drops: `ethtool -S ethX`
- Monitor mempool: `rte_mempool_avail_count()`
- Profile with perf to find hotspots

**2. High latency**
- Check for mempool exhaustion (blocking allocs)
- Monitor NUMA cross-socket traffic
- Use latency-sensitive scheduling if available

**3. Uneven load across cores**
- Verify flow hashing is working
- Check queue-to-core mapping
- Monitor per-core RX packet counts

### Debug Logging

```c
RTE_LOG(INFO, USER1, "RX %u packets from port %u queue %u\n",
        nb_rx, port_id, queue_id);
```

Levels: DEBUG, INFO, NOTICE, WARNING, ERR, CRIT, ALERT, EMERG

## Scaling to Multi-Core

### Linear Scaling Checklist

- [ ] Queues pinned to cores (1:1 mapping)
- [ ] Mempools allocated on same NUMA node
- [ ] Per-core statistics (no shared atomics)
- [ ] Lock-free ring queues for inter-core
- [ ] No global locks in packet path
- [ ] Cache-aligned data structures
- [ ] Prefetching in place

### Expected Scaling

| Cores | Scaling | Gbps (example) |
|-------|---------|----------------|
| 1     | 100%    | 10 Gbps        |
| 2     | 195%    | 19.5 Gbps      |
| 4     | 380%    | 38 Gbps        |
| 8     | 720%    | 72 Gbps        |

**Note**: Perfect 100% scaling unlikely due to:
- NIC/bus limitations
- Memory bandwidth saturation
- NUMA cross-socket traffic
- Fixed overhead per packet

## Advanced Features

### Hardware Offloads

```c
struct rte_eth_conf conf = {
    .rxmode.offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM |
                       RTE_ETH_RX_OFFLOAD_JUMBO_FRAME,
};
```

Supported:
- RX checksum validation
- TX checksum offload
- TSO (TCP Segmentation)
- Jumbo frames
- VLAN stripping

### Hardware Timestamping

```c
pkt->timestamp = rte_eth_timesync_read_rx_timestamp(port_id);
```

Accuracy: Sub-microsecond on supported NICs

### Rate Limiting (Software)

```c
// Weighted fair queuing or token bucket
// Implement in processing pipeline
```

## References

- DPDK Programmer's Guide: https://doc.dpdk.org/
- Intel PMD Documentation
- High-Frequency Trading Architectures (similar patterns)
