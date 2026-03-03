#include "ubb_meter.h"
#include <algorithm>
#include <chrono>

namespace vsg {
namespace metering {

UBBMeter::UBBMeter(uint32_t max_subscribers) : max_subscribers_(max_subscribers) {}

UBBMeter::~UBBMeter() {}

void UBBMeter::update_counter(uint32_t subscriber_id, uint16_t service_id, 
                              uint32_t bytes, bool uplink) {
    CounterKey key(subscriber_id, service_id);
    
    auto it = counters_.find(key);
    if (it == counters_.end()) {
        // Create new counter
        auto counter = std::make_shared<UsageCounter>();
        counter->subscriber_id = subscriber_id;
        counter->service_id = service_id;
        counter->created_time_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        counters_[key] = counter;
        it = counters_.find(key);
    }
    
    auto& counter = it->second;
    counter->last_update_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    if (uplink) {
        counter->uplink_bytes += bytes;
        counter->uplink_packets += 1;
    } else {
        counter->downlink_bytes += bytes;
        counter->downlink_packets += 1;
    }
}

UsageCounter UBBMeter::get_counter(uint32_t subscriber_id, uint16_t service_id) {
    CounterKey key(subscriber_id, service_id);
    
    auto it = counters_.find(key);
    if (it == counters_.end()) {
        UsageCounter empty;
        empty.subscriber_id = subscriber_id;
        empty.service_id = service_id;
        return empty;
    }
    
    return *it->second;
}

UsageRollup UBBMeter::rollup(uint32_t subscriber_id, uint16_t service_id, 
                             uint64_t rollup_period_ns) {
    CounterKey key(subscriber_id, service_id);
    
    UsageRollup rollup;
    rollup.subscriber_id = subscriber_id;
    rollup.service_id = service_id;
    rollup.period_start_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count() - rollup_period_ns;
    rollup.period_end_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    auto it = counters_.find(key);
    if (it != counters_.end()) {
        auto& counter = it->second;
        rollup.uplink_bytes = counter->uplink_bytes.load();
        rollup.downlink_bytes = counter->downlink_bytes.load();
        rollup.uplink_packets = counter->uplink_packets.load();
        rollup.downlink_packets = counter->downlink_packets.load();
        rollup.sample_count = 1;
    }
    
    return rollup;
}

std::vector<UsageRollup> UBBMeter::batch_rollup(uint64_t rollup_period_ns) {
    std::vector<UsageRollup> rollups;
    
    for (auto& pair : counters_) {
        auto& counter = pair.second;
        UsageRollup rollup;
        rollup.subscriber_id = counter->subscriber_id;
        rollup.service_id = counter->service_id;
        rollup.period_start_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count() - rollup_period_ns;
        rollup.period_end_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        rollup.uplink_bytes = counter->uplink_bytes.load();
        rollup.downlink_bytes = counter->downlink_bytes.load();
        rollup.uplink_packets = counter->uplink_packets.load();
        rollup.downlink_packets = counter->downlink_packets.load();
        rollup.sample_count = 1;
        
        rollups.push_back(rollup);
    }
    
    return rollups;
}

void UBBMeter::reset_counter(uint32_t subscriber_id, uint16_t service_id) {
    CounterKey key(subscriber_id, service_id);
    counters_.erase(key);
}

bool UBBMeter::check_threshold(uint32_t subscriber_id, uint16_t service_id, 
                               uint64_t threshold_bytes) {
    CounterKey key(subscriber_id, service_id);
    
    auto it = counters_.find(key);
    if (it == counters_.end()) return false;
    
    auto& counter = it->second;
    uint64_t total = counter->uplink_bytes.load() + counter->downlink_bytes.load();
    
    if (total > threshold_bytes) {
        counter->threshold_breaches += 1;
        return true;
    }
    return false;
}

uint64_t UBBMeter::get_total_usage() {
    uint64_t total = 0;
    
    for (auto& pair : counters_) {
        auto& counter = pair.second;
        total += counter->uplink_bytes.load();
        total += counter->downlink_bytes.load();
    }
    
    return total;
}

} // namespace metering
} // namespace vsg
