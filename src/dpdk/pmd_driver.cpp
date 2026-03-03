#include "pmd_driver.h"
#include <rte_ethdev.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <iostream>

namespace vsg {
namespace dpdk {

PMDDriver::PMDDriver() {}

PMDDriver::~PMDDriver() {
    stop_ports();
}

bool PMDDriver::init_eal(int argc, char** argv) {
    if (eal_initialized_) return true;
    
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        std::cerr << "Failed to initialize DPDK EAL\n";
        return false;
    }
    
    eal_initialized_ = true;
    return true;
}

bool PMDDriver::init_ports(const std::vector<PortConfig>& configs) {
    if (!eal_initialized_) {
        std::cerr << "EAL not initialized\n";
        return false;
    }
    
    port_configs_ = configs;
    uint16_t nb_ports = rte_eth_dev_count_avail();
    
    if (nb_ports == 0) {
        std::cerr << "No Ethernet ports found\n";
        return false;
    }
    
    for (const auto& cfg : configs) {
        if (cfg.port_id >= nb_ports) {
            std::cerr << "Port " << cfg.port_id << " not available\n";
            return false;
        }
        
        struct rte_eth_dev_info dev_info;
        rte_eth_dev_info_get(cfg.port_id, &dev_info);
        
        struct rte_eth_conf port_conf = {};
        port_conf.rxmode.mtu = cfg.mtu;
        port_conf.rxmode.offloads = RTE_ETH_RX_OFFLOAD_CHECKSUM;
        port_conf.txmode.offloads = RTE_ETH_TX_OFFLOAD_CHECKSUM;
        
        if (cfg.jumbo_enabled) {
            port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_JUMBO_FRAME;
        }
        
        if (rte_eth_dev_configure(cfg.port_id, cfg.rxq_count, cfg.txq_count, &port_conf) < 0) {
            std::cerr << "Failed to configure port " << cfg.port_id << "\n";
            return false;
        }
        
        // Setup RX queues
        for (uint16_t q = 0; q < cfg.rxq_count; q++) {
            if (rte_eth_rx_queue_setup(cfg.port_id, q, 512, 
                                      rte_eth_dev_socket_id(cfg.port_id),
                                      NULL, NULL) < 0) {
                std::cerr << "Failed to setup RX queue " << q << " on port " << cfg.port_id << "\n";
                return false;
            }
        }
        
        // Setup TX queues
        for (uint16_t q = 0; q < cfg.txq_count; q++) {
            if (rte_eth_tx_queue_setup(cfg.port_id, q, 512,
                                      rte_eth_dev_socket_id(cfg.port_id), NULL) < 0) {
                std::cerr << "Failed to setup TX queue " << q << " on port " << cfg.port_id << "\n";
                return false;
            }
        }
    }
    
    return true;
}

bool PMDDriver::start_ports() {
    for (const auto& cfg : port_configs_) {
        if (rte_eth_dev_start(cfg.port_id) < 0) {
            std::cerr << "Failed to start port " << cfg.port_id << "\n";
            return false;
        }
        
        if (cfg.promiscuous) {
            rte_eth_promiscuous_enable(cfg.port_id);
        }
    }
    
    return true;
}

void PMDDriver::stop_ports() {
    for (const auto& cfg : port_configs_) {
        rte_eth_dev_stop(cfg.port_id);
        rte_eth_dev_close(cfg.port_id);
    }
}

uint16_t PMDDriver::get_port_count() const {
    return port_configs_.size();
}

const PortConfig& PMDDriver::get_port_config(uint16_t port_id) const {
    for (const auto& cfg : port_configs_) {
        if (cfg.port_id == port_id) {
            return cfg;
        }
    }
    throw std::runtime_error("Port not found");
}

uint16_t PMDDriver::rx_burst(uint16_t port_id, uint16_t queue_id, 
                             struct rte_mbuf** pkts, uint16_t nb_pkts) {
    return rte_eth_rx_burst(port_id, queue_id, pkts, nb_pkts);
}

uint16_t PMDDriver::tx_burst(uint16_t port_id, uint16_t queue_id,
                             struct rte_mbuf** pkts, uint16_t nb_pkts) {
    return rte_eth_tx_burst(port_id, queue_id, pkts, nb_pkts);
}

void PMDDriver::get_port_stats(uint16_t port_id, struct rte_eth_stats& stats) {
    rte_eth_stats_get(port_id, &stats);
}

} // namespace dpdk
} // namespace vsg
