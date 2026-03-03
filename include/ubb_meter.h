#pragma once

#include <cstdint>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <vector>
#include <chrono>

namespace vsg {
namespace metering {

// Per-subscriber, per-service byte counters with rollup support
struct UsageCounter {
    uint32_t subscriber_id;
    uint16_t service_id;
    
    // Directional counters (lock-free atomic updates)
    std::atomic<uint64_t> uplink_bytes{0};
    std::atomic<uint64_t> downlink_bytes{0};
    std::atomic<uint64_t> uplink_packets{0};
    std::atomic<uint64_t> downlink_packets{0};
    
    // Lifecycle tracking
    int64_t created_time_ns;
    int64_t last_update_ns;
    bool active{true};
    
    // Threshold counters for alerts
    std::atomic<uint32_t> threshold_breaches{0};
};

// Rollup aggregation unit
struct UsageRollup {
    uint32_t subscriber_id;
    uint16_t service_id;
    uint64_t period_start_ns;
    uint64_t period_end_ns;
    uint64_t uplink_bytes;
    uint64_t downlink_bytes;
    uint64_t uplink_packets;
    uint64_t downlink_packets;
    uint32_t sample_count;      // For averaging
};

// UBB (Usage-Based Billing) Meter
class UBBMeter {
public:
    UBBMeter(uint32_t max_subscribers = 1000000);
    ~UBBMeter();
    
    // Update counters (fast path)
    void update_counter(uint32_t subscriber_id, uint16_t service_id, 
                       uint32_t bytes, bool uplink);
    
    // Get counter snapshot
    UsageCounter get_counter(uint32_t subscriber_id, uint16_t service_id);
    
    // Trigger rollup for a subscriber/service
    UsageRollup rollup(uint32_t subscriber_id, uint16_t service_id, 
                       uint64_t rollup_period_ns);
    
    // Batch rollup for all subscribers
    std::vector<UsageRollup> batch_rollup(uint64_t rollup_period_ns);
    
    // Reset counter
    void reset_counter(uint32_t subscriber_id, uint16_t service_id);
    
    // Check threshold (for billing alerts)
    bool check_threshold(uint32_t subscriber_id, uint16_t service_id, 
                        uint64_t threshold_bytes);
    
    // Get total usage across all subscribers
    uint64_t get_total_usage();

private:
    struct CounterKey {
        uint64_t key;  // Packed (subscriber_id << 16) | service_id
        
        CounterKey(uint32_t sub_id, uint16_t svc_id) {
            key = ((uint64_t)sub_id << 16) | svc_id;
        }
        
        bool operator==(const CounterKey& other) const {
            return key == other.key;
        }
    };
    
    struct CounterKeyHash {
        size_t operator()(const CounterKey& k) const {
            return std::hash<uint64_t>()(k.key);
        }
    };
    
    std::unordered_map<CounterKey, std::shared_ptr<UsageCounter>, CounterKeyHash> counters_;
    uint32_t max_subscribers_;
};

} // namespace metering
} // namespace vsg
