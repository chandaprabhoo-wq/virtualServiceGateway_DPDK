#include "dpi_classifier.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace vsg {
namespace dpi {

DPIClassifier::DPIClassifier(const DPIGuardrails& guardrails) : guardrails_(guardrails) {
    // Initialize classification counters
    classification_counts_.resize(16, 0);
}

DPIClassifier::~DPIClassifier() {}

ClassificationResult DPIClassifier::classify(const uint8_t* payload, uint32_t len) {
    if (!payload || len == 0) {
        return ClassificationResult::UNKNOWN;
    }
    
    // Check if we should sample (degradation mode)
    if (is_degraded() && (rand() % guardrails_.sample_rate) != 0) {
        return ClassificationResult::UNKNOWN;
    }
    
    // Time-bounded classification
    auto start = std::chrono::high_resolution_clock::now();
    
    ClassificationResult result = match_signatures(payload, len);
    
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    uint32_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    
    // Check timeout
    if (elapsed_us > guardrails_.timeout_us) {
        degraded_ = guardrails_.degradation_enabled;
        return ClassificationResult::UNKNOWN;
    }
    
    // Update counters
    if (result != ClassificationResult::UNKNOWN) {
        classification_counts_[(int)result] += 1;
    }
    
    return result;
}

bool DPIClassifier::load_signatures(const std::string& signature_file) {
    std::ifstream file(signature_file);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        // Parse signature line (simplified format)
        std::istringstream iss(line);
        uint16_t sig_id;
        uint8_t class_val;
        std::string pattern;
        uint32_t offset, depth;
        
        if (iss >> sig_id >> class_val >> pattern >> offset >> depth) {
            DPISignature sig;
            sig.signature_id = sig_id;
            sig.classification = (ClassificationResult)class_val;
            sig.pattern = pattern;
            sig.offset = offset;
            sig.depth = depth;
            sig.stateless = true;
            sig.priority = 0;
            
            signatures_.push_back(sig);
        }
    }
    
    // Sort by priority (descending)
    std::sort(signatures_.begin(), signatures_.end(),
        [](const DPISignature& a, const DPISignature& b) {
            return a.priority > b.priority;
        });
    
    return true;
}

void DPIClassifier::update_signature(const DPISignature& sig) {
    auto it = std::find_if(signatures_.begin(), signatures_.end(),
        [&sig](const DPISignature& s) { return s.signature_id == sig.signature_id; });
    
    if (it != signatures_.end()) {
        *it = sig;
    } else {
        signatures_.push_back(sig);
    }
}

void DPIClassifier::remove_signature(uint16_t signature_id) {
    auto it = std::find_if(signatures_.begin(), signatures_.end(),
        [signature_id](const DPISignature& s) { return s.signature_id == signature_id; });
    
    if (it != signatures_.end()) {
        signatures_.erase(it);
    }
}

uint64_t DPIClassifier::get_classification_count(ClassificationResult classification) {
    int idx = (int)classification;
    if (idx >= 0 && idx < (int)classification_counts_.size()) {
        return classification_counts_[idx].load();
    }
    return 0;
}

void DPIClassifier::set_guardrails(const DPIGuardrails& guardrails) {
    guardrails_ = guardrails;
    degraded_ = false;
}

ClassificationResult DPIClassifier::match_signatures(const uint8_t* payload, uint32_t len) {
    for (const auto& sig : signatures_) {
        if (sig.offset >= len) continue;
        
        uint32_t search_len = std::min(sig.depth, len - sig.offset);
        
        // Simple substring search
        const uint8_t* pattern_bytes = (const uint8_t*)sig.pattern.c_str();
        const uint8_t* search_start = payload + sig.offset;
        
        if (search_len >= sig.pattern.size()) {
            // This is a simplified implementation; real DPI would use more efficient algorithms
            for (uint32_t i = 0; i <= search_len - sig.pattern.size(); ++i) {
                if (std::memcmp(search_start + i, pattern_bytes, sig.pattern.size()) == 0) {
                    return sig.classification;
                }
            }
        }
    }
    
    return ClassificationResult::UNKNOWN;
}

ClassificationResult DPIClassifier::heuristic_classify(const uint8_t* payload, uint32_t len) {
    if (len < 4) return ClassificationResult::UNKNOWN;
    
    // Simple heuristics based on port numbers (if available)
    // This is a very simplified fallback
    
    return ClassificationResult::UNKNOWN;
}

} // namespace dpi
} // namespace vsg
