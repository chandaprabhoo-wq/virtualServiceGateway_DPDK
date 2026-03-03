#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include "dpdk_types.h"

namespace vsg {
namespace dpdk {

// Multi-stage worker pipeline with backpressure handling
class WorkerPipeline {
public:
    WorkerPipeline(const WorkerConfig& config);
    ~WorkerPipeline();
    
    // Start the worker thread
    bool start();
    
    // Stop the worker thread gracefully
    void stop();
    
    // Get current statistics
    PipelineStats get_stats() const;
    
    // Queue a packet processor stage
    void add_processor(std::shared_ptr<PacketProcessor> processor);
    
    // Get worker ID
    uint32_t get_worker_id() const { return config_.worker_id; }

private:
    WorkerConfig config_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    PipelineStats stats_;
    std::vector<std::shared_ptr<PacketProcessor>> processors_;
    
    // Worker main loop
    void worker_loop();
    
    // RX from configured queues
    uint16_t rx_burst(struct rte_mbuf** pkts);
    
    // TX to configured queues
    void tx_burst(uint16_t port, uint16_t queue, struct rte_mbuf** pkts, uint16_t count);
};

} // namespace dpdk
} // namespace vsg
