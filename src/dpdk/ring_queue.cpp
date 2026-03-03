#include "ring_queue.h"
#include <rte_ring.h>
#include <iostream>

namespace vsg {
namespace dpdk {

RingQueue::RingQueue(const std::string& name, uint32_t size, int numa_node, bool sp, bool sc)
    : name_(name), size_(size) {
    
    unsigned int flags = 0;
    if (sp) flags |= RING_F_SP_ENQ;  // Single producer
    if (sc) flags |= RING_F_SC_DEQ;  // Single consumer
    
    ring_ = rte_ring_create(name.c_str(), size, numa_node, flags);
    
    if (!ring_) {
        std::cerr << "Failed to create ring queue: " << name << "\n";
    }
}

RingQueue::~RingQueue() {
    if (ring_) {
        rte_ring_free(ring_);
    }
}

int RingQueue::enqueue(struct rte_mbuf* obj) {
    if (!ring_) return -1;
    return rte_ring_sp_enqueue(ring_, obj);
}

int RingQueue::enqueue_burst(struct rte_mbuf** objs, uint32_t count) {
    if (!ring_) return 0;
    return rte_ring_sp_enqueue_burst(ring_, (void**)objs, count, nullptr);
}

struct rte_mbuf* RingQueue::dequeue() {
    if (!ring_) return nullptr;
    struct rte_mbuf* obj = nullptr;
    if (rte_ring_sc_dequeue(ring_, (void**)&obj) == 0) {
        return obj;
    }
    return nullptr;
}

uint32_t RingQueue::dequeue_burst(struct rte_mbuf** objs, uint32_t max_count) {
    if (!ring_) return 0;
    return rte_ring_sc_dequeue_burst(ring_, (void**)objs, max_count, nullptr);
}

bool RingQueue::empty() const {
    if (!ring_) return true;
    return rte_ring_empty(ring_) == 1;
}

uint32_t RingQueue::available_count() const {
    if (!ring_) return 0;
    return rte_ring_free_count(ring_);
}

} // namespace dpdk
} // namespace vsg
