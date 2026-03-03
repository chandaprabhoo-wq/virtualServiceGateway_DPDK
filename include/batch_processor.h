#pragma once

#include <cstdint>
#include <rte_mbuf.h>

namespace vsg {
namespace dpdk {

// High-performance batch packet processor for cache-friendly operations
class BatchProcessor {
public:
    BatchProcessor();
    ~BatchProcessor();
    
    // Process a batch of packets with optimal cache behavior
    void process_batch(struct rte_mbuf** pkts, uint16_t count);
    
    // Prefetch next batch for cache optimization
    static void prefetch_batch(struct rte_mbuf** pkts, uint16_t count);
    
    // Extract metadata from packet header
    static void extract_metadata(struct rte_mbuf* pkt, PacketMetadata* meta);
    
    // Attach metadata to mbuf
    static void attach_metadata(struct rte_mbuf* pkt, const PacketMetadata* meta);

private:
    // Per-packet processing without branches
    void process_single(struct rte_mbuf* pkt);
};

} // namespace dpdk
} // namespace vsg
