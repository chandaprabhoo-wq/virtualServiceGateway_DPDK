# Minimal DPDK Application - Example Guide

## Overview

This example demonstrates a **minimal but complete DPDK application** that:
- Initializes DPDK EAL (Environment Abstraction Layer)
- Enables and configures Ethernet ports
- Sets up RX/TX queues with mempools
- Creates a ring buffer for inter-core communication
- Receives packets, processes them (MAC swap), and transmits back
- Uses multiple lcores (CPU cores) for different tasks

## Key DPDK Concepts

### 1. EAL (Environment Abstraction Layer)
```
rte_eal_init()  → Initializes DPDK, probes NICs, sets up mempools
```
- Parses command-line arguments (lcore list, memory, etc.)
- Prepares huge pages for DMA
- Maps PCI devices

### 2. Mempool (Memory Pool)
```
rte_pktmbuf_pool_create()  → Pre-allocates packet buffers
```
- Zero-copy packet handling (mbufs)
- Per-lcore cache for fast allocation
- Prevents malloc/free overhead
- NUMA-aware allocation

**Example:**
```cpp
pktmbuf_pool = rte_pktmbuf_pool_create(
    "pktmbuf_pool",           // Name
    MEMPOOL_MBUF_NUM,         // 8192 buffers
    MEMPOOL_CACHE_SIZE,       // 256 per-lcore cache
    0,                        // No private data
    RTE_MBUF_DEFAULT_BUF_SIZE,// 2KB per buffer
    rte_socket_id()          // NUMA socket
);
```

### 3. Port Configuration
```
rte_eth_dev_configure()  → Configure port parameters
rte_eth_rx_queue_setup() → Setup RX queue (for receiving)
rte_eth_tx_queue_setup() → Setup TX queue (for transmitting)
rte_eth_dev_start()      → Start port (begin receiving)
```

**RX Queue:**
- Receives packets from NIC
- Uses mempool for packet buffers
- Typical size: BURST_SIZE * 2 (128 entries)

**TX Queue:**
- Transmits packets to NIC
- Can hold pending packets before transmission
- Same size as RX queue

### 4. Ring Buffer (Inter-Core Communication)
```
rte_ring_create()  → Lock-free queue for passing pointers
```
- Single Producer/Consumer (SP/SC) or Multi (MP/MC)
- Zero-copy (only pointers passed, not data)
- Used to pass packets between RX and TX lcores

**Example:**
```cpp
rx_to_tx_ring = rte_ring_create(
    "rx_to_tx",              // Name
    RING_SIZE,               // 2048 entries
    rte_socket_id(),         // NUMA socket
    RING_F_SP_ENQ |          // Single producer enqueue
    RING_F_SC_DEQ            // Single consumer dequeue
);
```

### 5. RX/TX Bursts (Batch Processing)
```
rte_eth_rx_burst()  → Receive up to BURST_SIZE packets
rte_eth_tx_burst()  → Transmit up to BURST_SIZE packets
```
- More efficient than per-packet operations
- Amortizes overhead across 64 packets
- **BURST_SIZE = 64** is typical

## Application Architecture

```
                    Main Thread
                        |
        ________________|________________
        |               |               |
      [RX Core]    [Stats Core]    [TX Core]
        |                               |
     RX Burst                        TX Burst
        |                               |
     Enqueue ←→ Ring Buffer ←→ Dequeue
        |
    Process (MAC Swap)
        |
    Transmit
```

### Data Flow
```
1. RX Core:
   rte_eth_rx_burst(port, 0, bufs, 64)
   → rte_ring_sp_enqueue_burst(ring, bufs, count)

2. Ring Buffer:
   Lock-free circular queue
   Capacity: 2048 pointers

3. TX Core:
   rte_ring_sc_dequeue_burst(ring, bufs, 64)
   → [Process packets - swap MAC addresses]
   → rte_eth_tx_burst(port, 0, bufs, count)
```

## Building

### Prerequisites
```bash
# Linux - install DPDK
sudo apt-get install -y libdpdk-dev pkg-config

# Or build from source
git clone http://dpdk.org/git/dpdk
cd dpdk
meson build
ninja -C build
sudo ninja -C build install
```

### Compile
```bash
cd /path/to/vSG/build
cmake -DCMAKE_BUILD_TYPE=Release ..
make minimal_dpdk
```

### Output
```
./examples/minimal_dpdk
```

## Running

### Setup Huge Pages (Linux)
```bash
# Check current hugepages
cat /proc/meminfo | grep Huge

# Set up 1024 huge pages (2GB)
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# Verify
cat /proc/meminfo | grep Huge
```

### Run with Virtual NIC (Demo)
```bash
./examples/minimal_dpdk -l 0,1,2,3 -- -p 0x0
# Runs without physical NICs (mempool mode)
```

### Run with Real NICs (Linux only)
```bash
# For single port (port 0 only)
./examples/minimal_dpdk -l 0,1,2,3 -- -p 0x1

# For two ports
./examples/minimal_dpdk -l 0,1,2,3,4,5 -- -p 0x3
```

### Command-Line Arguments Explained
```
-l 0,1,2,3        Logical cores to use
                  - 0: Main/Master lcore
                  - 1: RX worker
                  - 2: TX worker  
                  - 3: Stats worker

-- -p 0x3          Port mask (hex)
                  - 0x1 = port 0 only
                  - 0x3 = ports 0,1
                  - 0x7 = ports 0,1,2
```

## Output Example

```
Initializing DPDK EAL...
DPDK EAL initialized
EAL timer resolution: 2400000000 cycles per second
Parsing application arguments...
Creating mempool with 8192 mbufs, cache size 256...
Mempool created successfully
Creating ring buffer with size 2048...
Ring created successfully
Found 2 Ethernet ports
Port 0: i40e
  Setting up RX queue 0...
  Setting up TX queue 0...
  Port 0 configured successfully
  Port 0 started
Port 1: i40e
  Setting up RX queue 0...
  Setting up TX queue 0...
  Port 1 configured successfully
  Port 1 started
Available lcores: 4
Launching RX worker on lcore 1...
[RX] Core 1 started, receiving from port 0
Launching TX worker on lcore 2...
[TX] Core 2 started, transmitting on port 0
Launching stats worker on lcore 3...
[STATS] Core 3 started

========================================
DPDK Application Running
Press Ctrl+C to stop
========================================

====== Port Statistics ======
RX Packets: 1000000
TX Packets: 1000000
Ring occupancy: 12
Mempool available: 8180
Mempool in use: 12
=============================
```

## Key Functions Reference

### Mempool
```cpp
rte_mempool *pool = rte_pktmbuf_pool_create(...);
struct rte_mbuf *m = rte_pktmbuf_alloc(pool);
rte_pktmbuf_free(m);
```

### Ring
```cpp
struct rte_ring *ring = rte_ring_create(...);
rte_ring_sp_enqueue_burst(ring, (void**)objs, count, NULL);
rte_ring_sc_dequeue_burst(ring, (void**)objs, count, NULL);
```

### Port RX/TX
```cpp
// Receive
uint16_t nb_rx = rte_eth_rx_burst(port_id, queue_id, pkts, burst_size);

// Transmit
uint16_t nb_tx = rte_eth_tx_burst(port_id, queue_id, pkts, burst_size);

// Packet data access
uint8_t *data = rte_pktmbuf_mtod(m, uint8_t*);
uint16_t len = rte_pktmbuf_pkt_len(m);
```

### Packet Metadata
```cpp
struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr*);
struct rte_ipv4_hdr *ip = (void*)eth + sizeof(*eth);
```

## Performance Tips

1. **Batch Processing**: Use BURST_SIZE=64 to amortize overhead
2. **Mempool Sizing**: Ensure pool has enough mbufs (2x per-core NIC queue depth)
3. **Ring Sizing**: Power of 2 (e.g., 2048) for modulo optimization
4. **Core Affinity**: Pin RX/TX to different CPU cores
5. **NUMA Awareness**: Use per-socket mempools for multi-socket systems

## Monitoring

The stats core prints every 2 seconds:
- **RX Packets**: Total received
- **TX Packets**: Total transmitted
- **Ring occupancy**: Packets waiting in ring
- **Mempool free**: Available buffers
- **Mempool in use**: Currently allocated buffers

## Next Steps

1. **Extend Packet Processing**: Add DPI classification in TX core
2. **Add Metering**: Update QoS queue based on subscriber data
3. **Multi-Core RX/TX**: Scale to 8+ lcores with multiple queues
4. **Performance Profiling**: Use `perf` to measure cycles/packet
5. **Integrate into vSG**: Combine with UBB metering and analytics

## Troubleshooting

### "No Ethernet ports"
- Ensure NIC is properly bound to DPDK (on Linux)
- Check: `dpdk-devbind.py --status`

### "Cannot allocate hugepages"
- Set: `echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages`

### Low throughput
- Check core affinity (cores on same socket)
- Verify mempool has enough buffers
- Profile with `perf record`

### Ring buffer full
- Increase RING_SIZE
- Add more TX lcores
- Check TX queue is not stuck

## References

- [DPDK Programmer's Guide](https://doc.dpdk.org/guides/)
- [DPDK API Reference](https://doc.dpdk.org/api/)
- [DPDK Examples](https://github.com/DPDK/dpdk/tree/main/examples)
