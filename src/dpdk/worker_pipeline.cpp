#include "worker_pipeline.h"
#include <rte_lcore.h>
#include <rte_ethdev.h>
#include <iostream>

namespace vsg {
namespace dpdk {

WorkerPipeline::WorkerPipeline(const WorkerConfig& config) : config_(config) {
    stats_.rx_pkts = 0;
    stats_.rx_bytes = 0;
    stats_.tx_pkts = 0;
    stats_.tx_bytes = 0;
    stats_.dropped_pkts = 0;
    stats_.dropped_bytes = 0;
    stats_.processing_errors = 0;
    stats_.metering_updates = 0;
    stats_.dpi_classifications = 0;
}

WorkerPipeline::~WorkerPipeline() {
    stop();
}

bool WorkerPipeline::start() {
    if (running_) return true;
    
    running_ = true;
    worker_thread_ = std::thread(&WorkerPipeline::worker_loop, this);
    
    return true;
}

void WorkerPipeline::stop() {
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

PipelineStats WorkerPipeline::get_stats() const {
    return stats_;
}

void WorkerPipeline::add_processor(std::shared_ptr<PacketProcessor> processor) {
    processors_.push_back(processor);
}

void WorkerPipeline::worker_loop() {
    struct rte_mbuf* pkts[PKT_BURST_SIZE];
    
    while (running_) {
        // RX from all configured RX queues
        for (uint16_t queue : config_.rx_queues) {
            uint16_t nb_rx = rx_burst(pkts);
            
            if (nb_rx == 0) continue;
            
            stats_.rx_pkts += nb_rx;
            
            // Calculate total bytes
            for (uint16_t i = 0; i < nb_rx; ++i) {
                stats_.rx_bytes += rte_pktmbuf_pkt_len(pkts[i]);
            }
            
            // Process through all pipeline stages
            for (auto& processor : processors_) {
                processor->process_burst(pkts, nb_rx);
            }
            
            // TX to output queues
            tx_burst(config_.worker_id, 0, pkts, nb_rx);
            
            stats_.tx_pkts += nb_rx;
        }
        
        // Prevent busy-waiting (brief sleep if no packets)
        rte_pause();
    }
}

uint16_t WorkerPipeline::rx_burst(struct rte_mbuf** pkts) {
    // Simple round-robin RX from all queues
    static uint16_t queue_idx = 0;
    
    if (config_.rx_queues.empty()) return 0;
    
    uint16_t queue = config_.rx_queues[queue_idx];
    queue_idx = (queue_idx + 1) % config_.rx_queues.size();
    
    // This is a placeholder; actual implementation would use DPDK port IDs
    return 0;
}

void WorkerPipeline::tx_burst(uint16_t port, uint16_t queue, struct rte_mbuf** pkts, uint16_t count) {
    // Placeholder for TX; actual implementation uses rte_eth_tx_burst
    for (uint16_t i = 0; i < count; ++i) {
        rte_pktmbuf_free(pkts[i]);
    }
}

} // namespace dpdk
} // namespace vsg
