#pragma once

#include "export.h"
#include "types.h"
#include "enhancer.h"

#include <opencv2/core.hpp>
#include <memory>
#include <string>
#include <vector>

namespace kipsma::fingerprint {

// Configurable parameters for binarization, skeletonization, and minutiae filtering
struct ExtractorParams {
    // Adaptive binarization parameters
    int   binarizeBlockSize       = 15;    // Neighborhood size for adaptive thresholding
    double binarizeC              = 0.0;   // Constant subtracted from adaptive mean

    // Boundary and quality gating
    int   borderMargin            = 16;    // Minimum distance from foreground mask border (pixels)
    float minCoherence            = 0.30f; // Minimum orientation coherence for valid minutia

    // Spatial artifact filtering
    float minMinutiaeDistance     = 10.0f; // Minimum distance between valid minutiae (pixels)
    float brokenRidgeMaxDist      = 14.0f; // Max distance between opposing endings to detect broken ridge
    float brokenRidgeCosThreshold = -0.6f; // Cosine threshold for opposing directions (~125° to 180°)
    float spurMaxDistance         = 12.0f; // Max distance between bifurcation and ending forming a spur
    float minBifurcationDistance  = 14.0f; // Min distance between bifurcations forming bridge
    float islandMaxDistance       = 14.0f; // Max length for isolated short ridge islands (two endings)

    // Direction tracing
    int   directionTraceSteps     = 8;     // Number of skeleton pixels to trace for ridge direction
};

// Diagnostic statistics tracking minutiae filtering stages
struct ExtractionFilterStats {
    int rawEndings               = 0; // Raw crossing-number CN=1 detections
    int rawBifurcations          = 0; // Raw crossing-number CN=3 detections
    int removedBorderMargin      = 0; // Rejected near mask boundary
    int removedLowCoherence      = 0; // Rejected due to poor local ridge flow coherence
    int removedDuplicateClusters = 0; // Rejected due to tight spatial clustering
    int removedBrokenRidges      = 0; // Rejected as opposing break pairs on the same ridge
    int removedSpurs             = 0; // Rejected as short spurious branches
    int retainedEndings          = 0; // Final valid ridge endings
    int retainedBifurcations     = 0; // Final valid bifurcations

    int rawTotal() const { return rawEndings + rawBifurcations; }
    int retainedTotal() const { return retainedEndings + retainedBifurcations; }
    int totalRemoved() const {
        return removedBorderMargin + removedLowCoherence +
               removedDuplicateClusters + removedBrokenRidges + removedSpurs;
    }
};

// Result structure containing intermediate images, extracted minutiae, and stats
struct ExtractionResult {
    bool                   success = false;
    cv::Mat                binarizedImage; // CV_8UC1: 255 for ridges, 0 for valleys/background
    cv::Mat                skeletonImage;  // CV_8UC1: 255 for 1-pixel skeleton ridges, 0 elsewhere
    std::vector<Minutia>   minutiae;       // Final filtered minutiae set
    ExtractionFilterStats  filterStats;    // Counts for each filtering stage
    double                 processingTimeMs = 0.0;
    std::string            statusMessage;
};

// Minutiae extraction engine implementing adaptive binarization, Zhang-Suen thinning,
// crossing-number detection, ridge direction tracing, and false-minutiae filtering.
class KIPSMA_FP_API MinutiaeExtractor {
public:
    explicit MinutiaeExtractor(const ExtractorParams& params = ExtractorParams());
    ~MinutiaeExtractor();

    MinutiaeExtractor(const MinutiaeExtractor&);
    MinutiaeExtractor& operator=(const MinutiaeExtractor&);
    MinutiaeExtractor(MinutiaeExtractor&&) noexcept;
    MinutiaeExtractor& operator=(MinutiaeExtractor&&) noexcept;

    // Extracts minutiae from precomputed enhancement results
    ExtractionResult extract(const EnhancementResult& enhancement) const;

    // Convenience overload: enhances input image then extracts minutiae
    ExtractionResult extract(const cv::Mat& inputImage, const ImageEnhancer& enhancer) const;

    // Visualizes minutiae on top of a background image (e.g. grayscale or enhanced print)
    // Red circles/vectors: Ridge endings
    // Green circles/vectors: Bifurcations
    // Optional faint blue overlay: Ridge skeleton
    cv::Mat visualizeMinutiae(const cv::Mat& background,
                             const std::vector<Minutia>& minutiae,
                             const cv::Mat& skeleton = cv::Mat()) const;

    const ExtractorParams& params() const;
    void setParams(const ExtractorParams& params);

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
