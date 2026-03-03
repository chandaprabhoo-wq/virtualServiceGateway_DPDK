/*
 * DPDK Packet Processing - Common Modifications & Examples
 * 
 * This file demonstrates common tasks you might want to add
 * to the minimal DPDK application.
 */

#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_ring.h>
#include <stdio.h>

// ============================================================================
// 1. EXTRACT PACKET HEADERS & METADATA
// ============================================================================

struct PacketHeaders {
    struct rte_ether_hdr *eth;
    struct rte_ipv4_hdr *ipv4;
    struct rte_ipv6_hdr *ipv6;
    struct rte_tcp_hdr *tcp;
    struct rte_udp_hdr *udp;
    uint32_t payload_size;
    int l3_proto;  // RTE_ETHER_TYPE_IPV4, RTE_ETHER_TYPE_IPV6
    int l4_proto;  // IPPROTO_TCP, IPPROTO_UDP, etc.
};

/**
 * Extract packet headers from mbuf
 * 
 * Example usage:
 *   struct PacketHeaders hdrs;
 *   if (extract_packet_headers(pkt, &hdrs) == 0) {
 *       printf("TCP Port: %u -> %u\n",
 *              ntohs(hdrs.tcp->src_port),
 *              ntohs(hdrs.tcp->dst_port));
 *   }
 */
int extract_packet_headers(struct rte_mbuf *pkt, struct PacketHeaders *hdrs) {
    uint32_t pkt_len = rte_pktmbuf_pkt_len(pkt);
    
    if (unlikely(pkt_len < sizeof(struct rte_ether_hdr)))
        return -1;

    // Layer 2: Ethernet
    hdrs->eth = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr*);
    hdrs->l3_proto = hdrs->eth->ether_type;

    // Layer 3: IPv4 or IPv6
    uint8_t *data_ptr = (uint8_t *)hdrs->eth + sizeof(struct rte_ether_hdr);
    
    if (hdrs->l3_proto == RTE_ETHER_TYPE_IPV4) {
        if (unlikely(pkt_len < sizeof(struct rte_ether_hdr) + 
                              sizeof(struct rte_ipv4_hdr)))
            return -1;
        
        hdrs->ipv4 = (struct rte_ipv4_hdr *)data_ptr;
        hdrs->l4_proto = hdrs->ipv4->next_proto_id;
        
        data_ptr += (hdrs->ipv4->version_ihl & 0x0f) * 4;  // IHL in 32-bit words
        
    } else if (hdrs->l3_proto == RTE_ETHER_TYPE_IPV6) {
        if (unlikely(pkt_len < sizeof(struct rte_ether_hdr) + 
                              sizeof(struct rte_ipv6_hdr)))
            return -1;
        
        hdrs->ipv6 = (struct rte_ipv6_hdr *)data_ptr;
        hdrs->l4_proto = hdrs->ipv6->proto;
        
        data_ptr += sizeof(struct rte_ipv6_hdr);
    } else {
        return -1;  // Unknown L3 protocol
    }

    // Layer 4: TCP or UDP
    if (hdrs->l4_proto == IPPROTO_TCP) {
        hdrs->tcp = (struct rte_tcp_hdr *)data_ptr;
        hdrs->payload_size = pkt_len - (data_ptr - (uint8_t *)hdrs->eth) -
                            ((hdrs->tcp->data_off >> 4) * 4);
    } else if (hdrs->l4_proto == IPPROTO_UDP) {
        hdrs->udp = (struct rte_udp_hdr *)data_ptr;
        hdrs->payload_size = ntohs(hdrs->udp->dgram_len) - 
                            sizeof(struct rte_udp_hdr);
    } else {
        // Other L4 protocol
        hdrs->payload_size = pkt_len - (data_ptr - (uint8_t *)hdrs->eth);
    }

    return 0;
}

// ============================================================================
// 2. CLASSIFY PACKET FLOW (DPI SIMULATION)
// ============================================================================

enum PacketClass {
    TRAFFIC_HTTP = 0,
    TRAFFIC_HTTPS = 1,
    TRAFFIC_DNS = 2,
    TRAFFIC_SSH = 3,
    TRAFFIC_OTHER = 4,
};

/**
 * Simple DPI classification based on port numbers
 * 
 * Real DPI would do deep packet inspection and signature matching
 * This is just a simple heuristic based on well-known ports.
 * 
 * Example usage:
 *   enum PacketClass class = classify_packet(&hdrs);
 *   switch (class) {
 *       case TRAFFIC_HTTP: break;
 *       case TRAFFIC_HTTPS: break;
 *       // ...
 *   }
 */
enum PacketClass classify_packet(struct PacketHeaders *hdrs) {
    uint16_t sport = 0, dport = 0;

    if (hdrs->tcp) {
        sport = ntohs(hdrs->tcp->src_port);
        dport = ntohs(hdrs->tcp->dst_port);
    } else if (hdrs->udp) {
        sport = ntohs(hdrs->udp->src_port);
        dport = ntohs(hdrs->udp->dst_port);
    } else {
        return TRAFFIC_OTHER;
    }

    // Well-known ports
    if (sport == 80 || dport == 80)
        return TRAFFIC_HTTP;
    if (sport == 443 || dport == 443)
        return TRAFFIC_HTTPS;
    if (sport == 53 || dport == 53)
        return TRAFFIC_DNS;
    if (sport == 22 || dport == 22)
        return TRAFFIC_SSH;

    return TRAFFIC_OTHER;
}

// ============================================================================
// 3. TRAFFIC METERING (BYTE COUNTING)
// ============================================================================

struct MeterStats {
    uint64_t bytes;
    uint64_t packets;
    uint64_t drops;
    uint64_t last_reset_ts;
};

/**
 * Update meter statistics for a flow
 * 
 * In a real system, this would update per-subscriber counters atomically.
 * 
 * Example:
 *   struct MeterStats stats = {0};
 *   update_meter(&stats, 1500);  // 1500 byte packet
 */
void update_meter(struct MeterStats *meter, uint32_t packet_bytes) {
    meter->bytes += packet_bytes;
    meter->packets++;
}

/**
 * Check if flow exceeds quota
 * 
 * Example:
 *   #define QUOTA_BYTES (1000000)  // 1 MB
 *   if (check_quota(&stats, QUOTA_BYTES)) {
 *       printf("Subscriber exceeded quota\n");
 *       // Drop packet or mark for rate limiting
 *   }
 */
int check_quota(struct MeterStats *meter, uint64_t quota_bytes) {
    return meter->bytes > quota_bytes ? 1 : 0;
}

// ============================================================================
// 4. PACKET MODIFICATION (MAC REWRITE)
// ============================================================================

/**
 * Swap source and destination MAC addresses
 * 
 * This is useful for:
 * - Simple loopback testing
 * - Returning packets to sender
 * - Load balancer hairpin
 * 
 * Example:
 *   struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr*);
 *   mac_swap(eth);
 */
void mac_swap(struct rte_ether_hdr *eth) {
    struct rte_ether_addr tmp = eth->dst_addr;
    eth->dst_addr = eth->src_addr;
    eth->src_addr = tmp;
}

/**
 * Rewrite destination MAC address
 * 
 * Useful for:
 * - Forwarding to next hop
 * - Service chaining
 * - Load balancing
 * 
 * Example:
 *   struct rte_ether_addr new_mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
 *   mac_rewrite(eth, &new_mac);
 */
void mac_rewrite(struct rte_ether_hdr *eth, struct rte_ether_addr *new_dst) {
    rte_ether_addr_copy(new_dst, &eth->dst_addr);
}

// ============================================================================
// 5. BATCH PROCESSING WITH PREFETCH
// ============================================================================

/**
 * Process burst with hardware prefetch for cache optimization
 * 
 * Prefetching helps because:
 * 1. Hardware pre-loads next packet headers into L1 cache
 * 2. By the time we process the packet, data is already cached
 * 3. Reduces memory latency from ~200ns to ~3ns
 * 
 * Example:
 *   struct rte_mbuf *bufs[64];
 *   uint16_t nb = rte_eth_rx_burst(port, 0, bufs, 64);
 *   process_burst_optimized(bufs, nb);
 */
void process_burst_optimized(struct rte_mbuf **bufs, uint16_t nb) {
    uint16_t i;
    struct PacketHeaders hdrs;

    // Prefetch first 4 packets
    for (i = 0; i < 4 && i < nb; i++) {
        rte_prefetch0(rte_pktmbuf_mtod(bufs[i], void*));
    }

    for (i = 0; i < nb; i++) {
        // Prefetch next packet ahead of time
        if (i + 4 < nb) {
            rte_prefetch0(rte_pktmbuf_mtod(bufs[i + 4], void*));
        }

        // Process current packet (headers already in cache!)
        if (extract_packet_headers(bufs[i], &hdrs) == 0) {
            enum PacketClass cls = classify_packet(&hdrs);
            // Use classification for routing, metering, etc.
        }
    }
}

// ============================================================================
// 6. MULTI-QUEUE RX/TX
// ============================================================================

/**
 * Receive from multiple RX queues (for multi-core scaling)
 * 
 * Each core can have its own RX queue to avoid contention:
 * 
 *   Port 0:
 *   ├── RX Queue 0 → Core 1
 *   ├── RX Queue 1 → Core 2
 *   ├── RX Queue 2 → Core 3
 *   └── RX Queue 3 → Core 4
 * 
 * Example:
 *   for (q = 0; q < num_queues; q++) {
 *       nb = rte_eth_rx_burst(port, q, bufs, burst_size);
 *       if (nb > 0) {
 *           rte_ring_sp_enqueue_burst(rings[q], (void**)bufs, nb, NULL);
 *       }
 *   }
 */
uint16_t rx_multi_queue(uint16_t port_id, uint16_t nb_queues,
                       struct rte_mbuf **bufs, uint16_t burst_size) {
    uint16_t q, total_rx = 0, nb_rx;

    for (q = 0; q < nb_queues; q++) {
        nb_rx = rte_eth_rx_burst(port_id, q, &bufs[total_rx], 
                                burst_size - total_rx);
        total_rx += nb_rx;
        if (total_rx == burst_size)
            break;
    }

    return total_rx;
}

// ============================================================================
// 7. RING BUFFER OPERATIONS
// ============================================================================

/**
 * Ring buffer wrapper for easier burst operations
 */
struct RingBuffer {
    struct rte_ring *ring;
    const char *name;
};

/**
 * Enqueue burst of packets to ring with error handling
 * 
 * Example:
 *   struct rte_mbuf *bufs[64];
 *   uint16_t nb = rte_eth_rx_burst(port, 0, bufs, 64);
 *   
 *   uint16_t enqueued = ring_enqueue_burst(&rb, bufs, nb);
 *   for (int i = enqueued; i < nb; i++) {
 *       rte_pktmbuf_free(bufs[i]);  // Free dropped packets
 *   }
 */
uint16_t ring_enqueue_burst(struct RingBuffer *rb,
                           struct rte_mbuf **pkts, uint16_t nb) {
    uint16_t enqueued = rte_ring_sp_enqueue_burst(rb->ring,
                                                 (void **)pkts,
                                                 nb, NULL);
    return enqueued;
}

/**
 * Dequeue burst of packets from ring
 * 
 * Example:
 *   struct rte_mbuf *bufs[64];
 *   uint16_t nb = ring_dequeue_burst(&rb, bufs, 64);
 *   
 *   for (int i = 0; i < nb; i++) {
 *       // Process bufs[i]
 *   }
 */
uint16_t ring_dequeue_burst(struct RingBuffer *rb,
                           struct rte_mbuf **pkts, uint16_t nb) {
    uint16_t dequeued = rte_ring_sc_dequeue_burst(rb->ring,
                                                 (void **)pkts,
                                                 nb, NULL);
    return dequeued;
}

// ============================================================================
// 8. STATISTICS TRACKING
// ============================================================================

struct PortStats {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t dropped_packets;
    uint64_t errors;
};

/**
 * Update port statistics
 * 
 * Should be called for each processed burst
 */
void update_stats(struct PortStats *stats,
                 struct rte_mbuf **bufs, uint16_t nb, int is_rx) {
    uint16_t i;
    uint32_t pkt_bytes;

    for (i = 0; i < nb; i++) {
        pkt_bytes = rte_pktmbuf_pkt_len(bufs[i]);
        
        if (is_rx) {
            stats->rx_packets++;
            stats->rx_bytes += pkt_bytes;
        } else {
            stats->tx_packets++;
            stats->tx_bytes += pkt_bytes;
        }
    }
}

/**
 * Print statistics in human-readable format
 */
void print_port_stats(struct PortStats *stats) {
    printf("=== Port Statistics ===\n");
    printf("RX: %lu packets, %lu bytes (%.2f Mbps)\n",
           stats->rx_packets,
           stats->rx_bytes,
           (double)stats->rx_bytes * 8 / 1000000);
    printf("TX: %lu packets, %lu bytes\n",
           stats->tx_packets,
           stats->tx_bytes);
    printf("Dropped: %lu packets\n", stats->dropped_packets);
    printf("Errors: %lu\n", stats->errors);
}

// ============================================================================
// 9. FLOW-BASED LOAD BALANCING
// ============================================================================

/**
 * Calculate flow hash for load balancing
 * 
 * Distributes flows evenly across worker cores
 * Keeps same flow on same core (important for ordering)
 * 
 * Example:
 *   uint32_t flow_hash = compute_flow_hash(&hdrs);
 *   uint16_t queue = flow_hash % num_workers;
 *   rte_ring_sp_enqueue(worker_rings[queue], pkt);
 */
uint32_t compute_flow_hash(struct PacketHeaders *hdrs) {
    uint32_t hash = 0;

    // Hash based on 5-tuple
    if (hdrs->ipv4) {
        hash = hdrs->ipv4->src_addr ^ hdrs->ipv4->dst_addr;
    }
    if (hdrs->tcp) {
        hash ^= ((uint32_t)ntohs(hdrs->tcp->src_port) << 16) |
                ntohs(hdrs->tcp->dst_port);
    } else if (hdrs->udp) {
        hash ^= ((uint32_t)ntohs(hdrs->udp->src_port) << 16) |
                ntohs(hdrs->udp->dst_port);
    }

    return hash;
}

// ============================================================================
// 10. COMPLETE PACKET PROCESSING PIPELINE
// ============================================================================

/**
 * Complete pipeline example: RX → Classify → Meter → TX
 * 
 * This shows how all the pieces fit together
 */
void example_complete_pipeline(
    uint16_t port_id,
    struct rte_ring *output_ring,
    struct rte_mempool *mempool) {
    
    struct rte_mbuf *bufs[64];
    uint16_t nb, i;
    struct PacketHeaders hdrs;
    struct MeterStats meter;
    struct PortStats stats = {0};

    while (1) {
        // 1. RX Burst
        nb = rte_eth_rx_burst(port_id, 0, bufs, 64);
        if (nb == 0)
            continue;

        // 2. Process each packet
        for (i = 0; i < nb; i++) {
            // Extract headers
            if (extract_packet_headers(bufs[i], &hdrs) < 0) {
                rte_pktmbuf_free(bufs[i]);
                stats.errors++;
                continue;
            }

            // Classify traffic
            enum PacketClass cls = classify_packet(&hdrs);

            // Update metering
            update_meter(&meter, rte_pktmbuf_pkt_len(bufs[i]));

            // Check quota
            if (check_quota(&meter, 1000000)) {  // 1 MB quota
                // Drop or rate-limit
                rte_pktmbuf_free(bufs[i]);
                stats.dropped_packets++;
                continue;
            }

            // Modify packet (swap MAC)
            mac_swap(hdrs.eth);

            // Update stats
            update_stats(&stats, &bufs[i], 1, 0);  // Count as TX
        }

        // 3. TX Burst
        uint16_t nb_tx = rte_eth_tx_burst(port_id, 0, bufs, nb);

        // 4. Free unshipped packets
        for (i = nb_tx; i < nb; i++) {
            rte_pktmbuf_free(bufs[i]);
            stats.dropped_packets++;
        }
    }
}

#endif  // Include guard would be added in real code
