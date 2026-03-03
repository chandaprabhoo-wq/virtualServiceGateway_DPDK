#pragma once

#include <cstdint>
#include <vector>
#include <rte_mempool.h>
#include <rte_ring.h>

namespace vsg {
namespace dpdk {

// Manages DPDK memory pools with per-core caching
class MempoolManager {
public:
    MempoolManager();
    ~MempoolManager();
    
    // Initialize mempool for a NUMA node
    bool init_mempool(uint32_t numa_node, uint32_t num_bufs, uint32_t cache_size);
    
    // Get mempool for a socket
    struct rte_mempool* get_mempool(uint32_t numa_node);
    
    // Allocate a packet buffer
    struct rte_mbuf* alloc_pkt(uint32_t numa_node, uint32_t size = 0);
    
    // Free packet buffer
    void free_pkt(struct rte_mbuf* m);
    
    // Get statistics
    void get_stats(uint32_t numa_node, uint32_t& free_count, uint32_t& in_use_count);

private:
    std::vector<struct rte_mempool*> mempools_;
    uint32_t cache_size_;
};

} // namespace dpdk
} // namespace vsg
