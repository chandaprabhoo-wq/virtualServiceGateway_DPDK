#include "qoe_analytics.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace vsg {
namespace analytics {

QoECollector::QoECollector(uint32_t max_flows) : max_flows_(max_flows) {
    flow_metrics_.resize(max_flows);
    for (auto& fm : flow_metrics_) {
        fm = std::make_shared<FlowMetrics>();
        fm->start_time_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        fm->latency_buckets.resize(10, 0);  // 10 buckets for latency distribution
    }
}

QoECollector::~QoECollector() {}

void QoECollector::record_sample(const TelemetrySample& sample) {
    if (sample.flow_id >= max_flows_) return;
    
    auto& metrics = flow_metrics_[sample.flow_id];
    metrics->packet_count += 1;
    metrics->byte_count += sample.packet_size;
    metrics->last_update_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    if (sample.packet_lost) {
        metrics->loss_count += 1;
    }
    
    // Record latency in appropriate bucket (simplified)
    uint32_t bucket = std::min((uint32_t)9, sample.latency_us / 10);
    if (bucket < metrics->latency_buckets.size()) {
        metrics->latency_buckets[bucket] += 1;
    }
}

FlowKPI QoECollector::get_flow_kpi(uint32_t flow_id) {
    FlowKPI kpi;
    kpi.flow_id = flow_id;
    
    if (flow_id >= flow_metrics_.size()) return kpi;
    
    auto& metrics = flow_metrics_[flow_id];
    kpi.packet_count = metrics->packet_count.load();
    kpi.byte_count = metrics->byte_count.load();
    
    uint64_t total_samples = 0;
    for (auto& bucket : metrics->latency_buckets) {
        total_samples += bucket.load();
    }
    
    if (total_samples > 0) {
        kpi.loss_percent = (metrics->loss_count.load() * 100) / kpi.packet_count;
    }
    
    return kpi;
}

ServiceKPI QoECollector::get_service_kpi(uint16_t service_id, uint64_t window_ns) {
    ServiceKPI kpi;
    kpi.service_id = service_id;
    
    uint64_t now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    kpi.window_start_ns = now - window_ns;
    kpi.window_end_ns = now;
    
    uint64_t total_loss = 0;
    uint64_t total_packets = 0;
    
    for (auto& metrics : flow_metrics_) {
        if (!metrics) continue;
        
        uint64_t last_update = metrics->last_update_ns.load();
        if (last_update < kpi.window_start_ns) continue;
        
        uint64_t packets = metrics->packet_count.load();
        uint64_t bytes = metrics->byte_count.load();
        uint64_t loss = metrics->loss_count.load();
        
        kpi.total_flows += 1;
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

std::vector<FlowKPI> QoECollector::get_all_flow_kpis() {
    std::vector<FlowKPI> kpis;
    
    for (uint32_t i = 0; i < flow_metrics_.size(); ++i) {
        if (flow_metrics_[i]->active) {
            kpis.push_back(get_flow_kpi(i));
        }
    }
    
    return kpis;
}

std::vector<TelemetrySample> QoECollector::get_aggregated_telemetry(uint32_t max_samples) {
    std::vector<TelemetrySample> samples;
    
    // Simple sampling strategy: every Nth packet
    uint32_t sampling_interval = std::max(1u, (uint32_t)(flow_metrics_.size() / max_samples));
    
    for (uint32_t i = 0; i < flow_metrics_.size(); i += sampling_interval) {
        if (samples.size() >= max_samples) break;
        
        auto& metrics = flow_metrics_[i];
        if (!metrics->active) continue;
        
        TelemetrySample sample;
        sample.flow_id = i;
        sample.timestamp_ns = metrics->last_update_ns.load();
        
        samples.push_back(sample);
    }
    
    return samples;
}

uint32_t QoECollector::flush_expired_flows(uint64_t max_idle_ns) {
    uint32_t flushed = 0;
    uint64_t now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    for (auto& metrics : flow_metrics_) {
        if (!metrics) continue;
        
        uint64_t last_update = metrics->last_update_ns.load();
        if (now - last_update > max_idle_ns) {
            metrics->active = false;
            flushed += 1;
        }
    }
    
    return flushed;
}

} // namespace analytics
} // namespace vsg
