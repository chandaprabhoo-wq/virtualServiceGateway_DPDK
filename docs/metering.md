# UBB Metering Implementation Guide

## Overview

**UBB (Usage-Based Billing)**: Accurate per-subscriber and per-service byte accounting for telecom charging.

Key requirements:
- **Accuracy**: ± 0.1% error acceptable for billing
- **Speed**: Lock-free updates in fast path
- **Scalability**: Support 1M+ concurrent subscribers
- **Reconciliation**: Detect and correct billing errors
- **Audit trail**: Complete history for disputes

## Architecture

### Counter Data Structure

```cpp
struct UsageCounter {
    uint32_t subscriber_id;
    uint16_t service_id;
    
    std::atomic<uint64_t> uplink_bytes;      // Fast path
    std::atomic<uint64_t> downlink_bytes;    // Fast path
    std::atomic<uint64_t> uplink_packets;
    std::atomic<uint64_t> downlink_packets;
    
    int64_t created_time_ns;
    int64_t last_update_ns;
    bool active;
};
```

**Size**: ~100 bytes per counter
**Typical**: 1M subscribers × 1K services = 100GB (if tracked all)

### Storage Strategy

**Sparse Storage**: Only track active (subscriber, service) pairs

```cpp
std::unordered_map<CounterKey, std::shared_ptr<UsageCounter>> counters_;
```

**Memory**: 1M active pairs ≈ 100 MB (good!)
**Lookup**: O(1) with good hash function

### CounterKey Packing

```cpp
struct CounterKey {
    uint64_t key;  // Packed: (subscriber_id << 16) | service_id
};
```

**Rationale**:
- Single hash lookup instead of tuple
- Faster hash function
- Smaller memory footprint

## Fast Path Update

### Single Packet Update

```cpp
void UBBMeter::update_counter(uint32_t subscriber_id, uint16_t service_id,
                              uint32_t bytes, bool uplink) {
    CounterKey key(subscriber_id, service_id);
    
    // Lookup (can fail if not tracking this pair yet)
    auto it = counters_.find(key);
    
    if (it == counters_.end()) {
        // Create if first packet
        auto counter = std::make_shared<UsageCounter>();
        // ... initialize ...
        counters_[key] = counter;
        it = counters_.find(key);
    }
    
    // Update (atomic, no locks)
    auto& counter = it->second;
    if (uplink) {
        counter->uplink_bytes += bytes;
        counter->uplink_packets += 1;
    } else {
        counter->downlink_bytes += bytes;
        counter->downlink_packets += 1;
    }
}
```

**Performance**:
- Hash lookup: 20-50 ns
- Atomic add: 1-2 ns
- Total: ~50-100 ns per packet

**Throughput**: At 100 ns/packet = 10M packets/sec (100 Mbps at 1.5KB packets)

### Batch Updates (Optimized)

```cpp
void UBBMeter::update_counters_batch(const PacketBatch& batch) {
    // Pre-fetch hash buckets
    for (auto& pkt : batch) {
        pkt.key = hash(pkt.subscriber_id, pkt.service_id);
    }
    
    // Update in hash order (better cache locality)
    std::sort(batch.begin(), batch.end(),
        [](const auto& a, const auto& b) { return a.key < b.key; });
    
    for (auto& pkt : batch) {
        // Cached hash bucket
        auto it = counters_.find(pkt.key);
        if (it != counters_.end()) {
            it->second->uplink_bytes += pkt.bytes;
        }
    }
}
```

**Improvement**: 2-3x better cache locality

## Rollup & Aggregation

### Period-Based Rollup

```cpp
UsageRollup UBBMeter::rollup(uint32_t subscriber_id, uint16_t service_id,
                             uint64_t rollup_period_ns) {
    UsageRollup rollup;
    rollup.period_start_ns = now() - rollup_period_ns;
    rollup.period_end_ns = now();
    
    auto counter = get_counter(subscriber_id, service_id);
    rollup.uplink_bytes = counter->uplink_bytes.load();
    rollup.downlink_bytes = counter->downlink_bytes.load();
    
    return rollup;
}
```

**Lifetime**: Typical 5-minute, 1-hour, daily rollups

### Batch Rollup

```cpp
std::vector<UsageRollup> UBBMeter::batch_rollup(uint64_t rollup_period_ns) {
    std::vector<UsageRollup> rollups;
    
    for (auto& [key, counter] : counters_) {
        // Snapshot counters (atomic read)
        UsageRollup rollup;
        rollup.uplink_bytes = counter->uplink_bytes.load();
        rollup.downlink_bytes = counter->downlink_bytes.load();
        // ...
        rollups.push_back(rollup);
    }
    
    return rollups;
}
```

**Frequency**: Every 5 minutes (typical)
**Output**: Write to time-series DB or file (async)

### Rollup Storage Format

```json
{
    "timestamp": "2025-02-23T10:05:00Z",
    "period_seconds": 300,
    "subscribers": [
        {
            "subscriber_id": 12345,
            "service_id": 100,
            "uplink_bytes": 1048576,
            "downlink_bytes": 5242880,
            "uplink_packets": 700,
            "downlink_packets": 3500
        }
    ]
}
```

**Retention**: Keep rollups for:
- 5-min: 7 days
- 1-hour: 90 days
- Daily: 3 years (regulatory)

## Reconciliation

### Sources of Discrepancies

1. **Packet loss**: Some packets not accounted
2. **Clock skew**: Timestamp differences between systems
3. **Double-billing**: Same packet counted twice
4. **Missing data**: System failure/restart

### Reconciliation Strategy

**External reference**: AAA (Authentication, Authorization, Accounting) system

```
vSG Gateway         AAA System
├─ Records usage    ├─ Records events
├─ Aggregates       ├─ Calculates expected
└─ Rollups          └─ Stored charges
   ↓                   ↓
   └─── Compare ───────┘
        Check within ±1% tolerance
```

### Implementation

```cpp
class ReconciliationEngine {
public:
    struct Discrepancy {
        uint32_t subscriber_id;
        uint16_t service_id;
        int64_t delta_bytes;  // vSG - AAA
        double error_percent;
        std::string action;  // "investigate", "credit", "adjust"
    };
    
    std::vector<Discrepancy> reconcile(
        const std::vector<UsageRollup>& vsg_rollups,
        const std::vector<ChargeRecord>& aaa_records) {
        
        std::vector<Discrepancy> discrepancies;
        
        for (auto& vsg_rollup : vsg_rollups) {
            auto aaa_record = find_matching_record(aaa_records, vsg_rollup);
            
            if (!aaa_record) {
                discrepancies.push_back({
                    .subscriber_id = vsg_rollup.subscriber_id,
                    .service_id = vsg_rollup.service_id,
                    .delta_bytes = vsg_rollup.total_bytes(),
                    .error_percent = 100.0,
                    .action = "investigate"  // Missing from AAA
                });
                continue;
            }
            
            int64_t vsg_total = vsg_rollup.total_bytes();
            int64_t aaa_total = aaa_record->total_bytes();
            double error = ((vsg_total - aaa_total) / (double)aaa_total) * 100;
            
            if (std::abs(error) > 1.0) {  // > 1% error
                discrepancies.push_back({
                    .subscriber_id = vsg_rollup.subscriber_id,
                    .service_id = vsg_rollup.service_id,
                    .delta_bytes = vsg_total - aaa_total,
                    .error_percent = error,
                    .action = error < 0 ? "credit" : "adjust"
                });
            }
        }
        
        return discrepancies;
    }
};
```

### Reconciliation Output

```json
{
    "reconciliation_run": "2025-02-23T11:00:00Z",
    "period": "2025-02-23T10:00:00Z",
    "total_subscribers": 1000000,
    "matched": 999500,
    "discrepancies": [
        {
            "subscriber_id": 12345,
            "service_id": 100,
            "delta_bytes": -1024,
            "error_percent": -0.2,
            "reason": "Network packet loss during period",
            "action": "credit_subscriber"
        }
    ],
    "summary": {
        "total_revenue": 5000000,
        "adjustments": 5000,
        "confidence": 0.998
    }
}
```

## Counter Lifecycle

### Creation

```
New flow detected (new subscriber/service pair)
    → Allocate UsageCounter
    → Store in hashmap
    → Set created_time_ns
```

### Active

```
Packets arriving → Atomic updates
                → Update last_update_ns
                → Monitor for threshold breaches
```

### Idle Detection & Cleanup

```cpp
uint32_t flush_idle_counters(uint64_t max_idle_ns) {
    uint64_t now = std::chrono::high_resolution_clock::now()
                       .time_since_epoch().count();
    uint32_t flushed = 0;
    
    for (auto it = counters_.begin(); it != counters_.end();) {
        uint64_t idle = now - it->second->last_update_ns;
        
        if (idle > max_idle_ns) {  // e.g., 30 minutes
            // Optional: Trigger final rollup before deletion
            final_rollup(it->second);
            
            it = counters_.erase(it);
            flushed++;
        } else {
            ++it;
        }
    }
    
    return flushed;
}
```

**Idle timeout**: Typical 30 minutes (configurable)

### Termination & Final Rollup

```
Flow ends (detected by timeout or explicit signal)
    → Create final rollup
    → Write to persistent storage
    → Remove from active tracking
    → Free memory
```

## Threshold Alerts

### Over-Quota Detection

```cpp
bool check_threshold(uint32_t subscriber_id, uint16_t service_id,
                     uint64_t threshold_bytes) {
    auto counter = get_counter(subscriber_id, service_id);
    uint64_t total = counter->uplink_bytes + counter->downlink_bytes;
    
    if (total > threshold_bytes) {
        counter->threshold_breaches++;
        
        // Trigger action
        if (counter->threshold_breaches == 1) {
            send_alert("subscriber_quota_exceeded", subscriber_id);
        }
        
        return true;
    }
    return false;
}
```

### Common Thresholds

- **Soft limit** (90%): Send warning
- **Hard limit** (100%): Throttle traffic
- **Overage** (+10%): Block or charge extra

## Performance Tuning

### Hash Function Quality

```cpp
// Good: FNV-1a
uint64_t fnv1a_hash(uint32_t subscriber_id, uint16_t service_id) {
    uint64_t offset = 14695981039346656037ULL;
    uint64_t prime = 1099511628211ULL;
    
    uint64_t hash = offset;
    hash ^= subscriber_id;
    hash *= prime;
    hash ^= service_id;
    hash *= prime;
    
    return hash;
}
```

**Goal**: < 5% collisions even with 10M counters

### Memory Efficiency

| Item | Size | Count | Total |
|------|------|-------|-------|
| Counter | 100 B | 1M | 100 MB |
| Hash table overhead | 10% | - | 10 MB |
| **Total** | | | **110 MB** |

**Per-core**: 1-2 counters × 65K burst = manageable

### Atomic Operation Cost

```
Uncontended atomic add: ~1 ns
Contended (CAS retry):  ~100-1000 ns

→ Use per-core atomics where possible
→ Batch updates to reduce call frequency
```

## Operational Considerations

### Backup & Recovery

```bash
# Regular snapshots
cron: "*/5 * * * *"  → dump rollups to S3
      "0 * * * *"   → daily backup to Glacier

# Recovery
Restore from most recent snapshot + replay logs
```

### Auditing

```
Log all changes:
- New counter creation: subscriber_id, service_id, timestamp
- Threshold breaches: subscriber_id, threshold, total_bytes
- Rollups: subscriber_id, service_id, bytes, timestamp
- Corrections: old_bytes, new_bytes, reason
```

### Monitoring

```
Metrics to track:
- Active counters (should grow/shrink gradually)
- Counter creation rate
- Update latency (p99)
- Reconciliation errors (should be < 1%)
- Storage usage growth
```

## Example Configuration

```ini
[metering]
max_subscribers = 1000000
max_services = 100
rollup_period_sec = 300
rollup_export_enabled = true
rollup_export_path = /data/rollups/
idle_timeout_sec = 1800
threshold_1gb = 1073741824
threshold_10gb = 10737418240
reconciliation_enabled = true
reconciliation_period_sec = 3600
```

## Testing

### Unit Tests

```cpp
// Test 1: Basic counter
meter.update_counter(1, 100, 1000, true);
assert(meter.get_counter(1, 100).uplink_bytes == 1000);

// Test 2: Atomic safety (simulate concurrent updates)
std::thread t1([&] { meter.update_counter(1, 100, 100, true); });
std::thread t2([&] { meter.update_counter(1, 100, 100, true); });
t1.join(); t2.join();
assert(meter.get_counter(1, 100).uplink_bytes == 200);

// Test 3: Rollup
auto rollup = meter.rollup(1, 100, period);
assert(rollup.uplink_bytes == expected);
```

### Integration Tests

See `tests/test_metering.cpp` for comprehensive examples.

## References

- Telecom Billing Standards (3GPP, IETF)
- Revenue Assurance best practices
- Fraud detection in billing systems
