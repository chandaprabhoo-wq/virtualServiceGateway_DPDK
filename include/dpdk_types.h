#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_ring.h>
#include <rte_mempool.h>

namespace vsg {
namespace dpdk {

// Packet metadata for fast path processing
struct PacketMetadata {
    uint32_t flow_id;           // For flow hashing/steering
    uint32_t subscriber_id;     // For UBB metering
    uint16_t service_id;        // For QoE tracking
    uint16_t ingress_port;
    uint16_t egress_port;
    uint64_t rx_timestamp_ns;
    uint8_t  dpi_class;         // DPI classification result
    uint8_t  qos_queue;         // QoS queue assignment
    uint32_t packet_bytes;
    bool     uplink;            // Direction indicator
};

// Constants for batch processing
constexpr uint32_t PKT_BURST_SIZE = 64;      // RX/TX burst size
constexpr uint32_t PKT_BATCH_SIZE = 16;      // Processing batch size
constexpr uint32_t NUM_MBUFS = 65536;        // Per-core mempool size
constexpr uint32_t MBUF_CACHE_SIZE = 256;    // Per-lcore cache

// Port configuration
struct PortConfig {
    uint16_t port_id;
    uint16_t rxq_count;
    uint16_t txq_count;
    uint16_t mtu;
    bool     promiscuous;
    bool     jumbo_enabled;
};

// Worker thread configuration
struct WorkerConfig {
    uint32_t worker_id;
    uint32_t lcore_id;
    std::vector<uint16_t> rx_queues;
    std::vector<uint16_t> tx_queues;
    uint32_t numa_node;
    bool     pinned;
};

// Pipeline statistics (atomic operations for lock-free updates)
struct PipelineStats {
    volatile uint64_t rx_pkts;
    volatile uint64_t rx_bytes;
    volatile uint64_t tx_pkts;
    volatile uint64_t tx_bytes;
    volatile uint64_t dropped_pkts;
    volatile uint64_t dropped_bytes;
    volatile uint64_t processing_errors;
    volatile uint64_t metering_updates;
    volatile uint64_t dpi_classifications;
};

// Core interface for packet processing
class PacketProcessor {
public:
    virtual ~PacketProcessor() = default;
    
    // Process a burst of packets
    virtual void process_burst(struct rte_mbuf** pkts, uint16_t count) = 0;
    
    // Get statistics
    virtual PipelineStats get_stats() const = 0;
};

} // namespace dpdk
} // namespace vsg
