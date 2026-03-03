# DPI Integration & Performance Isolation

## Overview

**DPI (Deep Packet Inspection)**: Traffic classification and protocol identification with performance guardrails.

Key design goals:
- **Performance**: < 100 µs per packet classification
- **Accuracy**: > 95% for major protocols
- **Isolation**: DPI failures don't crash gateway
- **Degradation**: Graceful fallback on overload
- **Updates**: Live signature updates without restarts

## Architecture

### Classification Pipeline

```
Packet arrives
    ↓
Extract payload (first N bytes)
    ↓
Check DPI cache (if stateful)
    ↓
Try signature matching
    ├─ Timeout (>100 µs) → Degrade
    ├─ Match found → Tag with classification
    └─ No match → UNKNOWN
    ↓
Update flow metadata
    ↓
Pass downstream
```

### DPI Classifier Interface

```cpp
class DPIClassifier {
    // Classify a packet (timeout-protected)
    ClassificationResult classify(const uint8_t* payload, uint32_t len);
    
    // Manage signatures
    void load_signatures(const std::string& signature_file);
    void update_signature(const DPISignature& sig);
    void remove_signature(uint16_t signature_id);
    
    // Performance monitoring
    uint64_t get_classification_count(ClassificationResult);
    bool is_degraded() const;
};
```

## Signature Management

### Signature Format

```cpp
struct DPISignature {
    uint16_t signature_id;           // Unique ID
    ClassificationResult classification;  // Result when matched
    std::string pattern;              // Bytes to match
    uint32_t offset;                  // Offset in packet
    uint32_t depth;                   // Max search depth
    bool stateless;                   // Can match in one pass
    uint32_t priority;                // Higher = checked first
};
```

### Signature File Format

```
# Signature database (text format for manageability)
# Format: id classification pattern offset depth

1   HTTP        HTTP/1  0   512
2   HTTPS       \x16\x03\x01  0   512
3   FTP         USER    0   256
4   SSH         SSH-2.0   0   256
10  VIDEO       M3U8    0   1024
13  P2P         BitTorrent  0  512
```

### Loading Signatures

```cpp
bool DPIClassifier::load_signatures(const std::string& file) {
    std::ifstream f(file);
    std::string line;
    
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        // Parse: id class pattern offset depth
        DPISignature sig;
        // ... parse ...
        signatures_.push_back(sig);
    }
    
    // Sort by priority (higher first)
    std::sort(signatures_.begin(), signatures_.end(),
        [](const auto& a, const auto& b) { return a.priority > b.priority; });
    
    return true;
}
```

## Timeout Protection

### Time-Bounded Classification

```cpp
ClassificationResult classify(const uint8_t* payload, uint32_t len) {
    auto start = std::chrono::high_resolution_clock::now();
    
    ClassificationResult result = match_signatures(payload, len);
    
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start).count();
    
    // Check timeout
    if (elapsed > guardrails_.timeout_us) {
        degraded_ = guardrails_.degradation_enabled;
        return ClassificationResult::UNKNOWN;
    }
    
    return result;
}
```

### Timeout Tuning

| Timeout | Pros | Cons |
|---------|------|------|
| 50 µs | Low latency impact | Miss complex patterns |
| 100 µs | Good balance | Slight latency hit |
| 200 µs | High accuracy | Noticeable latency |
| 500 µs+ | Very accurate | Unacceptable latency |

**Recommended**: 100 µs (handles most common protocols)

## Degraded Mode

### Overload Handling

```
Normal load → Classify all packets
    ↓
Timeout rate > 5% → Enter degraded
    ↓
Sample 50% of packets for classification
Sample unknown classifications → Assume prior
    ↓
If load continues, sample 10%
    ↓
Recovery: Exit degraded when timeout rate < 1%
```

### Implementation

```cpp
// Degraded mode check
if (is_degraded() && (rand() % sample_rate) != 0) {
    return ClassificationResult::UNKNOWN;  // Skip expensive DPI
}

// Track timeout rate
if (elapsed > timeout_us) {
    timeout_count++;
    if (timeout_count > threshold) {
        degraded_ = true;
        sample_rate = 2;  // Sample 50%
    }
}

// Recovery
if (timeout_count == 0 && degraded_) {
    degraded_count++;
    if (degraded_count > recovery_threshold) {
        degraded_ = false;
        sample_rate = 1;  // Classify all
    }
}
```

### Metrics During Degradation

```json
{
    "degraded": true,
    "sample_rate": 0.5,
    "timeout_count": 1250,
    "timeout_rate_percent": 5.2,
    "classifications_skipped": 2500,
    "estimated_accuracy": "70%"
}
```

## Pattern Matching Strategies

### 1. Exact Match (Simple)

```cpp
// Fast: O(n) string search
if (pattern_found_at(payload, payload_len, pattern)) {
    return sig.classification;
}
```

**Use for**: HTTP, FTP, SSH (simple signatures)

### 2. Regex (Flexible)

```cpp
// Slower: Regex engine
std::regex re(pattern);
if (std::regex_search((char*)payload, (char*)payload+len, re)) {
    return sig.classification;
}
```

**Use for**: Complex protocol patterns

### 3. State Machine (Stateful)

```cpp
// Stateful: Track connection state
if (is_http_response(payload, flow->state)) {
    flow->state = HTTP_IDENTIFIED;
    return ClassificationResult::HTTP;
}
```

**Use for**: Protocols needing context

### 4. Heuristics (Fallback)

```cpp
// Heuristics when exact match fails
uint16_t port = extract_port(payload);
if (port == 80) return HTTP;
if (port == 443) return HTTPS;
if (port == 21) return FTP;
```

**Use for**: Quick guesses in degraded mode

## Classification Results

### Supported Classifications

```cpp
enum class ClassificationResult : uint8_t {
    UNKNOWN = 0,
    HTTP = 1,
    HTTPS = 2,
    FTP = 3,
    DNS = 4,
    SSH = 5,
    SMTP = 6,
    POP3 = 7,
    IMAP = 8,
    TELNET = 9,
    VIDEO_STREAMING = 10,
    AUDIO_STREAMING = 11,
    FILE_TRANSFER = 12,
    P2P = 13,
    GAMING = 14,
    VOIP = 15,
    // Extensible...
};
```

### Metadata Tagging

```cpp
struct PacketMetadata {
    uint8_t dpi_class;  // Classification from DPI
    uint64_t flow_id;
    // ... other fields ...
};

// Pass classification downstream for QoS, metering, etc.
```

## DPI Worker Pipeline

### Burst Classification

```cpp
void classify_burst(struct rte_mbuf** pkts, uint16_t count) {
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t* payload = rte_pktmbuf_mtod(pkts[i], uint8_t*);
        uint16_t len = rte_pktmbuf_pkt_len(pkts[i]);
        
        // Classify (with timeout protection)
        ClassificationResult result = classifier_->classify(payload, len);
        
        // Attach to packet
        PacketMetadata* meta = get_metadata(pkts[i]);
        meta->dpi_class = (uint8_t)result;
    }
}
```

### Integration with Metering

```cpp
// Use DPI classification for service identification
void update_metering_based_on_dpi(PacketMetadata* meta) {
    uint16_t service_id;
    
    switch ((ClassificationResult)meta->dpi_class) {
        case ClassificationResult::VIDEO_STREAMING:
            service_id = 100;  // Video service
            break;
        case ClassificationResult::VOIP:
            service_id = 200;  // VoIP service
            break;
        default:
            service_id = 1;    // Generic service
    }
    
    meter_->update_counter(meta->subscriber_id, service_id,
                          meta->packet_bytes, meta->uplink);
}
```

## Signature Updates

### Live Update Mechanism

```cpp
void update_signature(const DPISignature& sig) {
    // Read-write lock to avoid blocking classification
    std::unique_lock<std::shared_mutex> lock(sig_lock_);
    
    // Find and update or add
    auto it = std::find_if(signatures_.begin(), signatures_.end(),
        [&sig](const DPISignature& s) { 
            return s.signature_id == sig.signature_id; 
        });
    
    if (it != signatures_.end()) {
        *it = sig;
    } else {
        signatures_.push_back(sig);
    }
    
    // Re-sort by priority
    std::sort(signatures_.begin(), signatures_.end(),
        [](const auto& a, const auto& b) { return a.priority > b.priority; });
}
```

### Update Workflow

```
1. New signatures published to S3/central repo
2. Periodic check (every hour) for updates
3. Download and validate signatures
4. Apply update via update_signature() API
5. Log change for audit
6. Monitor for any increase in timeouts
```

### Configuration for Updates

```ini
[dpi]
signature_file = /etc/vsg/dpi_signatures.conf
update_enabled = true
update_check_interval_sec = 3600
update_source = https://repo.example.com/signatures.json
update_validation = gpg_verify
```

## Performance Isolation

### Resource Limits

```cpp
struct DPIGuardrails {
    uint32_t timeout_us;        // 100 µs
    uint32_t max_payloads;      // 8 KB max per packet
    uint32_t sample_rate;       // In degraded: 1=all, 10=10%
    bool degradation_enabled;   // Allow graceful degradation
};
```

### Fail-Safe Mechanisms

```cpp
// 1. Timeout protection (watchdog)
if (duration > timeout_us) {
    degraded_ = true;  // Switch to sampling
    return UNKNOWN;
}

// 2. Memory bounds
if (payload_len > max_payloads) {
    return UNKNOWN;  // Don't process huge packets
}

// 3. CPU overload
if (cpu_usage > 80%) {
    degraded_ = true;
    sample_rate = 2;  // Sample 50%
}
```

## Accuracy Considerations

### False Positives

```
Type: Classify packet as HTTP when it's not

Causes:
- Simple patterns matching coincidentally
- Incomplete signatures

Mitigation:
- Use longer, more specific patterns
- Combine multiple indicators (ports, headers)
- Validation with flow state
```

### False Negatives

```
Type: Miss actual HTTP traffic (classify as UNKNOWN)

Causes:
- Encrypted or obfuscated traffic
- Timeout due to complex parsing
- Non-standard implementations

Mitigation:
- Heuristics for common ports
- Use timeout for classification
- Fallback to behavioral analysis
```

### Accuracy Targets

| Protocol | Accuracy | Notes |
|----------|----------|-------|
| HTTP/HTTPS | 99% | Well-defined protocols |
| Video | 95% | Streaming platforms vary |
| P2P | 90% | Often obfuscated |
| VoIP | 98% | SIP/RTP patterns clear |
| Generic | 85% | Catch-all category |

## Monitoring & Alerts

### Classification Distribution

```
HTTP: 45% (450K/s)
HTTPS: 30% (300K/s)
VIDEO: 15% (150K/s)
UNKNOWN: 10% (100K/s)
```

### Alerts to Configure

```yaml
alerts:
  - name: dpi_timeout_high
    condition: timeout_rate > 5%
    action: enter_degraded_mode
  
  - name: unknown_classification_high
    condition: unknown_percent > 20%
    action: review_signatures
  
  - name: signature_load_slow
    condition: signature_count_change > 0
    action: monitor_performance
```

## Testing

### Unit Tests

See `tests/` for examples:

```cpp
// Test signature matching
DPIClassifier dpi;
dpi.load_signatures("test_signatures.conf");

uint8_t http_packet[] = "HTTP/1.1 200 OK\r\n";
auto result = dpi.classify(http_packet, sizeof(http_packet));
assert(result == ClassificationResult::HTTP);

// Test timeout protection
auto start = std::chrono::high_resolution_clock::now();
result = dpi.classify(malformed_packet, large_size);
auto elapsed = std::chrono::high_resolution_clock::now() - start;
assert(elapsed < 200us);  // Should timeout gracefully
```

### Load Testing

```bash
# Generate traffic patterns
pktgen> set all burst 64
pktgen> start all

# Monitor DPI metrics
watch 'grep "DPI" /var/log/vsg/gateway.log | tail -20'
```

## Performance Tuning

### Profile Hotspots

```bash
perf record -e cycles -F 99 ./vsg_gateway
perf report --stdio | head -30
# Look for string search, regex matching
```

### Optimization Techniques

1. **Caching**: Remember flow classifications
2. **Batching**: Group similar packets
3. **Early Exit**: Stop after first match
4. **SIMD**: Use vector instructions for pattern search (future)

## References

- NIST Guidelines for Network IDS
- Snort/Suricata rule format documentation
- Protocol RFCs (RFC 7231 for HTTP, RFC 5246 for TLS)
