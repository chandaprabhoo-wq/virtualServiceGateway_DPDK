/*
 * RING BUFFER PATTERNS FOR MULTI-CORE PACKET PROCESSING
 * 
 * This document explains which ring pattern to use for your
 * RX → DPI → QoE → UBB pipeline architecture.
 */

// ============================================================================
// YOUR ARCHITECTURE
// ============================================================================

/*
 * Multi-Stage Pipeline:
 * 
 *     ┌──────────────┐
 *     │  RX Core     │  (receives packets from NIC)
 *     │  (lcore 0)   │
 *     └──────┬───────┘
 *            │
 *       Ring Buffer 1
 *     (RX → DPI distribution)
 *            │
 *     ┌──────▼───────┐
 *     │  DPI Cores   │  (classify packets)
 *     │  (lcore 1,2) │  <- Multiple cores competing
 *     └──────┬───────┘
 *            │
 *       Ring Buffer 2
 *     (DPI → QoE distribution)
 *            │
 *     ┌──────▼───────┐
 *     │  QoE Cores   │  (collect metrics)
 *     │  (lcore 3,4) │  <- Multiple cores competing
 *     └──────┬───────┘
 *            │
 *       Ring Buffer 3
 *     (QoE → UBB distribution)
 *            │
 *     ┌──────▼───────┐
 *     │  UBB Cores   │  (meter traffic)
 *     │  (lcore 5,6) │  <- Multiple cores competing
 *     └──────┬───────┘
 *            │
 *       Ring Buffer 4
 *     (UBB → TX distribution)
 *            │
 *     ┌──────▼───────┐
 *     │  TX Core     │  (transmit packets)
 *     │  (lcore 7)   │
 *     └──────────────┘
 */

// ============================================================================
// ANALYSIS: DO YOU NEED RING BUFFERS?
// ============================================================================

/*
 * SHORT ANSWER: YES, you need ring buffers between stages.
 * 
 * BUT the ring type depends on your stage design:
 * 
 * Stage 1: RX → DPI
 *   - RX: 1 core (producer)
 *   - DPI: 2+ cores (consumers)
 *   → SPMC (Single Producer, Multi-Consumer) ring ✓
 * 
 * Stage 2: DPI → QoE
 *   - DPI: 2+ cores (producers)
 *   - QoE: 2+ cores (consumers)
 *   → MPMC (Multi-Producer, Multi-Consumer) ring ✓
 * 
 * Stage 3: QoE → UBB
 *   - QoE: 2+ cores (producers)
 *   - UBB: 2+ cores (consumers)
 *   → MPMC ring ✓
 * 
 * Stage 4: UBB → TX
 *   - UBB: 2+ cores (producers)
 *   - TX: 1 core (consumer)
 *   → MPSC (Multi-Producer, Single Consumer) ring ✓
 */

// ============================================================================
// UNDERSTANDING SPMC: THE KEY INSIGHT
// ============================================================================

/*
 * SPMC = Single Producer, Multi-Consumer
 * 
 * The statement says:
 * "One thread produces data, and multiple threads are competing to grab it."
 * 
 * What does this mean exactly?
 * 
 * Let's say you have:
 * - RX Core: puts 64 packets into ring
 * - DPI Core 1: wants to process some packets
 * - DPI Core 2: wants to process some packets
 * 
 * Ring contents before dequeue:
 * ┌────┬────┬────┬────┬────┬────┬────┬────┐
 * │pkt1│pkt2│pkt3│pkt4│pkt5│pkt6│pkt7│pkt8│
 * └────┴────┴────┴────┴────┴────┴────┴────┘
 *  ▲ Producer position (RX)
 *                                   ▲ Consumer position (DPI)
 * 
 * Key issue: Which packets should Core 1 take? Which should Core 2 take?
 * 
 * Without coordination (RACE CONDITION):
 * 
 *   Time 1: DPI Core 1 reads: tail = 0, count = 8
 *   Time 2: DPI Core 2 reads: tail = 0, count = 8
 *   Time 3: DPI Core 1 executes: dequeue 4 packets (pkt1-pkt4), tail = 4
 *   Time 4: DPI Core 2 executes: dequeue 4 packets (pkt1-pkt4), tail = 4
 *   
 *   PROBLEM: Both cores got the SAME packets! pkt5-pkt8 were skipped!
 *   This is a RACE CONDITION - data corruption!
 * 
 * With CAS (Compare-And-Swap) in SPMC:
 * 
 *   Time 1: DPI Core 1: CAS(tail, 0, 4) → Success! Gets pkt1-pkt4
 *   Time 2: DPI Core 2: CAS(tail, 4, 8) → Success! Gets pkt5-pkt8
 *   
 *   CORRECT: Each packet goes to exactly one consumer!
 */

// ============================================================================
// CAS (COMPARE-AND-SWAP) EXPLAINED
// ============================================================================

/*
 * CAS = Atomic Compare-And-Swap instruction
 * 
 * Pseudo-code:
 * 
 *   bool CAS(volatile int *ptr, int old_value, int new_value) {
 *       if (*ptr == old_value) {
 *           *ptr = new_value;
 *           return true;   // Success - you own this update
 *       }
 *       return false;      // Failed - someone else already updated it
 *   }
 * 
 * Example in DPDK SPMC dequeue:
 * 
 *   Ring state: tail = 5 (next packet to dequeue)
 *   
 *   Thread 1:
 *     old_tail = 5
 *     CAS(&ring->tail, 5, 9)  → Success! Thread 1 claims packets 5-8
 *     Returns packets[5:9]
 *   
 *   Thread 2 (happens at same time):
 *     old_tail = 5
 *     CAS(&ring->tail, 5, 9)  → FAIL! tail is now 9, not 5
 *     CAS(&ring->tail, 9, 13) → Success! Thread 2 claims packets 9-12
 *     Returns packets[9:13]
 * 
 * Hardware guarantees:
 * - CAS is atomic (indivisible)
 * - Only ONE thread wins the compare-swap
 * - Other threads see failure and retry with new tail value
 * - No mutexes, no spinning locks
 * - Very fast (~3-5 CPU cycles)
 */

// ============================================================================
// RING TYPES FOR YOUR PIPELINE
// ============================================================================

/*
 * ┌─────────────────────────────────────────────────────────────────┐
 * │ Stage 1: RX Core → Multiple DPI Cores                          │
 * ├─────────────────────────────────────────────────────────────────┤
 * │                                                                   │
 * │ Architecture:                                                     │
 * │                                                                   │
 * │   RX Core (1)          DPI Cores (2-4)                          │
 * │      │                      │                                    │
 * │      └──→ SPMC Ring ←───────┘                                   │
 * │                                                                   │
 * │ Ring flags: RING_F_SP_ENQ | RING_F_MC_DEQ                      │
 * │   - SP: Single Producer (RX only writes)                        │
 * │   - MC: Multi-Consumer (DPI cores compete with CAS)            │
 * │                                                                   │
 * │ Enqueue (RX Core):                                              │
 * │   rte_ring_sp_enqueue_burst(ring, (void**)pkts, 64, NULL)      │
 * │   - No atomic ops (only 1 producer)                            │
 * │   - Very fast                                                   │
 * │                                                                   │
 * │ Dequeue (DPI Cores 1-4):                                        │
 * │   rte_ring_mc_dequeue_burst(ring, (void**)pkts, 16, NULL)      │
 * │   - Each core uses CAS to claim its own packets                │
 * │   - No race conditions                                          │
 * │   - Slightly slower than SC due to CAS overhead               │
 * │                                                                   │
 * └─────────────────────────────────────────────────────────────────┘
 */

/*
 * ┌─────────────────────────────────────────────────────────────────┐
 * │ Stage 2: Multiple DPI Cores → Multiple QoE Cores               │
 * ├─────────────────────────────────────────────────────────────────┤
 * │                                                                   │
 * │ Architecture:                                                     │
 * │                                                                   │
 * │   DPI Cores (2-4)      QoE Cores (2-4)                         │
 * │      │                      │                                    │
 * │      └──→ MPMC Ring ←───────┘                                   │
 * │                                                                   │
 * │ Ring flags: RING_F_MP_ENQ | RING_F_MC_DEQ                      │
 * │   - MP: Multi-Producer (multiple DPI cores write)              │
 * │   - MC: Multi-Consumer (multiple QoE cores read)               │
 * │                                                                   │
 * │ Enqueue (DPI Cores):                                            │
 * │   rte_ring_mp_enqueue_burst(ring, (void**)pkts, 16, NULL)      │
 * │   - Each DPI core uses CAS to add its packets                  │
 * │   - More contention than SP (multiple producers)               │
 * │                                                                   │
 * │ Dequeue (QoE Cores):                                            │
 * │   rte_ring_mc_dequeue_burst(ring, (void**)pkts, 16, NULL)      │
 * │   - Each QoE core uses CAS to claim packets                    │
 * │   - Fair distribution among QoE cores                          │
 * │                                                                   │
 * └─────────────────────────────────────────────────────────────────┘
 */

// ============================================================================
// PACKET DISTRIBUTION: HOW MPMC WORKS
// ============================================================================

/*
 * Question: If multiple DPI cores produce packets to ring,
 * how does DPDK ensure QoE cores don't get duplicates?
 * 
 * Answer: Through ENQUEUE ORDERING + CAS-based DEQUEUE
 * 
 * Example: 2 DPI cores, 2 QoE cores
 * 
 * Timeline:
 * 
 * T1: DPI Core 1 enqueues 16 packets
 *     Ring: [pkt0...pkt15]
 *           tail = 16
 * 
 * T2: DPI Core 2 enqueues 16 packets
 *     Ring: [pkt0...pkt31]
 *           tail = 32
 * 
 * T3: QoE Core A calls dequeue_mc_burst(ring, bufs, 16)
 *     - Reads: tail = 32, head = 0
 *     - CAS(&head, 0, 16) → Success!
 *     - Gets packets 0-15
 * 
 * T4: QoE Core B calls dequeue_mc_burst(ring, bufs, 16)
 *     - Reads: tail = 32, head = 16 (updated by Core A)
 *     - CAS(&head, 16, 32) → Success!
 *     - Gets packets 16-31
 * 
 * Result:
 * ┌────────────────────────────────────────────────┐
 * │ Ring (linear view with head/tail pointers)     │
 * ├────────────────────────────────────────────────┤
 * │                                                 │
 * │ [pkt0][pkt1]...[pkt15] [pkt16]...[pkt31]       │
 * │  ▲                      ▲                       │
 * │  │ head (= 32)          │                       │
 * │  │ consumed             │ enqueued tail = 32   │
 * │  │                      │                       │
 * │  └─ QoE Core A got 0-15 │                      │
 * │  └─ QoE Core B got 16-31 │                      │
 * │                                                 │
 * │ No duplicates! No data loss!                   │
 * │                                                 │
 * └────────────────────────────────────────────────┘
 */

// ============================================================================
// ACTUAL DPDK RING IMPLEMENTATION (SIMPLIFIED)
// ============================================================================

/*
 * struct rte_ring {
 *     struct rte_ring_headtail prod asm("__prod");  // Producer side
 *     struct rte_ring_headtail cons asm("__cons");  // Consumer side
 *     
 *     struct rte_ring_headtail {
 *         uint32_t head;      // Next position to produce/consume
 *         uint32_t tail;      // Last position produced/consumed
 *         uint32_t sync_type; // SP, SC, MP, MC flag
 *     }
 *     
 *     void *ring[size];       // Actual ring buffer
 * }
 * 
 * Enqueue (producer side):
 * 
 *   void rte_ring_sp_enqueue_burst() {
 *       // Single producer: no CAS needed
 *       uint32_t free = ring->cons.tail - ring->prod.head;
 *       if (free >= count) {
 *           memcpy(&ring->ring[prod.head], objs, count);
 *           prod.head += count;  // Direct write, no atomic needed
 *       }
 *   }
 * 
 *   void rte_ring_mp_enqueue_burst() {
 *       // Multi producer: use CAS
 *       while (true) {
 *           old_head = ring->prod.head;
 *           new_head = old_head + count;
 *           
 *           if (CAS(&ring->prod.head, old_head, new_head)) {
 *               memcpy(&ring->ring[old_head], objs, count);
 *               ring->prod.tail = new_head;  // Update when ready
 *               return;
 *           }
 *           // If CAS failed, retry with updated head
 *       }
 *   }
 * 
 * Dequeue (consumer side):
 * 
 *   void rte_ring_sc_dequeue_burst() {
 *       // Single consumer: no CAS needed
 *       uint32_t available = ring->prod.tail - ring->cons.head;
 *       if (available >= count) {
 *           memcpy(objs, &ring->ring[cons.head], count);
 *           cons.head += count;  // Direct write
 *       }
 *   }
 * 
 *   void rte_ring_mc_dequeue_burst() {
 *       // Multi consumer: use CAS
 *       while (true) {
 *           old_head = ring->cons.head;
 *           new_head = old_head + count;
 *           
 *           if (CAS(&ring->cons.head, old_head, new_head)) {
 *               memcpy(objs, &ring->ring[old_head], count);
 *               return;
 *           }
 *           // If CAS failed, retry with updated head
 *       }
 *   }
 */

// ============================================================================
// PERFORMANCE COMPARISON
// ============================================================================

/*
 * Throughput (packets/sec) - higher is better:
 * 
 * Ring Type        SP Enq   SC Deq   Total      Contention
 * ─────────────────────────────────────────────────────────
 * SP/SC (simple)   Fast     Fast     10 Mpps    None
 * SP/MC            Fast     Slow     8 Mpps     MC contention
 * MP/SC            Slow     Fast     8 Mpps     MP contention
 * MP/MC            Slow     Slow     5 Mpps     Both sides
 * 
 * Cycles per packet:
 * 
 * SP enqueue:  ~3-5 cycles (simple head/tail update)
 * MP enqueue:  ~20-30 cycles (CAS retry loops)
 * SC dequeue:  ~3-5 cycles (simple head/tail update)
 * MC dequeue:  ~20-30 cycles (CAS retry loops)
 * 
 * Your pipeline throughput bottleneck:
 * - Stage 1: 10 Mpps (SP in, MC out) - limited by MC CAS
 * - Stage 2: 8 Mpps (MP in, MC out) - limited by both MP and MC
 * - Stage 3: 8 Mpps (MP in, MC out) - limited by both MP and MC
 * - Stage 4: 8 Mpps (MP in, SC out) - limited by MP CAS
 * 
 * Total: ~8 Mpps per port (your bottleneck is MP/MC stages)
 */

// ============================================================================
// PRACTICAL RECOMMENDATION FOR YOUR USE CASE
// ============================================================================

/*
 * If you want max performance with your 4-stage pipeline:
 * 
 * Option 1: Use load balancing (RECOMMENDED)
 * ──────────────────────────────────────────
 * 
 *   RX (1 core)
 *      │
 *      ├─→ SPMC Ring 1 ←─ DPI Group A (2 cores)
 *      ├─→ SPMC Ring 2 ←─ DPI Group B (2 cores)
 *      └─→ SPMC Ring 3 ←─ DPI Group C (2 cores)
 *
 *   Then each DPI group feeds a separate QoE ring, etc.
 *   
 *   Benefit: Each ring only has 1 producer
 *   Cost: More rings to manage, more complex
 *   Throughput: 10 Mpps per path × 3 = 30 Mpps total
 * 
 * Option 2: Accept MPMC bottleneck (SIMPLER)
 * ──────────────────────────────────────────
 * 
 *   RX → SPMC → [DPI cores] → MPMC → [QoE cores] → MPMC → [UBB cores] → MPSC → TX
 *   
 *   Benefit: Simple, easy to understand
 *   Cost: Each core does CAS, more contention
 *   Throughput: ~8 Mpps (acceptable for most telco apps)
 * 
 * Option 3: Use per-core rings (MOST COMPLEX)
 * ───────────────────────────────────────────
 * 
 *   Each core has its own ring buffer
 *   RX distributes packets to multiple rings (e.g., hash-based)
 *   DPI cores pull from assigned rings
 *   
 *   Benefit: Zero lock contention, max speed
 *   Cost: Complex distribution logic, memory overhead
 *   Throughput: 10+ Mpps (best possible)
 */

// ============================================================================
// CODE EXAMPLE: YOUR 4-STAGE PIPELINE WITH RINGS
// ============================================================================

#include <rte_ring.h>
#include <rte_mbuf.h>

struct PipelineRings {
    struct rte_ring *rx_to_dpi;      // SPMC (1 RX → N DPI)
    struct rte_ring *dpi_to_qoe;     // MPMC (N DPI → N QoE)
    struct rte_ring *qoe_to_ubb;     // MPMC (N QoE → N UBB)
    struct rte_ring *ubb_to_tx;      // MPSC (N UBB → 1 TX)
};

// Initialize pipeline rings
int init_pipeline_rings(struct PipelineRings *rings) {
    // RX → DPI: Single Producer (RX), Multi Consumer (DPI cores)
    rings->rx_to_dpi = rte_ring_create(
        "rx_to_dpi",
        4096,  // Must be power of 2
        rte_socket_id(),
        RING_F_SP_ENQ | RING_F_MC_DEQ  // SP enqueue, MC dequeue
    );
    if (!rings->rx_to_dpi) return -1;

    // DPI → QoE: Multi Producer (DPI cores), Multi Consumer (QoE cores)
    rings->dpi_to_qoe = rte_ring_create(
        "dpi_to_qoe",
        4096,
        rte_socket_id(),
        RING_F_MP_ENQ | RING_F_MC_DEQ  // MP enqueue, MC dequeue
    );
    if (!rings->dpi_to_qoe) return -1;

    // QoE → UBB: Multi Producer (QoE cores), Multi Consumer (UBB cores)
    rings->qoe_to_ubb = rte_ring_create(
        "qoe_to_ubb",
        4096,
        rte_socket_id(),
        RING_F_MP_ENQ | RING_F_MC_DEQ  // MP enqueue, MC dequeue
    );
    if (!rings->qoe_to_ubb) return -1;

    // UBB → TX: Multi Producer (UBB cores), Single Consumer (TX)
    rings->ubb_to_tx = rte_ring_create(
        "ubb_to_tx",
        4096,
        rte_socket_id(),
        RING_F_MP_ENQ | RING_F_SC_DEQ  // MP enqueue, SC dequeue
    );
    if (!rings->ubb_to_tx) return -1;

    return 0;
}

// RX Core function
int lcore_rx(struct PipelineRings *rings) {
    struct rte_mbuf *pkts[64];
    uint16_t nb_rx;

    while (!quit) {
        // Receive from NIC
        nb_rx = rte_eth_rx_burst(port, 0, pkts, 64);
        if (nb_rx == 0) continue;

        // Enqueue to DPI ring (single producer - no CAS)
        rte_ring_sp_enqueue_burst(rings->rx_to_dpi, 
                                 (void**)pkts, 
                                 nb_rx, 
                                 NULL);
    }

    return 0;
}

// DPI Core function (multiple cores running this)
int lcore_dpi(struct PipelineRings *rings) {
    struct rte_mbuf *pkts[64];
    uint16_t nb_pkt;

    while (!quit) {
        // Dequeue from RX (multi consumer - uses CAS)
        nb_pkt = rte_ring_mc_dequeue_burst(rings->rx_to_dpi,
                                          (void**)pkts,
                                          64,
                                          NULL);
        if (nb_pkt == 0) continue;

        // Process packets (DPI classification)
        for (uint16_t i = 0; i < nb_pkt; i++) {
            // ... DPI classification logic ...
        }

        // Enqueue to QoE ring (multi producer - uses CAS)
        rte_ring_mp_enqueue_burst(rings->dpi_to_qoe,
                                 (void**)pkts,
                                 nb_pkt,
                                 NULL);
    }

    return 0;
}

// QoE Core function (multiple cores running this)
int lcore_qoe(struct PipelineRings *rings) {
    struct rte_mbuf *pkts[64];
    uint16_t nb_pkt;

    while (!quit) {
        // Dequeue from DPI (multi consumer - uses CAS)
        nb_pkt = rte_ring_mc_dequeue_burst(rings->dpi_to_qoe,
                                          (void**)pkts,
                                          64,
                                          NULL);
        if (nb_pkt == 0) continue;

        // Process packets (collect QoE metrics)
        for (uint16_t i = 0; i < nb_pkt; i++) {
            // ... QoE collection logic ...
        }

        // Enqueue to UBB ring (multi producer - uses CAS)
        rte_ring_mp_enqueue_burst(rings->qoe_to_ubb,
                                 (void**)pkts,
                                 nb_pkt,
                                 NULL);
    }

    return 0;
}

// UBB Core function (multiple cores running this)
int lcore_ubb(struct PipelineRings *rings) {
    struct rte_mbuf *pkts[64];
    uint16_t nb_pkt;

    while (!quit) {
        // Dequeue from QoE (multi consumer - uses CAS)
        nb_pkt = rte_ring_mc_dequeue_burst(rings->qoe_to_ubb,
                                          (void**)pkts,
                                          64,
                                          NULL);
        if (nb_pkt == 0) continue;

        // Process packets (meter traffic)
        for (uint16_t i = 0; i < nb_pkt; i++) {
            // ... UBB metering logic ...
        }

        // Enqueue to TX ring (multi producer - uses CAS)
        rte_ring_mp_enqueue_burst(rings->ubb_to_tx,
                                 (void**)pkts,
                                 nb_pkt,
                                 NULL);
    }

    return 0;
}

// TX Core function
int lcore_tx(struct PipelineRings *rings) {
    struct rte_mbuf *pkts[64];
    uint16_t nb_pkt;

    while (!quit) {
        // Dequeue from UBB (single consumer - no CAS)
        nb_pkt = rte_ring_sc_dequeue_burst(rings->ubb_to_tx,
                                          (void**)pkts,
                                          64,
                                          NULL);
        if (nb_pkt == 0) continue;

        // Transmit to NIC
        rte_eth_tx_burst(port, 0, pkts, nb_pkt);
    }

    return 0;
}

// ============================================================================
// KEY TAKEAWAYS
// ============================================================================

/*
 * 1. YES, you need ring buffers between stages
 *    - Decouples producers from consumers
 *    - Allows different core counts per stage
 *    - Prevents packet loss under bursts
 * 
 * 2. Ring type depends on producer/consumer count:
 *    - 1 producer, 1 consumer: SP/SC (fastest, no atomics)
 *    - 1 producer, N consumers: SP/MC (uses CAS on consume)
 *    - N producers, 1 consumer: MP/SC (uses CAS on produce)
 *    - N producers, N consumers: MP/MC (uses CAS on both)
 * 
 * 3. CAS (Compare-And-Swap) prevents duplicates:
 *    - Only ONE consumer gets a specific packet
 *    - Atomic hardware instruction (~3-5 cycles)
 *    - No mutexes, no spinning
 * 
 * 4. Performance trade-off:
 *    - SP/SC: ~10 Mpps
 *    - SP/MC or MP/SC: ~8 Mpps (CAS overhead on one side)
 *    - MP/MC: ~5-8 Mpps (CAS overhead on both sides)
 * 
 * 5. Your architecture should use:
 *    - RX → DPI: SPMC (ring flag: RING_F_SP_ENQ | RING_F_MC_DEQ)
 *    - DPI → QoE: MPMC (ring flag: RING_F_MP_ENQ | RING_F_MC_DEQ)
 *    - QoE → UBB: MPMC (ring flag: RING_F_MP_ENQ | RING_F_MC_DEQ)
 *    - UBB → TX: MPSC (ring flag: RING_F_MP_ENQ | RING_F_SC_DEQ)
 */
