#pragma once

#include "export.h"

#include <opencv2/core.hpp>
#include <memory>
#include <string>

namespace kipsma::fingerprint {

// Configurable parameters for fingerprint image enhancement
struct EnhancerParams {
    int   blockSize          = 16;            // Block size for orientation and frequency (pixels)
    int   orientSmoothSize   = 15;            // Size of Gaussian filter for orientation field smoothing
    int   freqWindowSize     = 32;            // Window size for local 1D profile frequency projection
    int   maskErosionRadius  = 4;             // Boundary margin exclusion radius to clean irregular edges
    float minFrequency       = 1.0f / 25.0f;  // Minimum ridge frequency (cycles/pixel, max period 25px)
    float maxFrequency       = 1.0f / 3.0f;   // Maximum ridge frequency (cycles/pixel, min period 3px)
    float defaultFrequency   = 1.0f / 9.0f;   // Canonical frequency for fallback (~500 dpi, period 9px)
    float varThreshold       = 0.005f;        // Normalized local variance threshold for foreground
    float coherenceThreshold = 0.20f;         // Minimum ridge coherence for reliable orientation
    int   gaborKernelRadius  = 7;             // Half-size of Gabor kernel (kernel size = 2 * radius + 1)
    float gaborSigmaX        = 4.0f;          // Spatial sigma across ridges
    float gaborSigmaY        = 4.0f;          // Spatial sigma along ridges
    bool  autoPolarity       = true;          // Detect and correct inverted ridge polarity
};

// Result structure containing intermediate and enhanced representations
struct EnhancementResult {
    bool        success            = false;
    cv::Mat     grayscaleImage;    // CV_8UC1 normalized polarity grayscale image
    cv::Mat     normalizedImage;   // CV_32FC1 local contrast normalized image
    cv::Mat     foregroundMask;    // CV_8UC1 binary mask (255 foreground, 0 background)
    cv::Mat     orientationMap;    // CV_32FC1 ridge orientation in radians [0, PI) per pixel
    cv::Mat     coherenceMap;      // CV_32FC1 ridge flow coherence [0.0, 1.0] per pixel
    cv::Mat     frequencyMap;      // CV_32FC1 ridge frequency (cycles/pixel) per pixel
    cv::Mat     enhancedImage;     // CV_8UC1 orientation-aware Gabor enhanced image
    float       foregroundCoverage = 0.0f;   // Ratio of foreground pixels [0.0, 1.0]
    float       averageCoherence   = 0.0f;   // Mean coherence in foreground [0.0, 1.0]
    float       qualityScore       = 0.0f;   // Overall image quality metric [0.0, 100.0]
    bool        isUsable           = false;  // Whether image passes quality and coverage gates
    std::string statusMessage;               // Diagnostic status message
};

// Image enhancement module performing normalization, orientation, frequency, and Gabor filtering
class KIPSMA_FP_API ImageEnhancer {
public:
    explicit ImageEnhancer(const EnhancerParams& params = EnhancerParams());
    ~ImageEnhancer();

    ImageEnhancer(const ImageEnhancer&);
    ImageEnhancer& operator=(const ImageEnhancer&);
    ImageEnhancer(ImageEnhancer&&) noexcept;
    ImageEnhancer& operator=(ImageEnhancer&&) noexcept;

    // Process a raw fingerprint image
    EnhancementResult process(const cv::Mat& inputImage) const;

    // Visualizes orientation field as directional line segments
    cv::Mat visualizeOrientation(const cv::Mat& image,
                                 const cv::Mat& orientationMap,
                                 const cv::Mat& mask,
                                 int step = 16,
                                 int lineLength = 10) const;

    // Visualizes frequency field as a normalized 8-bit heatmap
    cv::Mat visualizeFrequency(const cv::Mat& frequencyMap,
                               const cv::Mat& mask) const;

    const EnhancerParams& params() const;
    void setParams(const EnhancerParams& params);

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
