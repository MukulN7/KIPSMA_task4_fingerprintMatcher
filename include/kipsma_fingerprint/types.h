#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kipsma::fingerprint {

enum class MinutiaType : uint8_t {
    Unknown     = 0,
    Ending      = 1,
    Bifurcation = 2
};

struct Minutia {
    float x = 0.0f;              // Sub-pixel X coordinate
    float y = 0.0f;              // Sub-pixel Y coordinate
    float angle = 0.0f;          // Ridge direction [0, 2*PI) radians
    MinutiaType type = MinutiaType::Unknown;
    float quality = 1.0f;        // Local quality / confidence [0.0, 1.0]

    Minutia() = default;
    Minutia(float px, float py, float pAngle, MinutiaType pType = MinutiaType::Unknown, float pQuality = 1.0f)
        : x(px), y(py), angle(pAngle), type(pType), quality(pQuality) {}
};

struct FingerprintTemplate {
    uint32_t width = 0;
    uint32_t height = 0;
    float dpi = 500.0f;
    float quality = 0.0f;        // Overall template quality [0.0, 100.0]
    std::vector<Minutia> minutiae;

    bool empty() const { return minutiae.empty(); }
    size_t size() const { return minutiae.size(); }
};

struct MatchPair {
    int probeIndex = -1;
    int candidateIndex = -1;
    float distance = 0.0f;       // Spatial residual distance in pixels
    float angleDiff = 0.0f;      // Angular residual difference in radians
};

struct MatchResult {
    bool isMatch = false;        // Decision against calibrated verification threshold
    float score = 0.0f;          // Normalized match score [0.0, 100.0]
    int matchedPairs = 0;        // Number of aligned and verified minutiae pairs
    float rmsError = 0.0f;       // Spatial RMS alignment residual error in pixels
    std::string message;         // Status or diagnostic failure reason

    // Alignment parameters (Probe -> Candidate: cand = R(rotation) * probe + translation)
    float rotation = 0.0f;       // Estimated rotation in radians [-PI, PI]
    float translationX = 0.0f;   // Estimated translation X in pixels
    float translationY = 0.0f;   // Estimated translation Y in pixels

    // Individual matched minutiae pairs
    std::vector<MatchPair> pairs;
};

enum class VerificationDecision {
    MATCH_YES,               // Verified genuine match (score >= threshold)
    NON_MATCH_NO,            // Verified impostor non-match (score < threshold)
    ERROR_UNSUPPORTED_INPUT, // Input format, dimensions, or DPI unsupported
    ERROR_EXTRACTION_FAILED, // Image quality insufficient or extractor rejected impression
    ERROR_MATCHING_FAILED    // Comparison failed internally
};

struct VerificationResult {
    VerificationDecision decision = VerificationDecision::ERROR_UNSUPPORTED_INPUT;
    bool isMatch = false;               // true if decision == MATCH_YES
    float score = 0.0f;                 // Match score [0, 100]
    float threshold = 2.0f;             // Calibrated threshold used for decision
    std::string message;                // Detailed status, error message, or diagnostic reason
    std::vector<std::string> probeOperations; // Preprocessing steps applied to probe
    std::vector<std::string> candOperations;  // Preprocessing steps applied to candidate
};

} // namespace kipsma::fingerprint


