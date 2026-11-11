#pragma once

#include "export.h"
#include "types.h"

#include <opencv2/core.hpp>
#include <memory>
#include <string>

namespace kipsma::fingerprint {

// Returns the library version string
KIPSMA_FP_API const char* GetVersion();

struct MatcherParams;

// Main engine interface for fingerprint extraction and verification
class KIPSMA_FP_API FingerprintEngine {
public:
    FingerprintEngine();
    ~FingerprintEngine();

    FingerprintEngine(const FingerprintEngine&) = delete;
    FingerprintEngine& operator=(const FingerprintEngine&) = delete;

    FingerprintEngine(FingerprintEngine&&) noexcept;
    FingerprintEngine& operator=(FingerprintEngine&&) noexcept;

    // Feature extraction: takes an input image and extracts minutiae template
    FingerprintTemplate extractTemplate(const cv::Mat& image);

    // 1:1 Matching: compares two minutiae templates and returns match score
    MatchResult match(const FingerprintTemplate& probe, const FingerprintTemplate& candidate);

    // End-to-end 1:1 Verification: validates inputs, normalizes, extracts via FingerJetFX, and verifies via OpenAFIS
    VerificationResult verifyFiles(const std::string& probePath,
                                   const std::string& candidatePath,
                                   unsigned short fallbackDpi = 500,
                                   float threshold = 2.0f);

    // Configuration
    void setMatcherParams(const MatcherParams& params);
    const MatcherParams& getMatcherParams() const;

private:
    struct Impl;
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4251)
#endif
    std::unique_ptr<Impl> impl_;
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
};

} // namespace kipsma::fingerprint
