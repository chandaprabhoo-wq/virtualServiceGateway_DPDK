# DPDK Quick Reference Card

## Initialization

```cpp
// Initialize EAL
int ret = rte_eal_init(argc, argv);
if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot init EAL\n");
```

## Mempool (Packet Buffers)

```cpp
// Create mempool
struct rte_mempool *pool = rte_pktmbuf_pool_create(
    "name", 8192, 256, 0, 
    RTE_MBUF_DEFAULT_BUF_SIZE, 
    rte_socket_id()
);

// Allocate packet
struct rte_mbuf *m = rte_pktmbuf_alloc(pool);

// Free packet
rte_pktmbuf_free(m);

// Get packet data pointer
uint8_t *data = rte_pktmbuf_mtod(m, uint8_t*);

// Get packet length
uint16_t len = rte_pktmbuf_pkt_len(m);
```

## Port Configuration

```cpp
// Configure port
rte_eth_dev_configure(port_id, 1, 1, &port_conf);

// Setup RX queue
rte_eth_rx_queue_setup(port_id, 0, ring_size, socket_id, NULL, pool);

// Setup TX queue
rte_eth_tx_queue_setup(port_id, 0, ring_size, socket_id, &txconf);

// Start port
rte_eth_dev_start(port_id);

// Stop port
rte_eth_dev_stop(port_id);

// Enable promiscuous mode
rte_eth_promiscuous_enable(port_id);
```

## RX/TX Burst (Most Important!)

```cpp
// Receive burst (typical: 64 packets)
uint16_t nb_rx = rte_eth_rx_burst(port_id, queue_id, pkts, 64);

// Transmit burst
uint16_t nb_tx = rte_eth_tx_burst(port_id, queue_id, pkts, nb_rx);
```

## Ring Buffers (Inter-Core Communication)

```cpp
// Create ring
struct rte_ring *ring = rte_ring_create(
    "name", 2048, rte_socket_id(),
    RING_F_SP_ENQ | RING_F_SC_DEQ
);

// Single producer enqueue
uint16_t enq = rte_ring_sp_enqueue_burst(ring, (void**)objs, count, NULL);

// Single consumer dequeue
uint16_t deq = rte_ring_sc_dequeue_burst(ring, (void**)objs, count, NULL);

// Check ring count
uint32_t cnt = rte_ring_count(ring);
```

## Packet Headers

```cpp
// Ethernet header
struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr*);

// IPv4 header (after ethernet)
struct rte_ipv4_hdr *ipv4 = (void*)eth + sizeof(*eth);

// TCP header (after IPv4)
struct rte_tcp_hdr *tcp = (void*)ipv4 + sizeof(*ipv4);

// UDP header (after IPv4)
struct rte_udp_hdr *udp = (void*)ipv4 + sizeof(*ipv4);
```

## Core Utilities

```cpp
// Get current lcore ID
unsigned int core = rte_lcore_id();

// Get total lcore count
unsigned int count = rte_lcore_count();

// Launch function on remote lcore
rte_eal_remote_launch(func, arg, lcore_id);

// Wait for all lcores
rte_eal_mp_wait_lcore();

// CPU cycles (for timing)
uint64_t tsc = rte_rdtsc();

// Prefetch for cache optimization
rte_prefetch0(ptr);  // Prefetch to L1
rte_prefetch1(ptr);  // Prefetch to L2
```

## Statistics

```cpp
// Get port stats
struct rte_eth_stats stats;
rte_eth_stats_get(port_id, &stats);
printf("RX: %lu, TX: %lu\n", stats.ipackets, stats.opackets);

// Get mempool stats
uint32_t avail = rte_mempool_avail_count(pool);
uint32_t used = rte_mempool_in_use_count(pool);
```

## Logging

```cpp
// Log with level
RTE_LOG(ERR, USER1, "Error message\n");
RTE_LOG(NOTICE, USER1, "Notice message\n");

// Levels: EMERG, ALERT, CRIT, ERR, WARNING, NOTICE, INFO, DEBUG
```

## Common Errors & Solutions

| Error | Solution |
|-------|----------|
| "Cannot find dpdk" | `pkg-config --cflags --libs libdpdk` |
| "No hugepages" | `echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages` |
| "No Ethernet ports" | Bind NIC to DPDK with dpdk-devbind.py |
| "Ring full" | Increase RING_SIZE or add more workers |
| "Mempool exhausted" | Increase NUM_MBUFS or free packets faster |

## Performance Tips

| Tip | Impact |
|-----|--------|
| BURST_SIZE = 64 | 25x faster than per-packet |
| Prefetch next packet | Reduces latency 50x |
| Lock-free ring | Zero contention between cores |
| Per-lcore mempool cache | 100x faster alloc vs global |
| NUMA-aware allocation | 2-3x faster on multi-socket |

## Build Command

```bash
gcc -c program.c `pkg-config --cflags libdpdk`
gcc -o program program.o `pkg-config --libs libdpdk`
```

## Run Command

```bash
# Basic: 4 cores, 1 port
./program -l 0,1,2,3 -- -p 0x1

# With memory
./program -l 0,1,2,3 --socket-mem=1024,1024 -- -p 0x1

# No hugepages (slow, testing only)
./program -l 0,1,2,3 --no-huge -- -p 0x1
```

## Example: Simple TX Loop

```cpp
void tx_loop(uint16_t port_id) {
    struct rte_mbuf *bufs[64];
    uint16_t nb, i;

    while (!quit) {
        // Receive
        nb = rte_eth_rx_burst(port_id, 0, bufs, 64);
        if (nb == 0) continue;

        // Process
        for (i = 0; i < nb; i++) {
            struct rte_ether_hdr *eth = 
                rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr*);
            // Swap MAC
            struct rte_ether_addr tmp = eth->dst_addr;
            eth->dst_addr = eth->src_addr;
            eth->src_addr = tmp;
        }

        // Transmit
        rte_eth_tx_burst(port_id, 0, bufs, nb);
    }
}
```

## Example: Multi-Core with Ring

```cpp
// RX Core
void rx_core(struct rte_ring *ring) {
    struct rte_mbuf *bufs[64];
    uint16_t nb;
    while (!quit) {
        nb = rte_eth_rx_burst(0, 0, bufs, 64);
        if (nb > 0)
            rte_ring_sp_enqueue_burst(ring, (void**)bufs, nb, NULL);
    }
}

// TX Core
void tx_core(struct rte_ring *ring) {
    struct rte_mbuf *bufs[64];
    uint16_t nb;
    while (!quit) {
        nb = rte_ring_sc_dequeue_burst(ring, (void**)bufs, 64, NULL);
        if (nb > 0)
            rte_eth_tx_burst(0, 0, bufs, nb);
    }
}

// Main
int main() {
    rte_eal_init(argc, argv);
    struct rte_ring *ring = rte_ring_create("rx_tx", 2048, 
                                           NUMA_NODE, 
                                           RING_F_SP_ENQ | RING_F_SC_DEQ);
    rte_eal_remote_launch(rx_core, ring, 1);
    rte_eal_remote_launch(tx_core, ring, 2);
    rte_eal_mp_wait_lcore();
}
```

## Common Macros

```cpp
// Likely/Unlikely for branch prediction
if (likely(condition)) { }  // Branch usually taken
if (unlikely(condition)) { }  // Branch rarely taken

// Iterate all available ports
RTE_ETH_FOREACH_DEV(port_id) { }

// Iterate all available lcores
RTE_LCORE_FOREACH(lcore_id) { }
```

## Memory Layout

```
Mempool Structure:
┌────────────────────────────────┐
│ Global Free List (8192 items)  │
├────────────────────────────────┤
│ lcore 0 cache (256 items)      │
├────────────────────────────────┤
│ lcore 1 cache (256 items)      │
├────────────────────────────────┤
│ Object memory (8192 x 2KB)     │
└────────────────────────────────┘

RX Queue Ring:
┌────┬────┬────┬────┬────┐
│m1  │m2  │m3  │m4  │... │ ← mbuf pointers
└────┴────┴────┴────┴────┘
 ▲                       ▲
 RX Producer            RX Consumer
 (hardware)             (RX core)
```

## Data Path Latency Breakdown

```
Typical packet: 1500 bytes

RX Path:
  Receive DMA:        2-5 µs
  to Software RX:     0.1-0.5 µs
  Ring enqueue:       0.1 µs
  ──────────────────────────
  Total:              ~3 µs

TX Path:
  Ring dequeue:       0.1 µs
  Process packet:     0.2-0.5 µs
  TX burst:           0.1 µs
  Transmit DMA:       2-5 µs
  ──────────────────────────
  Total:              ~5 µs

Head-to-head latency: 6-10 µs
(Without processing: 3-5 µs)
```

## Where to Find More Info

- **Complete Code**: `examples/minimal_dpdk.cpp`
- **Detailed Guide**: `DPDK_MINIMAL_GUIDE.md`
- **Common Tasks**: `examples/dpdk_common_modifications.h`
- **Official Docs**: https://doc.dpdk.org/
- **API Reference**: https://doc.dpdk.org/api/
