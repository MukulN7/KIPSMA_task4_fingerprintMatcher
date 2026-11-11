#pragma once

#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace kipsma::fingerprint {

// Standardized preprocessing error / status codes
enum class PreprocessingStatus {
    SUCCESS = 0,
    WARN_DPI_ESTIMATED,               // DPI was missing/unreliable and supplied fallback was used
    INPUT_INVALID_EMPTY,              // File not found or empty matrix
    INPUT_UNSUPPORTED_FORMAT,         // Format not recognized/decodable
    INPUT_UNSUPPORTED_DIMENSIONS,     // Physical dimensions or pixel bounds outside extractor limits
    IMAGE_QUALITY_INSUFFICIENT,       // Blank, zero-contrast, or completely obscured image
    EXTRACTION_FAILED,                // FingerJetFX failed feature extraction
    MATCHING_FAILED                   // OpenAFIS failed comparison
};

struct PreprocessingConfig {
    unsigned short configuredDpi = 0; // Caller-supplied DPI (e.g. 500). If 0 and image has no DPI metadata, reports error/warning
    bool autoPolarity = true;         // Detect dark-background/white-ridges and invert to bright-field
    bool autoContrast = true;         // Conservative contrast normalization (CLAHE / min-max stretch)
    bool cropBlankBorders = true;     // Crop excessive uninformative white/black borders safely
    float minForegroundFraction = 0.05f; // Minimum non-blank area required
    float minStdDev = 5.0f;           // Minimum image intensity variance (detect completely blank/faint images)
};

struct PreprocessingResult {
    PreprocessingStatus status = PreprocessingStatus::SUCCESS;
    std::string errorMessage;
    std::vector<std::string> appliedOperations;
    
    cv::Mat processedImage;           // Contiguous 8-bit grayscale image ready for FingerJetFX
    unsigned short detectedDpi = 0;   // DPI metadata or validated configured DPI
    bool dpiMetadataFound = false;    // Whether genuine DPI metadata was found in image file
    
    // Geometric / Quality metrics
    int originalWidth = 0;
    int originalHeight = 0;
    int processedWidth = 0;
    int processedHeight = 0;
    double originalMean = 0.0;
    double originalStdDev = 0.0;
    double processedMean = 0.0;
    double processedStdDev = 0.0;
    bool wasInverted = false;
    bool wasCropped = false;
    bool wasContrastAdjusted = false;
};

#include "export.h"

// Pure C++17 Preprocessor for fingerprint images
class KIPSMA_FP_API ImagePreprocessor {
public:
    explicit ImagePreprocessor(PreprocessingConfig config = PreprocessingConfig{});

    // Process from file path (allows reading container headers for DPI metadata)
    PreprocessingResult processFile(const std::string& filePath) const;

    // Process directly from existing cv::Mat memory buffer
    PreprocessingResult processMat(const cv::Mat& inputMat, unsigned short explicitDpi = 0) const;

    // Helper: read DPI metadata directly from file container (TIFF, BMP, PNG, JPEG)
    static bool extractDpiMetadata(const std::string& filePath, unsigned short& outDpiX, unsigned short& outDpiY);

    const PreprocessingConfig& config() const { return config_; }
    void setConfig(const PreprocessingConfig& cfg) { config_ = cfg; }

private:
    PreprocessingConfig config_;
};

} // namespace kipsma::fingerprint
