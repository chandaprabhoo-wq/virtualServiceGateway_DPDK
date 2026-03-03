#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <chrono>

namespace vsg {
namespace analytics {

// Flow-level KPI data
struct FlowKPI {
    uint32_t flow_id;
    uint32_t subscriber_id;
    uint16_t service_id;
    
    // Packet statistics
    uint64_t packet_count;
    uint64_t byte_count;
    
    // Loss metrics (0-100%)
    uint32_t loss_percent;
    
    // Latency metrics (microseconds)
    uint32_t rtt_us;
    uint32_t p50_latency_us;
    uint32_t p95_latency_us;
    uint32_t p99_latency_us;
    
    // Jitter (microseconds)
    uint32_t jitter_us;
    
    // Throughput (Mbps)
    uint32_t throughput_mbps;
    
    // Time tracking
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    uint32_t duration_ms;
};

// Service-level aggregated metrics
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

// Telemetry sample for high-cardinality safe aggregation
struct TelemetrySample {
    uint32_t flow_id;
    uint32_t subscriber_id;
    uint16_t service_id;
    
    uint32_t packet_size;
    uint32_t latency_us;
    bool packet_lost;
    
    uint64_t timestamp_ns;
};

// QoE analytics collector
class QoECollector {
public:
    QoECollector(uint32_t max_flows = 100000);
    ~QoECollector();
    
    // Record a telemetry sample (fast path, lock-free)
    void record_sample(const TelemetrySample& sample);
    
    // Get flow KPI snapshot
    FlowKPI get_flow_kpi(uint32_t flow_id);
    
    // Get service KPI for a window
    ServiceKPI get_service_kpi(uint16_t service_id, uint64_t window_ns);
    
    // Aggregate all active flows
    std::vector<FlowKPI> get_all_flow_kpis();
    
    // Safe high-cardinality aggregation (sampling/bucketing)
    std::vector<TelemetrySample> get_aggregated_telemetry(
        uint32_t max_samples = 10000
    );
    
    // Flush expired flows
    uint32_t flush_expired_flows(uint64_t max_idle_ns);

private:
    struct FlowMetrics {
        std::atomic<uint64_t> packet_count{0};
        std::atomic<uint64_t> byte_count{0};
        std::atomic<uint32_t> loss_count{0};
        
        // Latency tracking (simplified histogram)
        std::vector<std::atomic<uint32_t>> latency_buckets;  // [0-100us, 100-200us, etc.]
        
        uint64_t start_time_ns;
        std::atomic<uint64_t> last_update_ns;
        bool active{true};
    };
    
    std::vector<std::shared_ptr<FlowMetrics>> flow_metrics_;
    uint32_t max_flows_;
};

} // namespace analytics
} // namespace vsg
