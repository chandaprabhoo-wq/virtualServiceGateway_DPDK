#include "mempool_manager.h"
#include <rte_mempool.h>
#include <iostream>
#include <cstring>

namespace vsg {
namespace dpdk {

MempoolManager::MempoolManager() : cache_size_(0) {}

MempoolManager::~MempoolManager() {
    // Mempools are managed by DPDK, no explicit cleanup needed
}

bool MempoolManager::init_mempool(uint32_t numa_node, uint32_t num_bufs, uint32_t cache_size) {
    cache_size_ = cache_size;
    
    char pool_name[RTE_MEMPOOL_NAMESIZE];
    snprintf(pool_name, sizeof(pool_name), "mbuf_pool_%u", numa_node);
    
    struct rte_mempool* mp = rte_pktmbuf_pool_create(
        pool_name,
        num_bufs,
        cache_size,
        0,  // priv_size
        RTE_MBUF_DEFAULT_BUF_SIZE,
        numa_node
    );
    
    if (!mp) {
        std::cerr << "Failed to create mempool for NUMA node " << numa_node << "\n";
        return false;
    }
    
    // Ensure mempools_ vector is large enough
    if (mempools_.size() <= numa_node) {
        mempools_.resize(numa_node + 1, nullptr);
    }
    
    mempools_[numa_node] = mp;
    return true;
}

struct rte_mempool* MempoolManager::get_mempool(uint32_t numa_node) {
    if (numa_node >= mempools_.size()) {
        return nullptr;
    }
    return mempools_[numa_node];
}

struct rte_mbuf* MempoolManager::alloc_pkt(uint32_t numa_node, uint32_t size) {
    struct rte_mempool* mp = get_mempool(numa_node);
    if (!mp) return nullptr;
    
    struct rte_mbuf* m = rte_pktmbuf_alloc(mp);
    if (m && size > 0) {
        m->data_len = size;
        m->pkt_len = size;
    }
    return m;
}

void MempoolManager::free_pkt(struct rte_mbuf* m) {
    if (m) {
        rte_pktmbuf_free(m);
    }
}

void MempoolManager::get_stats(uint32_t numa_node, uint32_t& free_count, uint32_t& in_use_count) {
    struct rte_mempool* mp = get_mempool(numa_node);
    if (!mp) {
        free_count = 0;
        in_use_count = 0;
        return;
    }
    
    free_count = rte_mempool_avail_count(mp);
    in_use_count = rte_mempool_in_use_count(mp);
}

} // namespace dpdk
} // namespace vsg
