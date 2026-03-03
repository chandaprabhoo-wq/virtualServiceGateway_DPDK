#pragma once

#include <cstdint>
#include <vector>

namespace vsg {
namespace dpdk {

// PMD (Poll Mode Driver) abstraction for port management
class PMDDriver {
public:
    PMDDriver();
    ~PMDDriver();
    
    // Initialize EAL (Environment Abstraction Layer)
    bool init_eal(int argc, char** argv);
    
    // Probe and initialize ports
    bool init_ports(const std::vector<PortConfig>& configs);
    
    // Start all ports
    bool start_ports();
    
    // Stop all ports
    void stop_ports();
    
    // Get port count
    uint16_t get_port_count() const;
    
    // Get port info
    const PortConfig& get_port_config(uint16_t port_id) const;
    
    // RX a burst from a queue
    uint16_t rx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf** pkts, uint16_t nb_pkts);
    
    // TX a burst to a queue
    uint16_t tx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf** pkts, uint16_t nb_pkts);
    
    // Get port statistics
    void get_port_stats(uint16_t port_id, struct rte_eth_stats& stats);

private:
    std::vector<PortConfig> port_configs_;
    bool eal_initialized_{false};
};

} // namespace dpdk
} // namespace vsg
