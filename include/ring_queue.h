#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <rte_ring.h>
#include "dpdk_types.h"

namespace vsg {
namespace dpdk {

// Lock-free ring queue wrapper for inter-worker communication
class RingQueue {
public:
    RingQueue(const std::string& name, uint32_t size, int numa_node, bool sp, bool sc);
    ~RingQueue();
    
    // Single producer enqueue
    int enqueue(struct rte_mbuf* obj);
    
    // Burst enqueue
    int enqueue_burst(struct rte_mbuf** objs, uint32_t count);
    
    // Single consumer dequeue
    struct rte_mbuf* dequeue();
    
    // Burst dequeue
    uint32_t dequeue_burst(struct rte_mbuf** objs, uint32_t max_count);
    
    // Check if empty
    bool empty() const;
    
    // Check available space
    uint32_t available_count() const;
    
    // Get underlying ring for advanced operations
    struct rte_ring* get_ring() { return ring_; }

private:
    struct rte_ring* ring_;
    std::string name_;
    uint32_t size_;
};

} // namespace dpdk
} // namespace vsg
