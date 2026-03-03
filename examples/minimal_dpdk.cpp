/*
 * Minimal DPDK Application Example
 * 
 * This example demonstrates:
 * - EAL initialization
 * - Port configuration (RX/TX queues)
 * - Mempool setup (packet buffer management)
 * - Ring buffer (inter-core communication)
 * - Simple RX/TX packet forwarding loop
 * 
 * Build: cd build && cmake .. && make
 * Run:   ./examples/minimal_dpdk -l 0,1 -- -p 0x3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ring.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

#define BURST_SIZE          64      // Packets per RX/TX burst
#define MEMPOOL_MBUF_NUM    8192    // Number of mbufs in pool
#define MEMPOOL_CACHE_SIZE  256     // Per-lcore cache size
#define RING_SIZE           2048    // Ring buffer size
#define MAX_PORTS           32
#define MAX_RX_QUEUE_PER_PORT 128
#define MAX_TX_QUEUE_PER_PORT 128

// ============================================================================
// GLOBAL STRUCTURES
// ============================================================================

// Ring buffer for inter-core packet passing
struct rte_ring *rx_to_tx_ring = NULL;

// Mempool for packet buffers
struct rte_mempool *pktmbuf_pool = NULL;

// Port mask (bitmap of active ports)
uint32_t enabled_portmask = 0;

// Global statistics
volatile bool force_quit = false;
uint64_t tx_packets = 0;
uint64_t rx_packets = 0;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nReceived signal %d, exiting...\n", signum);
        force_quit = true;
    }
}

/*
 * Print port statistics periodically
 */
static void print_stats(void) {
    printf("\n====== Port Statistics ======\n");
    printf("RX Packets: %" PRIu64 "\n", rx_packets);
    printf("TX Packets: %" PRIu64 "\n", tx_packets);
    printf("Ring occupancy: %u\n", rte_ring_count(rx_to_tx_ring));
    printf("Mempool available: %u\n", rte_mempool_avail_count(pktmbuf_pool));
    printf("Mempool in use: %u\n", rte_mempool_in_use_count(pktmbuf_pool));
    printf("=============================\n");
}

/*
 * Allocate mempool for packet buffers
 * 
 * Mempool = pool of pre-allocated mbufs (memory buffers) for zero-copy packet handling
 * - Avoids malloc/free overhead
 * - Each lcore has local cache (MEMPOOL_CACHE_SIZE) for allocation speed
 * - Supports NUMA-aware allocation
 */
static int init_mempool(void) {
    printf("Creating mempool with %u mbufs, cache size %u...\n",
           MEMPOOL_MBUF_NUM, MEMPOOL_CACHE_SIZE);

    pktmbuf_pool = rte_pktmbuf_pool_create(
        "pktmbuf_pool",           // Pool name
        MEMPOOL_MBUF_NUM,         // Number of elements
        MEMPOOL_CACHE_SIZE,       // Per-lcore cache size
        0,                        // Private data size
        RTE_MBUF_DEFAULT_BUF_SIZE, // Mbuf data buffer size
        rte_socket_id()           // NUMA socket
    );

    if (!pktmbuf_pool) {
        RTE_LOG(ERR, USER1, "Cannot create mempool\n");
        return -1;
    }

    printf("Mempool created successfully\n");
    return 0;
}

/*
 * Create ring buffer for inter-core communication
 * 
 * Ring = lock-free circular queue for passing pointers between cores
 * - SP = Single Producer, SC = Single Consumer (no locking if only 1 core uses)
 * - MP = Multi-Producer, MC = Multi-Consumer (uses atomic ops, slightly slower)
 * - Used here to pass packets from RX core to TX core
 */
static int init_ring(void) {
    printf("Creating ring buffer with size %u...\n", RING_SIZE);

    rx_to_tx_ring = rte_ring_create(
        "rx_to_tx",     // Ring name
        RING_SIZE,      // Ring size (must be power of 2)
        rte_socket_id(), // NUMA socket
        RING_F_SP_ENQ | RING_F_SC_DEQ  // Single producer/consumer flags
    );

    if (!rx_to_tx_ring) {
        RTE_LOG(ERR, USER1, "Cannot create ring\n");
        return -1;
    }

    printf("Ring created successfully\n");
    return 0;
}

/*
 * Configure a single Ethernet port
 * 
 * This sets up RX and TX queues for a port
 * - RX queues: incoming packets from NIC
 * - TX queues: outgoing packets to NIC
 * - Each queue has a mempool for packet buffers
 */
static int configure_port(uint16_t port_id) {
    struct rte_eth_conf port_conf;
    struct rte_eth_txconf txconf;
    int retval;
    uint16_t q;

    printf("Configuring port %" PRIu16 "...\n", port_id);

    // Get default config and update
    memset(&port_conf, 0, sizeof(struct rte_eth_conf));
    port_conf.rxmode.mtu = 9000;  // Jumbo frame support
    port_conf.rxmode.offloads = 0;
    port_conf.txmode.offloads = 0;

    // Configure port with 1 RX queue and 1 TX queue
    retval = rte_eth_dev_configure(port_id, 1, 1, &port_conf);
    if (retval != 0) {
        printf("Cannot configure port %" PRIu16 "\n", port_id);
        return retval;
    }

    // Setup RX queue
    printf("  Setting up RX queue 0...\n");
    retval = rte_eth_rx_queue_setup(
        port_id,
        0,                      // Queue ID
        BURST_SIZE * 2,         // Ring size
        rte_eth_dev_socket_id(port_id),
        NULL,                   // Use default config
        pktmbuf_pool            // Memory pool
    );
    if (retval < 0) {
        printf("Cannot setup RX queue\n");
        return retval;
    }

    // Setup TX queue
    printf("  Setting up TX queue 0...\n");
    txconf = rte_eth_dev_default_txconf_get(port_id);
    retval = rte_eth_tx_queue_setup(
        port_id,
        0,                      // Queue ID
        BURST_SIZE * 2,         // Ring size
        rte_eth_dev_socket_id(port_id),
        &txconf
    );
    if (retval < 0) {
        printf("Cannot setup TX queue\n");
        return retval;
    }

    // Enable promiscuous mode (receive all packets)
    retval = rte_eth_promiscuous_enable(port_id);
    if (retval != 0) {
        printf("Cannot enable promiscuous mode\n");
        return retval;
    }

    printf("  Port %" PRIu16 " configured successfully\n", port_id);
    return 0;
}

/*
 * Initialize and start all enabled ports
 */
static int init_ports(void) {
    uint16_t portid;
    uint16_t nb_ports = rte_eth_dev_count_avail();

    if (nb_ports == 0) {
        RTE_LOG(ERR, USER1, "No Ethernet ports - bye\n");
        return -1;
    }

    printf("Found %u Ethernet ports\n", nb_ports);

    RTE_ETH_FOREACH_DEV(portid) {
        struct rte_eth_dev_info dev_info;

        // Skip ports not in mask
        if ((enabled_portmask & (1 << portid)) == 0)
            continue;

        rte_eth_dev_info_get(portid, &dev_info);
        printf("Port %u: %s\n", portid, dev_info.driver_name);

        if (configure_port(portid) != 0) {
            printf("Cannot configure port %" PRIu16 "\n", portid);
            return -1;
        }

        // Start port
        if (rte_eth_dev_start(portid) < 0) {
            printf("Cannot start port %" PRIu16 "\n", portid);
            return -1;
        }

        printf("Port %" PRIu16 " started\n", portid);
    }

    return 0;
}

/*
 * RX Core Function - receives packets and enqueues to ring
 * 
 * This runs on a dedicated lcore, continuously:
 * 1. Receives burst of packets from port RX queue
 * 2. Enqueues packets to ring buffer for processing
 * 3. Frees any packets that couldn't be enqueued
 */
static int lcore_rx(void *arg) {
    uint16_t port_id = (uintptr_t) arg;
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;
    uint16_t nb_enqueued;
    uint16_t i;

    printf("[RX] Core %u started, receiving from port %" PRIu16 "\n",
           rte_lcore_id(), port_id);

    while (!force_quit) {
        // Receive burst of packets from NIC
        nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
            continue;

        rx_packets += nb_rx;

        // Enqueue packets to ring for TX core
        nb_enqueued = rte_ring_sp_enqueue_burst(rx_to_tx_ring,
                                               (void **) bufs,
                                               nb_rx,
                                               NULL);

        // Free any packets that couldn't be enqueued
        if (unlikely(nb_enqueued < nb_rx)) {
            for (i = nb_enqueued; i < nb_rx; i++) {
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }

    printf("[RX] Core %u exiting\n", rte_lcore_id());
    return 0;
}

/*
 * TX Core Function - dequeues packets from ring and transmits
 * 
 * This runs on a dedicated lcore, continuously:
 * 1. Dequeues packets from ring buffer
 * 2. Optionally processes packet (e.g., swap MAC addresses)
 * 3. Transmits burst to port TX queue
 */
static int lcore_tx(void *arg) {
    uint16_t port_id = (uintptr_t) arg;
    struct rte_mbuf *bufs[BURST_SIZE];
    struct rte_ether_hdr *eth_hdr;
    struct rte_ether_addr tmp_addr;
    uint16_t nb_dequeued;
    uint16_t nb_tx;
    uint16_t i;

    printf("[TX] Core %u started, transmitting on port %" PRIu16 "\n",
           rte_lcore_id(), port_id);

    while (!force_quit) {
        // Dequeue packets from ring
        nb_dequeued = rte_ring_sc_dequeue_burst(rx_to_tx_ring,
                                               (void **) bufs,
                                               BURST_SIZE,
                                               NULL);

        if (unlikely(nb_dequeued == 0))
            continue;

        // Simple packet processing: swap source and destination MAC addresses
        for (i = 0; i < nb_dequeued; i++) {
            eth_hdr = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *);
            
            // Swap MAC addresses (simple loopback behavior)
            tmp_addr = eth_hdr->dst_addr;
            eth_hdr->dst_addr = eth_hdr->src_addr;
            eth_hdr->src_addr = tmp_addr;
        }

        // Transmit burst to NIC
        nb_tx = rte_eth_tx_burst(port_id, 0, bufs, nb_dequeued);

        // Free any packets that couldn't be transmitted
        if (unlikely(nb_tx < nb_dequeued)) {
            for (i = nb_tx; i < nb_dequeued; i++) {
                rte_pktmbuf_free(bufs[i]);
            }
            tx_packets += nb_tx;
        } else {
            tx_packets += nb_dequeued;
        }
    }

    printf("[TX] Core %u exiting\n", rte_lcore_id());
    return 0;
}

/*
 * Stats core - prints statistics periodically
 */
static int lcore_stats(void *arg) {
    (void) arg;

    printf("[STATS] Core %u started\n", rte_lcore_id());

    while (!force_quit) {
        sleep(2);
        print_stats();
    }

    printf("[STATS] Core %u exiting\n", rte_lcore_id());
    return 0;
}

/*
 * Parse command line arguments
 */
static int parse_args(int argc, char **argv) {
    int opt;
    extern char *optarg;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            enabled_portmask = strtoul(optarg, NULL, 16);
            break;
        default:
            printf("Usage: %s -l lcore_list -- -p port_mask\n", argv[0]);
            printf("  -p port_mask: hex bitmask of ports to enable\n");
            printf("  Example: %s -l 0,1,2,3 -- -p 0x3\n", argv[0]);
            printf("           (enables ports 0 and 1)\n");
            return -1;
        }
    }

    if (enabled_portmask == 0) {
        printf("No ports enabled. Use -p option.\n");
        return -1;
    }

    return 0;
}

/*
 * Main function - initializes DPDK and launches worker lcores
 */
int main(int argc, char *argv[]) {
    unsigned lcore_id;
    int ret;
    unsigned nb_lcores;

    // Register signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize DPDK EAL (Environment Abstraction Layer)
    printf("Initializing DPDK EAL...\n");
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

    argc -= ret;
    argv += ret;

    printf("DPDK EAL initialized\n");
    printf("EAL timer resolution: %" PRIu64 " cycles per second\n", rte_get_timer_hz());

    // Parse application arguments
    printf("Parsing application arguments...\n");
    ret = parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid arguments\n");

    // Initialize mempool
    if (init_mempool() < 0)
        rte_exit(EXIT_FAILURE, "Cannot init mempool\n");

    // Initialize ring buffer
    if (init_ring() < 0)
        rte_exit(EXIT_FAILURE, "Cannot init ring\n");

    // Initialize and configure ports
    if (init_ports() < 0)
        rte_exit(EXIT_FAILURE, "Cannot init ports\n");

    // Count available lcores
    nb_lcores = rte_lcore_count();
    printf("Available lcores: %u\n", nb_lcores);

    if (nb_lcores < 3) {
        rte_exit(EXIT_FAILURE, "Need at least 3 lcores (RX, TX, Stats)\n");
    }

    // Launch RX worker on lcore 1
    printf("Launching RX worker on lcore 1...\n");
    rte_eal_remote_launch(lcore_rx, (void *)(uintptr_t) 0, 1);

    // Launch TX worker on lcore 2
    printf("Launching TX worker on lcore 2...\n");
    rte_eal_remote_launch(lcore_tx, (void *)(uintptr_t) 0, 2);

    // Launch stats worker on lcore 3 (if available)
    if (nb_lcores >= 3) {
        printf("Launching stats worker on lcore 3...\n");
        rte_eal_remote_launch(lcore_stats, NULL, 3);
    }

    printf("\n========================================\n");
    printf("DPDK Application Running\n");
    printf("Press Ctrl+C to stop\n");
    printf("========================================\n\n");

    // Wait for all lcores to finish
    rte_eal_mp_wait_lcore();

    printf("\n========================================\n");
    printf("Final Statistics:\n");
    print_stats();
    printf("========================================\n");

    // Cleanup
    printf("Cleaning up...\n");
    RTE_ETH_FOREACH_DEV(lcore_id) {
        if ((enabled_portmask & (1 << lcore_id)) == 0)
            continue;
        printf("Stopping port %u\n", lcore_id);
        rte_eth_dev_stop(lcore_id);
        rte_eth_dev_close(lcore_id);
    }

    rte_eal_cleanup();
    printf("Cleanup complete\n");

    return 0;
}
