# QoE Analytics & Telemetry

## Overview

**QoE (Quality of Experience)**: Measurable indicators of user-perceived quality and service performance.

Key metrics:
- **Loss**: Packet loss percentage
- **Latency**: Round-trip time (RTT)
- **Jitter**: Latency variation
- **Throughput**: Data rate (Mbps)
- **MOS**: Mean Opinion Score (audio/video quality)

## Architecture

### Flow-Level Metrics

```cpp
struct FlowKPI {
    uint32_t flow_id;
    uint32_t subscriber_id;
    uint16_t service_id;
    
    // Packet statistics
    uint64_t packet_count;
    uint64_t byte_count;
    
    // Loss metrics
    uint32_t loss_percent;  // 0-100%
    
    // Latency (microseconds)
    uint32_t rtt_us;
    uint32_t p50_latency_us;
    uint32_t p95_latency_us;
    uint32_t p99_latency_us;
    
    // Jitter
    uint32_t jitter_us;
    
    // Throughput
    uint32_t throughput_mbps;
    
    // Time window
    uint64_t start_time_ns;
    uint64_t end_time_ns;
};
```

### Service-Level Aggregation

```cpp
struct ServiceKPI {
    uint16_t service_id;
    
    // Aggregated metrics
    uint64_t total_flows;
    uint64_t total_bytes;
    uint64_t total_packets;
    
    // Quality metrics (averages)
    uint32_t avg_loss_percent;
    uint32_t avg_rtt_us;
    uint32_t avg_throughput_mbps;
    
    // Distribution metrics
    uint32_t p50_loss;
    uint32_t p95_loss;
    uint32_t p99_loss;
    
    // Time window
    uint64_t window_start_ns;
    uint64_t window_end_ns;
};
```

## Collection Strategy

### Per-Packet Sampling

**Goal**: Track metrics without overwhelming storage/memory

```cpp
struct TelemetrySample {
    uint32_t flow_id;
    uint32_t subscriber_id;
    uint16_t service_id;
    uint32_t packet_size;
    uint32_t latency_us;
    bool packet_lost;
    uint64_t timestamp_ns;
};

// Record sample (sampling strategy)
void record_sample(const TelemetrySample& sample) {
    if (should_sample(sample.flow_id)) {  // e.g., sample 1 in 10
        samples_.push_back(sample);
    }
}
```

### Sampling Strategy

| Strategy | Rate | Memory | Accuracy |
|----------|------|--------|----------|
| All packets | 1:1 | 100 MB/sec | Perfect |
| 1 in 10 | 1:10 | 10 MB/sec | Good |
| 1 in 100 | 1:100 | 1 MB/sec | Fair |
| Biased (errors) | 1:1 on loss | 5 MB/sec | Good for QoS |

**Recommendation**: 1 in 10 sampling for typical deployments

### High-Cardinality Handling

**Problem**: 1M flows × 100 services = 100M possible combinations

**Solution**: Safe aggregation with bucketing

```cpp
std::vector<TelemetrySample> get_aggregated_telemetry(uint32_t max_samples) {
    std::vector<TelemetrySample> samples;
    
    // Sampling interval
    uint32_t interval = std::max(1u, (uint32_t)(all_samples_.size() / max_samples));
    
    // Collect every Nth sample
    for (uint32_t i = 0; i < all_samples_.size(); i += interval) {
        if (samples.size() >= max_samples) break;
        samples.push_back(all_samples_[i]);
    }
    
    return samples;
}
```

## Metric Calculation

### Loss Percentage

```cpp
uint32_t calculate_loss(uint64_t total_packets, uint64_t lost_packets) {
    if (total_packets == 0) return 0;
    return (lost_packets * 100) / total_packets;
}
```

### Latency Percentiles

```cpp
// Simplified: Use latency histogram buckets
std::vector<uint32_t> latency_buckets(10);  // [0-10us, 10-20us, ...]

uint32_t calculate_percentile(uint32_t percentile) {
    uint64_t total = 0;
    for (auto& bucket : latency_buckets) {
        total += bucket;
    }
    
    uint64_t target = (total * percentile) / 100;
    uint64_t cumulative = 0;
    
    for (uint32_t i = 0; i < latency_buckets.size(); ++i) {
        cumulative += latency_buckets[i];
        if (cumulative >= target) {
            return (i + 1) * 10;  // Return bucket midpoint
        }
    }
    
    return 0;
}
```

### Jitter Calculation

```cpp
uint32_t calculate_jitter(const std::vector<uint32_t>& latencies) {
    // Simplified: Std dev of latencies
    double mean = 0;
    for (auto l : latencies) mean += l;
    mean /= latencies.size();
    
    double sum_sq = 0;
    for (auto l : latencies) {
        double diff = l - mean;
        sum_sq += diff * diff;
    }
    
    double variance = sum_sq / latencies.size();
    return (uint32_t)sqrt(variance);
}
```

### Throughput Calculation

```cpp
uint32_t calculate_throughput(uint64_t bytes, uint64_t duration_us) {
    // Mbps = (bytes * 8 bits/byte) / (duration_us * 1us = 1e-6 seconds)
    //      = (bytes * 8) / (duration_us / 1e6)
    //      = (bytes * 8 * 1e6) / duration_us / 1e6
    //      = (bytes * 8) / duration_ms / 1000
    
    if (duration_us == 0) return 0;
    uint64_t mbps = (bytes * 8 * 1000) / duration_us / 1000;
    return (uint32_t)std::min(mbps, (uint64_t)UINT32_MAX);
}
```

## Flow Lifecycle

### Flow Tracking

```cpp
class QoECollector {
private:
    std::vector<std::shared_ptr<FlowMetrics>> flow_metrics_;
    uint32_t max_flows_;
};

struct FlowMetrics {
    std::atomic<uint64_t> packet_count{0};
    std::atomic<uint64_t> byte_count{0};
    std::atomic<uint32_t> loss_count{0};
    std::vector<std::atomic<uint32_t>> latency_buckets;
    
    uint64_t start_time_ns;
    std::atomic<uint64_t> last_update_ns;
    bool active{true};
};
```

### Flow Aging

```cpp
// Detect idle flows
uint32_t flush_expired_flows(uint64_t max_idle_ns) {
    uint64_t now = std::chrono::high_resolution_clock::now()
                       .time_since_epoch().count();
    uint32_t flushed = 0;
    
    for (auto& metrics : flow_metrics_) {
        if (!metrics) continue;
        
        uint64_t idle_time = now - metrics->last_update_ns;
        
        if (idle_time > max_idle_ns) {  // e.g., 30 minutes
            metrics->active = false;
            flushed++;
            
            // Optional: Generate final report
            export_flow_kpi(metrics);
        }
    }
    
    return flushed;
}
```

**Idle timeout**: Typical 30 minutes (configurable)

## Service-Level Aggregation

### Time-Windowed Aggregation

```cpp
ServiceKPI get_service_kpi(uint16_t service_id, uint64_t window_ns) {
    ServiceKPI kpi;
    kpi.service_id = service_id;
    
    uint64_t now = std::chrono::high_resolution_clock::now()
                       .time_since_epoch().count();
    kpi.window_start_ns = now - window_ns;
    kpi.window_end_ns = now;
    
    uint64_t total_loss = 0;
    uint64_t total_packets = 0;
    
    // Aggregate across all flows for this service
    for (auto& metrics : flow_metrics_) {
        if (!metrics) continue;
        
        // Only include if active within window
        if (metrics->last_update_ns < kpi.window_start_ns) continue;
        
        uint64_t packets = metrics->packet_count.load();
        uint64_t bytes = metrics->byte_count.load();
        uint64_t loss = metrics->loss_count.load();
        
        kpi.total_flows++;
        kpi.total_packets += packets;
        kpi.total_bytes += bytes;
        total_loss += loss;
        total_packets += packets;
    }
    
    if (total_packets > 0) {
        kpi.avg_loss_percent = (total_loss * 100) / total_packets;
    }
    
    return kpi;
}
```

### Percentile Distribution

```
Service: Video Streaming (ID=100)
Time window: Last 1 hour

Loss distribution:
  p50: 0.2%   (50% of flows have <= 0.2% loss)
  p95: 1.0%   (95% have <= 1.0% loss)
  p99: 5.0%   (99% have <= 5.0% loss)

Interpretation:
  Good QoE: < 1% loss is acceptable
  Alert: If p95 > 2% loss
```

## Telemetry Export

### JSON Format

```json
{
    "timestamp": "2025-02-23T10:05:00Z",
    "window_seconds": 60,
    "flow_kpis": [
        {
            "flow_id": 12345,
            "subscriber_id": 100,
            "service_id": 100,
            "packet_count": 1000,
            "byte_count": 1500000,
            "loss_percent": 0.5,
            "rtt_us": 45,
            "p99_latency_us": 120,
            "jitter_us": 15,
            "throughput_mbps": 120
        }
    ],
    "service_kpis": [
        {
            "service_id": 100,
            "total_flows": 50000,
            "avg_loss_percent": 0.8,
            "p99_loss_percent": 3.5,
            "avg_throughput_mbps": 110
        }
    ]
}
```

### Time-Series DB Integration

```cpp
void export_to_prometheus(const ServiceKPI& kpi) {
    // Format: metric_name{labels} value timestamp
    printf("vsg_service_flows{service=\"%u\"} %lu %lu\n",
        kpi.service_id, kpi.total_flows, now_ms());
    printf("vsg_service_loss_percent{service=\"%u\"} %u %lu\n",
        kpi.service_id, kpi.avg_loss_percent, now_ms());
}
```

## Alerting Rules

### QoE Thresholds

```yaml
alerts:
  - name: high_packet_loss
    condition: service_loss_percent > 2
    severity: warning
    action: notify_noc
  
  - name: high_latency
    condition: p99_latency_us > 200
    severity: warning
  
  - name: low_throughput
    condition: throughput_mbps < 10
    severity: critical
    action: trigger_failover
```

### Service-Level Alerts

```
Video Streaming (service_id=100):
- Loss > 2% → Adjust CDN cache
- Jitter > 50us → Check network
- Throughput < 5 Mbps → Trigger throttling

VoIP (service_id=200):
- Loss > 1% → Priority reroute
- Latency > 150ms → Quality alarm
- Jitter > 30us → Codec switch

Web (service_id=1):
- Latency > 500ms → Cache optimization
- No special requirements for loss/jitter
```

## Performance Considerations

### Memory Usage

| Item | Size | Notes |
|------|------|-------|
| Flow metrics (1M) | 100 MB | Per flow tracking |
| Latency histogram | 40 bytes/flow | 10 buckets |
| Samples (1:10) | 100 MB/day | At 100K Mpps |
| **Total** | **~200 MB** | Typical setup |

### Sampling Impact on Accuracy

```
Sampling Rate   Typical Error   Memory
1:1 (all)       0%             500 MB
1:10            2-5%           50 MB
1:100           5-10%          5 MB
1:1000          10-20%         1 MB

→ Use 1:10 as default (good balance)
```

## Integration with Other Components

### With Metering

```cpp
// Use QoE metrics for charging decisions
void apply_qoe_based_pricing(const FlowKPI& kpi) {
    // High loss flow? Discount slightly
    if (kpi.loss_percent > 2) {
        discount = 0.95;  // 5% credit
    }
    
    // Low throughput? Refund
    if (kpi.throughput_mbps < 1) {
        discount = 0.85;  // 15% credit
    }
}
```

### With DPI

```cpp
// Track QoE by service type
void track_service_quality(const PacketMetadata& meta, const TelemetrySample& sample) {
    // Group by DPI classification
    uint16_t service_id = dpi_to_service(meta.dpi_class);
    
    // Update both subscriber and service metrics
    qoe_collector_->record_sample({
        .flow_id = meta.flow_id,
        .subscriber_id = meta.subscriber_id,
        .service_id = service_id,
        .latency_us = sample.latency_us,
        .packet_lost = sample.packet_lost
    });
}
```

## Monitoring Dashboard

### Key Metrics to Display

```
Network Statistics:
├─ Total flows: 50,000
├─ Total throughput: 50 Gbps
└─ Active subscribers: 100,000

Quality Metrics:
├─ Avg loss: 0.5%
├─ p95 loss: 2.0%
├─ Avg latency: 45 ms
├─ p99 latency: 150 ms
└─ Jitter: 20 ms

Service Breakdown:
├─ Video: 40 Gbps (80% flows)
├─ VoIP: 5 Gbps (15% flows)
└─ Web: 5 Gbps (5% flows)
```

## Testing

### Unit Tests

See `tests/test_analytics.cpp`:

```cpp
void test_flow_kpi() {
    QoECollector collector(1000);
    
    // Record samples
    TelemetrySample sample;
    sample.flow_id = 1;
    sample.packet_size = 1500;
    sample.latency_us = 50;
    sample.packet_lost = false;
    
    collector.record_sample(sample);
    
    // Verify KPI
    auto kpi = collector.get_flow_kpi(1);
    assert(kpi.packet_count > 0);
}
```

## References

- IETF RFC 3393: IP Packet Delay Variation Metric
- ITU-T G.1030: QoE assessment methodology
- 3GPP TS 32.417: Performance management
