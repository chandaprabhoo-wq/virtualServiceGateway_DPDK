#include "batch_processor.h"
#include <rte_mbuf.h>
#include <rte_prefetch.h>
#include <cstring>

namespace vsg {
namespace dpdk {

BatchProcessor::BatchProcessor() {}

BatchProcessor::~BatchProcessor() {}

void BatchProcessor::prefetch_batch(struct rte_mbuf** pkts, uint16_t count) {
    // Prefetch packet data for cache optimization
    for (uint16_t i = 0; i < count && i < 4; ++i) {
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void*));
    }
}

void BatchProcessor::process_batch(struct rte_mbuf** pkts, uint16_t count) {
    // Prefetch first batch
    prefetch_batch(pkts, count);
    
    // Process packets with optimal cache behavior
    for (uint16_t i = 0; i < count; ++i) {
        process_single(pkts[i]);
        
        // Prefetch next packet ahead of processing
        if (i + 4 < count) {
            rte_prefetch0(rte_pktmbuf_mtod(pkts[i + 4], void*));
        }
    }
}

void BatchProcessor::process_single(struct rte_mbuf* pkt) {
    if (!pkt) return;
    
    // Extract basic packet info
    uint16_t pkt_len = rte_pktmbuf_pkt_len(pkt);
    uint8_t* data = rte_pktmbuf_mtod(pkt, uint8_t*);
    
    // Store metadata in mbuf userdata area for downstream processing
    PacketMetadata* meta = new (pkt->userdata) PacketMetadata();
    meta->packet_bytes = pkt_len;
    meta->rx_timestamp_ns = rte_rdtsc_precise();
}

void BatchProcessor::extract_metadata(struct rte_mbuf* pkt, PacketMetadata* meta) {
    if (!pkt || !meta) return;
    
    PacketMetadata* stored = (PacketMetadata*)pkt->userdata;
    if (stored) {
        *meta = *stored;
    }
}

void BatchProcessor::attach_metadata(struct rte_mbuf* pkt, const PacketMetadata* meta) {
    if (!pkt || !meta) return;
    
    PacketMetadata* stored = new (pkt->userdata) PacketMetadata(*meta);
}

} // namespace dpdk
} // namespace vsg
