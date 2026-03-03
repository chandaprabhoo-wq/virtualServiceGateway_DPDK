#include <cassert>
#include <iostream>
#include <memory>
#include "../include/qoe_analytics.h"

using namespace vsg::analytics;

void test_flow_kpi() {
    std::cout << "Testing flow KPI...\n";
    
    QoECollector collector(1000);
    
    TelemetrySample sample;
    sample.flow_id = 1;
    sample.subscriber_id = 100;
    sample.service_id = 10;
    sample.packet_size = 1500;
    sample.latency_us = 50;
    sample.packet_lost = false;
    
    collector.record_sample(sample);
    
    auto kpi = collector.get_flow_kpi(1);
    assert(kpi.flow_id == 1);
    assert(kpi.packet_count > 0);
    
    std::cout << "  PASS: Flow KPI test\n";
}

void test_multiple_flows() {
    std::cout << "Testing multiple flows...\n";
    
    QoECollector collector(1000);
    
    for (uint32_t flow_id = 0; flow_id < 10; ++flow_id) {
        for (int i = 0; i < 100; ++i) {
            TelemetrySample sample;
            sample.flow_id = flow_id;
            sample.subscriber_id = 100 + flow_id;
            sample.service_id = 10;
            sample.packet_size = 1500;
            sample.latency_us = 30 + (i % 20);
            sample.packet_lost = (i % 100 == 0);  // 1% loss
            
            collector.record_sample(sample);
        }
    }
    
    auto kpis = collector.get_all_flow_kpis();
    assert(kpis.size() > 0);
    
    std::cout << "  PASS: Multiple flows test\n";
}

void test_service_kpi() {
    std::cout << "Testing service KPI...\n";
    
    QoECollector collector(1000);
    
    // Record samples for a service
    for (int i = 0; i < 1000; ++i) {
        TelemetrySample sample;
        sample.flow_id = i % 10;
        sample.subscriber_id = 100 + (i % 5);
        sample.service_id = 20;
        sample.packet_size = 1500;
        sample.latency_us = 40;
        sample.packet_lost = false;
        
        collector.record_sample(sample);
    }
    
    auto service_kpi = collector.get_service_kpi(20, 60000000000LL);  // 1 minute window
    assert(service_kpi.service_id == 20);
    assert(service_kpi.total_flows > 0);
    
    std::cout << "  PASS: Service KPI test\n";
}

void test_loss_tracking() {
    std::cout << "Testing packet loss tracking...\n";
    
    QoECollector collector(1000);
    
    // Record 100 packets with 10% loss
    for (int i = 0; i < 100; ++i) {
        TelemetrySample sample;
        sample.flow_id = 1;
        sample.subscriber_id = 100;
        sample.service_id = 10;
        sample.packet_size = 1500;
        sample.latency_us = 50;
        sample.packet_lost = (i % 10 == 0);  // 10% loss
        
        collector.record_sample(sample);
    }
    
    auto kpi = collector.get_flow_kpi(1);
    assert(kpi.packet_count >= 100);
    
    std::cout << "  PASS: Loss tracking test\n";
}

void test_high_cardinality_aggregation() {
    std::cout << "Testing high-cardinality aggregation...\n";
    
    QoECollector collector(10000);
    
    // Create a large number of flows with varying characteristics
    for (uint32_t flow_id = 0; flow_id < 5000; ++flow_id) {
        TelemetrySample sample;
        sample.flow_id = flow_id;
        sample.subscriber_id = 100 + (flow_id % 1000);
        sample.service_id = 10 + (flow_id % 20);
        sample.packet_size = 1500;
        sample.latency_us = 20 + (flow_id % 100);
        sample.packet_lost = false;
        
        collector.record_sample(sample);
    }
    
    // Safe aggregation with sampling
    auto samples = collector.get_aggregated_telemetry(1000);
    assert(samples.size() <= 1000);
    
    std::cout << "  PASS: High-cardinality aggregation test\n";
}

int main() {
    std::cout << "\n=== QoE Analytics Unit Tests ===\n\n";
    
    test_flow_kpi();
    test_multiple_flows();
    test_service_kpi();
    test_loss_tracking();
    test_high_cardinality_aggregation();
    
    std::cout << "\n=== All tests passed! ===\n\n";
    
    return 0;
}
