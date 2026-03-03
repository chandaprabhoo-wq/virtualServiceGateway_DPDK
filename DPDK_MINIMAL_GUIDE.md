# DPDK Minimal Example - Complete Setup Guide

## What is DPDK?

**DPDK (Data Plane Development Kit)** is a set of C libraries that enable high-performance packet processing on commodity hardware. It provides:

- **PMD (Poll Mode Drivers)**: Direct NIC access without kernel interrupts
- **Memory Management**: Pre-allocated buffers (mbufs) via mempools  
- **Lock-Free Data Structures**: Ring queues for inter-core communication
- **Multi-Core Scaling**: Per-core worker threads with NUMA awareness
- **High Performance**: 25+ Gbps per core, sub-100µs latency

## Quick Start (5 Minutes)

### Step 1: Build the Example

```bash
cd /Users/cprabhoo/Downloads/Interview-Preparation/vSG
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make minimal_dpdk
```

### Step 2: Review the Code

```bash
# View the minimal DPDK application
cat ../examples/minimal_dpdk.cpp

# View the detailed guide
cat ../examples/README_MINIMAL_DPDK.md
```

### Step 3: Understand the Architecture

See diagram below for component relationships.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      DPDK Application                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────┐         ┌──────────┐         ┌──────────┐         │
│  │  RX Core │         │  Ring    │         │  TX Core │         │
│  │ (lcore 1)│ ───────→│ Buffer   │────────→│(lcore 2) │         │
│  └──────────┘         │ (lock    │         └──────────┘         │
│       │               │  free)   │              ▲                │
│       │               │          │              │                │
│  rte_eth_rx_burst  rte_ring_sp_  rte_eth_tx_burst              │
│       │            enqueue_burst │              │                │
│       ▼                          ▼              │                │
│  ┌──────────────────────────────────────┐     │                │
│  │      Mempool (Pre-allocated)         │     │                │
│  │      Packet Buffers (mbufs)          │     │                │
│  │  ┌────┐┌────┐┌────┐┌────┐┌────┐  │     │                │
│  │  │pkt1││pkt2││pkt3││pkt4││... │  │─────┘                │
│  │  └────┘└────┘└────┘└────┘└────┘  │                      │
│  │  Cache per lcore for fast alloc   │                      │
│  └──────────────────────────────────────┘                      │
│       ▲                                                          │
│       │                                                          │
│   rte_eal_init()                                               │
│       │                                                          │
│  ┌────────────────────────────────────────────────────────┐    │
│  │          EAL (Environment Abstraction Layer)            │    │
│  │  • Huge page allocation                                │    │
│  │  • NIC probing and PCIe enumeration                   │    │
│  │  • Per-lcore thread management                         │    │
│  │  • NUMA socket awareness                               │    │
│  └────────────────────────────────────────────────────────┘    │
│       ▲                                                          │
│       │                                                          │
└───────┼──────────────────────────────────────────────────────────┘
        │
        ▼
┌──────────────────────────────────────────────────────────────────┐
│              Linux Kernel & Hardware                             │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Ethernet NIC (Intel 40GB, etc.)                         │   │
│  │  ┌──────────────────────────────────────┐               │   │
│  │  │  RX Ring (Queue 0): incoming packets │◄──────────┐   │   │
│  │  └──────────────────────────────────────┘           │   │   │
│  │  ┌──────────────────────────────────────┐           │   │   │
│  │  │  TX Ring (Queue 0): outgoing packets │───────────┼──→├─┐ │
│  │  └──────────────────────────────────────┘           │   │ │ │
│  │                                                       │   │ │ │
│  │  Physical Port (Intel i40e, Mellanox, etc.)         │   │ │ │
│  │  [=================Internet==================]       │   │ │ │
│  └──────────────────────────────────────────────────────┘   │ │ │
│                    ▲                         ▲               │ │ │
│                    │                         │               │ │ │
│              Packet RX                    Packet TX          │ │ │
│              (Poll Mode)                  (Poll Mode)        │ │ │
│              No interrupts!                No context switch │ │ │
│                                                               │ │ │
└───────────────────────────────────────────────────────────────┼─┘
                                                                │
                                                    Promiscuous mode
                                                    (receive all packets)
```

## Key Components Explained

### 1. DPDK EAL (Environment Abstraction Layer)

**Function:** Initializes DPDK and prepares the system

```cpp
// Initialize DPDK
int ret = rte_eal_init(argc, argv);
if (ret < 0)
    rte_exit(EXIT_FAILURE, "Cannot init EAL\n");
```

**What happens:**
- Parses huge page configuration
- Probes PCI bus for NICs
- Sets up per-lcore memory caches
- Prepares NUMA-aware memory allocation
- Initializes timer subsystem

**Command line:**
```
-l 0,1,2,3    : Use cores 0, 1, 2, 3
--socket-mem=1024,1024  : 1GB per NUMA socket
--no-huge     : Use regular pages (slow, testing only)
```

### 2. Mempool (Packet Buffer Pool)

**Function:** Pre-allocates packet buffers (mbufs) to avoid malloc/free overhead

```cpp
pktmbuf_pool = rte_pktmbuf_pool_create(
    "pktmbuf_pool",           // Name for debugging
    MEMPOOL_MBUF_NUM,         // 8192 buffers
    MEMPOOL_CACHE_SIZE,       // 256 per-lcore cache
    0,                        // Private data size
    RTE_MBUF_DEFAULT_BUF_SIZE,// 2048 bytes per buffer
    rte_socket_id()           // NUMA socket
);
```

**Memory Layout:**
```
Mempool on Socket 0:
┌─────────────────────────────────────────────┐
│  Global Object List (8192 mbufs)           │
│  ┌─────────┬─────────┬─────────┬─────────┐ │
│  │ mbuf[0] │ mbuf[1] │ mbuf[2] │ mbuf[3] │ │
│  └─────────┴─────────┴─────────┴─────────┘ │
├─────────────────────────────────────────────┤
│  Per-lcore Cache (Core 0) - 256 items      │
│  ┌─────┬─────┬─────┬─────┬─────┬─────────┐ │
│  │ m1  │ m2  │ m3  │ ... │ m256│ [empty]│ │
│  └─────┴─────┴─────┴─────┴─────┴─────────┘ │
├─────────────────────────────────────────────┤
│  Per-lcore Cache (Core 1) - 256 items      │
│  [Similar structure]                       │
└─────────────────────────────────────────────┘
```

**Benefits:**
- **Zero-copy**: Pointers passed, not data copied
- **NUMA-aware**: Each socket has its own pool
- **Per-lcore cache**: Fast alloc without locking
- **Pre-allocated**: No garbage collection pauses

**Operations:**
```cpp
// Allocate
struct rte_mbuf *m = rte_pktmbuf_alloc(pktmbuf_pool);

// Access data
uint8_t *data = rte_pktmbuf_mtod(m, uint8_t*);
uint16_t len = rte_pktmbuf_pkt_len(m);

// Free (returns to cache/mempool)
rte_pktmbuf_free(m);
```

### 3. Port Configuration

**Function:** Set up Ethernet port with RX/TX queues

```cpp
// Configure port
rte_eth_dev_configure(port_id, 1, 1, &port_conf);

// Setup RX queue (for receiving)
rte_eth_rx_queue_setup(port_id, 0, ring_size, socket_id, NULL, mempool);

// Setup TX queue (for transmitting)
rte_eth_tx_queue_setup(port_id, 0, ring_size, socket_id, &txconf);

// Start port (begin accepting packets)
rte_eth_dev_start(port_id);
```

**Port Structure:**
```
Port 0 (Intel i40e - 40 Gbps):
├── RX Queues (up to 128)
│   ├── Queue 0 → Ring of 256 entries (can hold 256 mbufs)
│   ├── Queue 1 → Ring of 256 entries
│   └── Queue N → ...
├── TX Queues (up to 128)
│   ├── Queue 0 → Ring of 256 entries
│   ├── Queue 1 → Ring of 256 entries
│   └── Queue N → ...
└── Port Statistics (packets, bytes, errors)

Queue Entry Structure:
┌──────────┬──────────┬──────────┬──────────┐
│ mbuf ptr │ mbuf ptr │ mbuf ptr │ mbuf ptr │ ← Points to packet buffers
└──────────┴──────────┴──────────┴──────────┘
```

**Ring Size Recommendations:**
- Minimum: 256 (BURST_SIZE * 4)
- Typical: 512-2048
- Maximum: Limited by NIC (often 65536)

### 4. Ring Buffer (Inter-Core Communication)

**Function:** Lock-free queue for passing packets between lcores

```cpp
rx_to_tx_ring = rte_ring_create(
    "rx_to_tx",                    // Name
    RING_SIZE,                     // 2048 (power of 2)
    rte_socket_id(),               // NUMA socket
    RING_F_SP_ENQ | RING_F_SC_DEQ  // SP=single producer, SC=single consumer
);
```

**Ring Structure:**
```
Ring Buffer (2048 entries, power of 2):

Head pointer (RX producer)
    ↓
┌────┬────┬────┬────┬────┬────┬────┐
│ *m1│ *m2│ *m3│ *m4│NULL│NULL│NULL│ ← mbuf pointers (not data!)
└────┴────┴────┴────┴────┴────┴────┘
                    ▲
                    Tail pointer (TX consumer)

When RX enqueues:
  - Adds pointer to next empty slot
  - Updates head pointer atomically (no lock!)

When TX dequeues:
  - Reads pointer from current position
  - Updates tail pointer atomically
  - Buffer is now empty and available for RX
```

**Variants:**
```
SP_ENQ | SC_DEQ  : Single producer/consumer (fast, no atomic ops)
MP_ENQ | MC_DEQ  : Multi producer/consumer (slower, uses atomics)
SP_ENQ | MC_DEQ  : Single prod / multi cons
MP_ENQ | SC_DEQ  : Multi prod / single cons
```

### 5. RX/TX Bursts (Batch Processing)

**Function:** Move multiple packets at once (cache efficient)

```cpp
// Receive burst
uint16_t nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

// Transmit burst
uint16_t nb_tx = rte_eth_tx_burst(port_id, queue_id, bufs, nb_rx);
```

**Burst Processing:**
```
Single packet RX (SLOW):
  rte_eth_rx_burst() → 1 mbuf
  Process 1 packet
  Cache miss on next packet
  → ~100 cycles per packet

Burst RX (FAST):
  rte_eth_rx_burst() → 64 mbufs
  Prefetch next batch
  Process 64 packets in pipelined fashion
  → ~4 cycles per packet (25x faster!)

BURST_SIZE=64 is typical:
┌─────────────────────────────────────────────┐
│  RX Burst Buffer                            │
│ [m1][m2][m3][m4]...[m62][m63][m64]         │
│  ▲   ▲   ▲   ▲              ▲    ▲    ▲    │
│  └───┴───┴───┴──────────────┴────┴────┘    │
│       All processed in single loop         │
│       with cache locality                  │
└─────────────────────────────────────────────┘
```

## Data Flow Example

**Simplified packet flow through application:**

```
1. RX CORE (lcore 1):
   
   Loop:
     nb_rx = rte_eth_rx_burst(port0, queue0, bufs, 64)
     → Gets up to 64 mbufs with packet data from NIC
     
     if (nb_rx > 0) {
       rte_ring_sp_enqueue_burst(ring, (void**)bufs, nb_rx, NULL)
       → Sends pointers to ring buffer
     }

2. RING BUFFER:
   
   ┌─────────────────────────────┐
   │ RX → [*m1, *m2, *m3, ...] ← TX │
   └─────────────────────────────┘
   
   Stores pointers to mbufs
   Lock-free (no mutexes)

3. TX CORE (lcore 2):
   
   Loop:
     nb_deq = rte_ring_sc_dequeue_burst(ring, bufs, 64, NULL)
     → Gets up to 64 mbufs from ring
     
     if (nb_deq > 0) {
       // Process each packet
       for (i = 0; i < nb_deq; i++) {
         eth_hdr = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr*);
         // Swap MAC: src ↔ dst
         tmp = eth_hdr->dst_addr;
         eth_hdr->dst_addr = eth_hdr->src_addr;
         eth_hdr->src_addr = tmp;
       }
       
       // Transmit all at once
       nb_tx = rte_eth_tx_burst(port0, queue0, bufs, nb_deq)
       → Sends packets back to NIC
       
       // Free any unsent
       for (i = nb_tx; i < nb_deq; i++)
         rte_pktmbuf_free(bufs[i]);
     }

4. MEMPOOL (Automatic):
   
   When rte_pktmbuf_free() called:
   → Packet buffer returned to per-lcore cache
   → Next rte_pktmbuf_alloc() reuses immediately
   → No syscalls, no malloc/free
```

## Performance Characteristics

### Throughput (Gbps)
```
Single core:
  - With batching (burst_size=64): 25-40 Gbps
  - Per packet: ~4 cycles
  
Multi-core (8 cores):
  - 25 Gbps per core = 200 Gbps aggregate
  - Perfect scaling with lock-free design
  
Factors affecting throughput:
  - Packet size (optimal: 64 byte headers + data)
  - CPU frequency (GHz)
  - Cache misses (minimized with prefetch)
  - Ring utilization (lock-free = no contention)
```

### Latency
```
Packet latency breakdown:
  RX DMA transfer:        1-10 µs (depends on size)
  RX to ring enqueue:     0.1-0.5 µs (lock-free)
  Ring dequeue delay:     0-500 ns (ring burstiness)
  TX processing:          0.1-0.5 µs (MAC swap, etc)
  TX burst transmission:  1-10 µs
  
Total (head-to-tail):     3-20 µs typical
p99 latency:              < 100 µs (with proper tuning)
```

### Memory Usage
```
Per core:
  - Mempool cache: MBUF_CACHE_SIZE * sizeof(mbuf*) ≈ 2 KB
  - Per-lcore data: ≈ 100 KB
  - Thread stack: 2 MB default
  
Total per core:
  - With 65536 mbufs per core: ≈ 128 MB (2KB per mbuf)
  - 8 cores: 1 GB mempool + per-core overhead
```

## Building and Running

### Build
```bash
cd /Users/cprabhoo/Downloads/Interview-Preparation/vSG/build
cmake -DCMAKE_BUILD_TYPE=Release ..
make minimal_dpdk -j$(nproc)
```

### Run (Virtual NICs - Testing)
```bash
# Without physical NICs (uses virtual port 0)
./examples/minimal_dpdk -l 0,1,2,3 -- -p 0x0

# With simulated traffic (no hardware needed)
```

### Run (Real NICs - Production)
```bash
# Prerequisites (Linux only)
sudo sysctl -w vm.nr_hugepages=1024

# Bind NIC to DPDK (see DPDK docs)
# sudo dpdk-devbind.py --bind vfio-pci 0000:01:00.0

# Run application
./examples/minimal_dpdk -l 0,1,2,3 -- -p 0x1
```

## Key Takeaways

✅ **DPDK enables:**
- PMD (Poll Mode Driver): Direct NIC access, no kernel interrupts
- Batch processing: 64 packets at a time for cache efficiency
- Lock-free design: Atomic ops instead of mutexes
- NUMA awareness: Per-socket mempools for latency

✅ **Minimal example includes:**
- EAL initialization
- Mempool setup (pre-allocated buffers)
- Port configuration (RX/TX queues)
- Ring buffer (inter-core communication)
- RX core (receive packets)
- TX core (transmit packets)
- Stats core (monitor performance)

✅ **Performance targets:**
- 25+ Gbps per core
- < 100 µs p99 latency
- < 1 GB memory per core
- Linear scaling to 8+ cores

## Next Steps

1. **Understand the code:**
   - Read `examples/minimal_dpdk.cpp` line by line
   - Understand each DPDK API call

2. **Experiment:**
   - Modify packet processing (add DPI classification)
   - Add metering (byte counting)
   - Test with real traffic

3. **Optimize:**
   - Profile with `perf record`
   - Tune ring sizes, mempool sizes
   - Add more RX/TX queues

4. **Integrate:**
   - Combine with vSG metering/analytics
   - Add DPI classification
   - Deploy to production

## References

- [DPDK Official Documentation](https://doc.dpdk.org/)
- [DPDK Programmer's Guide](https://doc.dpdk.org/guides/prog_guide/)
- [DPDK API Reference](https://doc.dpdk.org/api/)
- [DPDK Examples](https://github.com/DPDK/dpdk/tree/main/examples)
- This project: `examples/minimal_dpdk.cpp`
