#include <cassert>
#include <iostream>
#include <memory>
#include "../include/ubb_meter.h"

using namespace vsg::metering;

void test_basic_counter() {
    std::cout << "Testing basic counter...\n";
    
    UBBMeter meter(1000);
    
    // Update counter
    meter.update_counter(1, 100, 1000, true);   // 1KB uplink
    meter.update_counter(1, 100, 2000, false);  // 2KB downlink
    
    // Verify counters
    auto counter = meter.get_counter(1, 100);
    assert(counter.uplink_bytes == 1000);
    assert(counter.downlink_bytes == 2000);
    assert(counter.uplink_packets == 1);
    assert(counter.downlink_packets == 1);
    
    std::cout << "  PASS: Basic counter test\n";
}

void test_multiple_services() {
    std::cout << "Testing multiple services...\n";
    
    UBBMeter meter(1000);
    
    // Track different services
    meter.update_counter(1, 10, 1000, true);   // Service 10
    meter.update_counter(1, 20, 2000, true);   // Service 20
    meter.update_counter(1, 30, 3000, true);   // Service 30
    
    auto counter_10 = meter.get_counter(1, 10);
    auto counter_20 = meter.get_counter(1, 20);
    auto counter_30 = meter.get_counter(1, 30);
    
    assert(counter_10.uplink_bytes == 1000);
    assert(counter_20.uplink_bytes == 2000);
    assert(counter_30.uplink_bytes == 3000);
    
    std::cout << "  PASS: Multiple services test\n";
}

void test_rollup() {
    std::cout << "Testing rollup...\n";
    
    UBBMeter meter(1000);
    
    meter.update_counter(1, 100, 5000, true);
    meter.update_counter(1, 100, 3000, false);
    
    auto rollup = meter.rollup(1, 100, 3600000000000LL);  // 1 hour
    
    assert(rollup.subscriber_id == 1);
    assert(rollup.service_id == 100);
    assert(rollup.uplink_bytes == 5000);
    assert(rollup.downlink_bytes == 3000);
    
    std::cout << "  PASS: Rollup test\n";
}

void test_threshold() {
    std::cout << "Testing threshold check...\n";
    
    UBBMeter meter(1000);
    
    meter.update_counter(1, 100, 500000000, true);  // 500MB
    
    bool exceeded = meter.check_threshold(1, 100, 1000000000);  // 1GB threshold
    assert(!exceeded);
    
    exceeded = meter.check_threshold(1, 100, 100000000);  // 100MB threshold
    assert(exceeded);
    
    std::cout << "  PASS: Threshold test\n";
}

void test_concurrent_updates() {
    std::cout << "Testing concurrent updates...\n";
    
    UBBMeter meter(1000);
    
    // Simulate concurrent updates (in reality would use threads)
    for (int i = 0; i < 100; ++i) {
        meter.update_counter(1, 100, 100, true);
        meter.update_counter(1, 100, 50, false);
    }
    
    auto counter = meter.get_counter(1, 100);
    assert(counter.uplink_bytes == 10000);
    assert(counter.downlink_bytes == 5000);
    assert(counter.uplink_packets == 100);
    assert(counter.downlink_packets == 100);
    
    std::cout << "  PASS: Concurrent updates test\n";
}

int main() {
    std::cout << "\n=== UBB Metering Unit Tests ===\n\n";
    
    test_basic_counter();
    test_multiple_services();
    test_rollup();
    test_threshold();
    test_concurrent_updates();
    
    std::cout << "\n=== All tests passed! ===\n\n";
    
    return 0;
}
