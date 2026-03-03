#include <iostream>
#include <memory>
#include <signal.h>
#include "pmd_driver.h"
#include "mempool_manager.h"
#include "worker_pipeline.h"
#include "ubb_meter.h"
#include "qoe_analytics.h"
#include "dpi_classifier.h"

using namespace vsg;

static volatile bool running = true;

void signal_handler(int sig) {
    std::cout << "Received signal " << sig << ", shutting down gracefully...\n";
    running = false;
}

int main(int argc, char** argv) {
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "vSG Gateway Starting\n";
    std::cout << "====================\n\n";
    
    // Initialize DPDK
    std::cout << "Initializing DPDK PMD...\n";
    auto pmd = std::make_unique<dpdk::PMDDriver>();
    if (!pmd->init_eal(argc, argv)) {
        std::cerr << "Failed to initialize DPDK EAL\n";
        return 1;
    }
    
    // Initialize mempools
    std::cout << "Initializing mempools...\n";
    auto mempool_mgr = std::make_unique<dpdk::MempoolManager>();
    if (!mempool_mgr->init_mempool(0, dpdk::NUM_MBUFS, dpdk::MBUF_CACHE_SIZE)) {
        std::cerr << "Failed to initialize mempool\n";
        return 1;
    }
    
    // Initialize metering
    std::cout << "Initializing UBB metering...\n";
    auto meter = std::make_unique<metering::UBBMeter>();
    
    // Initialize analytics
    std::cout << "Initializing QoE analytics...\n";
    auto qoe = std::make_unique<analytics::QoECollector>();
    
    // Initialize DPI
    std::cout << "Initializing DPI classifier...\n";
    dpi::DPIGuardrails guardrails;
    guardrails.timeout_us = 100;
    guardrails.sample_rate = 10;
    guardrails.degradation_enabled = true;
    auto dpi_classifier = std::make_unique<dpi::DPIClassifier>(guardrails);
    
    std::cout << "\nvSG Gateway initialized successfully\n";
    std::cout << "Waiting for packets...\n";
    std::cout << "(Press Ctrl+C to shutdown)\n\n";
    
    // Main loop - in a real implementation, this would drive the worker pipelines
    while (running) {
        // Sleep briefly to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Periodically print statistics
        static int counter = 0;
        if (++counter % 10 == 0) {
            std::cout << "Gateway running - Total usage: " 
                     << meter->get_total_usage() / (1024*1024) << " MB\n";
        }
    }
    
    std::cout << "\nShutting down vSG Gateway...\n";
    
    return 0;
}
