#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

namespace vsg {
namespace dpi {

// DPI classification result
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
    // ... extensible
};

// DPI signature for pattern matching
struct DPISignature {
    uint16_t signature_id;
    ClassificationResult classification;
    std::string pattern;
    uint32_t offset;            // Pattern offset in packet
    uint32_t depth;             // Maximum search depth
    bool stateless;             // No state tracking needed
    uint32_t priority;
};

// Performance guardrails
struct DPIGuardrails {
    uint32_t timeout_us;        // Max processing time per packet
    uint32_t max_payloads;      // Max payloads to inspect
    uint32_t sample_rate;       // Sampling (1 = all, 10 = 1 in 10)
    bool degradation_enabled;   // Fall back to heuristics on overload
};

// DPI classifier pipeline with performance isolation
class DPIClassifier {
public:
    DPIClassifier(const DPIGuardrails& guardrails = {});
    ~DPIClassifier();
    
    // Classify a packet (fast path, with timeout protection)
    ClassificationResult classify(const uint8_t* payload, uint32_t len);
    
    // Load signatures from file
    bool load_signatures(const std::string& signature_file);
    
    // Update single signature
    void update_signature(const DPISignature& sig);
    
    // Remove signature
    void remove_signature(uint16_t signature_id);
    
    // Get classification stats
    uint64_t get_classification_count(ClassificationResult classification);
    
    // Update guardrails at runtime
    void set_guardrails(const DPIGuardrails& guardrails);
    
    // Check if in degraded mode
    bool is_degraded() const { return degraded_.load(); }

private:
    std::vector<DPISignature> signatures_;
    DPIGuardrails guardrails_;
    std::atomic<bool> degraded_{false};
    std::vector<std::atomic<uint64_t>> classification_counts_;
    
    // Signature matching engine
    ClassificationResult match_signatures(const uint8_t* payload, uint32_t len);
    
    // Heuristic classification for degraded mode
    ClassificationResult heuristic_classify(const uint8_t* payload, uint32_t len);
};

} // namespace dpi
} // namespace vsg
