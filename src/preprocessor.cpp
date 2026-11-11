#include "kipsma_fingerprint/preprocessor.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace kipsma::fingerprint {

namespace {

// FingerJetFX physical boundary requirements from FJFX.h
// width * 500 in [150 * dpi, 812 * dpi] -> width in [0.30, 1.62] inches
// height * 500 in [150 * dpi, 1000 * dpi] -> height in [0.30, 2.00] inches
// width <= 2000, height <= 2000
// dpi in [300, 1024]
bool validateExtractorBounds(int width, int height, unsigned short dpi, std::string& err) {
    if (dpi < 300 || dpi > 1024) {
        err = "DPI " + std::to_string(dpi) + " outside FingerJetFX supported range [300, 1024]";
        return false;
    }
    if (width > 2000 || height > 2000) {
        err = "Pixel dimensions " + std::to_string(width) + "x" + std::to_string(height) + " exceed max 2000x2000";
        return false;
    }

    // Physical sizes in inches
    const double physicalWidth = static_cast<double>(width) / dpi;
    const double physicalHeight = static_cast<double>(height) / dpi;

    if (physicalWidth < 0.30 || physicalWidth > 1.624) {
        err = "Physical width " + std::to_string(physicalWidth) + " in outside required range [0.30, 1.62] in (width=" + std::to_string(width) + " px @ " + std::to_string(dpi) + " DPI)";
        return false;
    }
    if (physicalHeight < 0.30 || physicalHeight > 2.00) {
        err = "Physical height " + std::to_string(physicalHeight) + " in outside required range [0.30, 2.00] in (height=" + std::to_string(height) + " px @ " + std::to_string(dpi) + " DPI)";
        return false;
    }
    return true;
}

// Safely crops uninformative blank (near-constant background) border pixels without touching ridge structure
cv::Rect findCropBounds(const cv::Mat& gray, float minForegroundFraction) {
    if (gray.empty() || gray.rows < 30 || gray.cols < 30) {
        return cv::Rect(0, 0, gray.cols, gray.rows);
    }

    // Estimate background color from outer perimeter edge
    double borderSum = 0.0;
    int borderCount = 0;
    for (int x = 0; x < gray.cols; ++x) {
        borderSum += gray.at<uchar>(0, x);
        borderSum += gray.at<uchar>(gray.rows - 1, x);
        borderCount += 2;
    }
    for (int y = 1; y < gray.rows - 1; ++y) {
        borderSum += gray.at<uchar>(y, 0);
        borderSum += gray.at<uchar>(y, gray.cols - 1);
        borderCount += 2;
    }
    const double bgLevel = borderCount > 0 ? (borderSum / borderCount) : 255.0;

    // A pixel deviates from background if |pixel - bgLevel| > 25
    int top = 0;
    for (int y = 0; y < gray.rows; ++y) {
        int devCount = 0;
        const uchar* ptr = gray.ptr<uchar>(y);
        for (int x = 0; x < gray.cols; ++x) {
            if (std::abs(static_cast<double>(ptr[x]) - bgLevel) > 25.0) {
                devCount++;
            }
        }
        if (devCount > std::max(5, static_cast<int>(gray.cols * minForegroundFraction))) {
            top = y;
            break;
        }
    }

    int bottom = gray.rows - 1;
    for (int y = gray.rows - 1; y >= top; --y) {
        int devCount = 0;
        const uchar* ptr = gray.ptr<uchar>(y);
        for (int x = 0; x < gray.cols; ++x) {
            if (std::abs(static_cast<double>(ptr[x]) - bgLevel) > 25.0) {
                devCount++;
            }
        }
        if (devCount > std::max(5, static_cast<int>(gray.cols * minForegroundFraction))) {
            bottom = y;
            break;
        }
    }

    int left = 0;
    for (int x = 0; x < gray.cols; ++x) {
        int devCount = 0;
        for (int y = top; y <= bottom; ++y) {
            if (std::abs(static_cast<double>(gray.at<uchar>(y, x)) - bgLevel) > 25.0) {
                devCount++;
            }
        }
        if (devCount > std::max(5, static_cast<int>((bottom - top + 1) * minForegroundFraction))) {
            left = x;
            break;
        }
    }

    int right = gray.cols - 1;
    for (int x = gray.cols - 1; x >= left; --x) {
        int devCount = 0;
        for (int y = top; y <= bottom; ++y) {
            if (std::abs(static_cast<double>(gray.at<uchar>(y, x)) - bgLevel) > 25.0) {
                devCount++;
            }
        }
        if (devCount > std::max(5, static_cast<int>((bottom - top + 1) * minForegroundFraction))) {
            right = x;
            break;
        }
    }

    // Add safe padding margin (12 pixels) around foreground content
    const int pad = 12;
    top = std::max(0, top - pad);
    bottom = std::min(gray.rows - 1, bottom + pad);
    left = std::max(0, left - pad);
    right = std::min(gray.cols - 1, right + pad);

    const int width = right - left + 1;
    const int height = bottom - top + 1;

    // Only crop if at least 15% of excessive border is trimmed, and remaining size is healthy
    if (width >= 120 && height >= 120 && (width < gray.cols * 0.88 || height < gray.rows * 0.88)) {
        return cv::Rect(left, top, width, height);
    }

    return cv::Rect(0, 0, gray.cols, gray.rows);
}

} // namespace

ImagePreprocessor::ImagePreprocessor(PreprocessingConfig config)
    : config_(config) {}

bool ImagePreprocessor::extractDpiMetadata(const std::string& filePath, unsigned short& outDpiX, unsigned short& outDpiY) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return false;

    char header[64];
    f.read(header, sizeof(header));
    const std::streamsize bytesRead = f.gcount();
    if (bytesRead < 16) return false;

    // 1. TIFF Check (little-endian 'II' or big-endian 'MM')
    if ((header[0] == 'I' && header[1] == 'I' && header[2] == 0x2A && header[3] == 0x00) ||
        (header[0] == 'M' && header[1] == 'M' && header[2] == 0x00 && header[3] == 0x2A)) {
        const bool isLE = (header[0] == 'I');
        f.seekg(4, std::ios::beg);
        uint32_t ifdOffset = 0;
        f.read(reinterpret_cast<char*>(&ifdOffset), 4);
        if (!isLE) ifdOffset = ((ifdOffset & 0xFF) << 24) | ((ifdOffset & 0xFF00) << 8) | ((ifdOffset >> 8) & 0xFF00) | ((ifdOffset >> 24) & 0xFF);

        if (ifdOffset > 0 && ifdOffset < 10000000) {
            f.seekg(ifdOffset, std::ios::beg);
            uint16_t numEntries = 0;
            f.read(reinterpret_cast<char*>(&numEntries), 2);
            if (!isLE) numEntries = (numEntries << 8) | (numEntries >> 8);

            double xRes = 0.0, yRes = 0.0;
            uint16_t resUnit = 2; // Default 2 = inch

            for (uint16_t i = 0; i < numEntries && i < 200; ++i) {
                uint16_t tag = 0, typ = 0;
                uint32_t cnt = 0, val = 0;
                f.read(reinterpret_cast<char*>(&tag), 2);
                f.read(reinterpret_cast<char*>(&typ), 2);
                f.read(reinterpret_cast<char*>(&cnt), 4);
                f.read(reinterpret_cast<char*>(&val), 4);

                if (!isLE) {
                    tag = (tag << 8) | (tag >> 8);
                    typ = (typ << 8) | (typ >> 8);
                    cnt = ((cnt & 0xFF) << 24) | ((cnt & 0xFF00) << 8) | ((cnt >> 8) & 0xFF00) | ((cnt >> 24) & 0xFF);
                    val = ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF);
                }

                if (tag == 282 || tag == 283) { // XResolution or YResolution
                    const auto curPos = f.tellg();
                    f.seekg(val, std::ios::beg);
                    uint32_t num = 0, den = 0;
                    f.read(reinterpret_cast<char*>(&num), 4);
                    f.read(reinterpret_cast<char*>(&den), 4);
                    if (!isLE) {
                        num = ((num & 0xFF) << 24) | ((num & 0xFF00) << 8) | ((num >> 8) & 0xFF00) | ((num >> 24) & 0xFF);
                        den = ((den & 0xFF) << 24) | ((den & 0xFF00) << 8) | ((den >> 8) & 0xFF00) | ((den >> 24) & 0xFF);
                    }
                    if (den > 0) {
                        if (tag == 282) xRes = static_cast<double>(num) / den;
                        else yRes = static_cast<double>(num) / den;
                    }
                    f.seekg(curPos, std::ios::beg);
                } else if (tag == 296) { // ResolutionUnit
                    resUnit = static_cast<uint16_t>(val);
                }
            }

            if (xRes > 10.0) {
                if (resUnit == 3) { // cm -> inch
                    xRes *= 2.54;
                    yRes *= 2.54;
                }
                outDpiX = static_cast<unsigned short>(std::round(xRes));
                outDpiY = static_cast<unsigned short>(std::round(yRes > 10.0 ? yRes : xRes));
                return true;
            }
        }
    }

    // 2. BMP Check ('BM')
    if (header[0] == 'B' && header[1] == 'M') {
        f.seekg(38, std::ios::beg);
        int32_t ppmX = 0, ppmY = 0;
        f.read(reinterpret_cast<char*>(&ppmX), 4);
        f.read(reinterpret_cast<char*>(&ppmY), 4);
        if (ppmX > 1000) {
            const double dpiX = ppmX * 0.0254;
            const double dpiY = ppmY * 0.0254;
            outDpiX = static_cast<unsigned short>(std::round(dpiX));
            outDpiY = static_cast<unsigned short>(std::round(dpiY));
            return true;
        }
    }

    // 3. JPEG Check (0xFFD8)
    if (static_cast<unsigned char>(header[0]) == 0xFF && static_cast<unsigned char>(header[1]) == 0xD8) {
        f.seekg(2, std::ios::beg);
        while (f) {
            unsigned char marker[2];
            f.read(reinterpret_cast<char*>(marker), 2);
            if (marker[0] != 0xFF) break;
            if (marker[1] == 0xDA || marker[1] == 0xD9) break; // SOS or EOI

            uint16_t length = 0;
            f.read(reinterpret_cast<char*>(&length), 2);
            length = (length << 8) | (length >> 8);

            if (marker[1] == 0xE0 && length >= 16) { // APP0 / JFIF
                char jfifId[5];
                f.read(jfifId, 5);
                if (std::memcmp(jfifId, "JFIF\0", 5) == 0) {
                    f.seekg(2, std::ios::cur); // version
                    uint8_t units = 0;
                    uint16_t xDens = 0, yDens = 0;
                    f.read(reinterpret_cast<char*>(&units), 1);
                    f.read(reinterpret_cast<char*>(&xDens), 2);
                    f.read(reinterpret_cast<char*>(&yDens), 2);
                    xDens = (xDens << 8) | (xDens >> 8);
                    yDens = (yDens << 8) | (yDens >> 8);
                    if (units == 1 && xDens > 0) { // DPI
                        outDpiX = xDens;
                        outDpiY = yDens;
                        return true;
                    } else if (units == 2 && xDens > 0) { // DPCM
                        outDpiX = static_cast<unsigned short>(std::round(xDens * 2.54));
                        outDpiY = static_cast<unsigned short>(std::round(yDens * 2.54));
                        return true;
                    }
                }
                break;
            } else {
                f.seekg(length - 2, std::ios::cur);
            }
        }
    }

    // 4. PNG Check
    if (std::memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0) {
        f.seekg(8, std::ios::beg);
        while (f) {
            uint32_t len = 0;
            char type[4];
            f.read(reinterpret_cast<char*>(&len), 4);
            f.read(type, 4);
            len = ((len & 0xFF) << 24) | ((len & 0xFF00) << 8) | ((len >> 8) & 0xFF00) | ((len >> 24) & 0xFF);

            if (std::memcmp(type, "pHYs", 4) == 0 && len >= 9) {
                uint32_t ppuX = 0, ppuY = 0;
                uint8_t unit = 0;
                f.read(reinterpret_cast<char*>(&ppuX), 4);
                f.read(reinterpret_cast<char*>(&ppuY), 4);
                f.read(reinterpret_cast<char*>(&unit), 1);
                ppuX = ((ppuX & 0xFF) << 24) | ((ppuX & 0xFF00) << 8) | ((ppuX >> 8) & 0xFF00) | ((ppuX >> 24) & 0xFF);
                ppuY = ((ppuY & 0xFF) << 24) | ((ppuY & 0xFF00) << 8) | ((ppuY >> 8) & 0xFF00) | ((ppuY >> 24) & 0xFF);
                if (unit == 1) { // meter
                    outDpiX = static_cast<unsigned short>(std::round(ppuX * 0.0254));
                    outDpiY = static_cast<unsigned short>(std::round(ppuY * 0.0254));
                    return true;
                }
                break;
            } else {
                f.seekg(len + 4, std::ios::cur); // skip data + CRC
            }
        }
    }

    return false;
}

PreprocessingResult ImagePreprocessor::processFile(const std::string& filePath) const {
    PreprocessingResult res;

    // 1. Check file metadata for DPI
    unsigned short dpiX = 0, dpiY = 0;
    if (extractDpiMetadata(filePath, dpiX, dpiY) && dpiX >= 300 && dpiX <= 1024) {
        res.detectedDpi = dpiX;
        res.dpiMetadataFound = true;
        res.appliedOperations.push_back("Extracted DPI metadata from container: " + std::to_string(dpiX) + " DPI");
    }

    // 2. Load image via OpenCV
    cv::Mat raw = cv::imread(filePath, cv::IMREAD_UNCHANGED);
    if (raw.empty()) {
        res.status = PreprocessingStatus::INPUT_INVALID_EMPTY;
        res.errorMessage = "Failed to load image or file does not exist: " + filePath;
        return res;
    }

    const unsigned short dpiToUse = res.dpiMetadataFound ? res.detectedDpi : config_.configuredDpi;
    auto matRes = processMat(raw, dpiToUse);
    matRes.dpiMetadataFound = res.dpiMetadataFound;
    if (res.dpiMetadataFound) {
        matRes.detectedDpi = res.detectedDpi;
    }
    return matRes;
}

PreprocessingResult ImagePreprocessor::processMat(const cv::Mat& inputMat, unsigned short explicitDpi) const {
    PreprocessingResult res;

    if (inputMat.empty()) {
        res.status = PreprocessingStatus::INPUT_INVALID_EMPTY;
        res.errorMessage = "Input matrix is empty (0x0)";
        return res;
    }

    res.originalWidth = inputMat.cols;
    res.originalHeight = inputMat.rows;

    // 1. Establish and validate DPI
    unsigned short dpi = explicitDpi > 0 ? explicitDpi : config_.configuredDpi;
    if (dpi == 0) {
        res.status = PreprocessingStatus::INPUT_UNSUPPORTED_DIMENSIONS;
        res.errorMessage = "Missing DPI metadata and no configured fallback DPI supplied. Cannot determine physical scale.";
        return res;
    }
    res.detectedDpi = dpi;

    // 2. Convert to 8-bit single-channel grayscale
    cv::Mat gray;
    if (inputMat.channels() == 4) { // BGRA
        cv::cvtColor(inputMat, gray, cv::COLOR_BGRA2GRAY);
        res.appliedOperations.push_back("Converted 4-channel BGRA to 8-bit grayscale");
    } else if (inputMat.channels() == 3) { // BGR
        cv::cvtColor(inputMat, gray, cv::COLOR_BGR2GRAY);
        res.appliedOperations.push_back("Converted 3-channel BGR to 8-bit grayscale");
    } else if (inputMat.depth() == CV_16U) {
        inputMat.convertTo(gray, CV_8U, 1.0 / 256.0);
        res.appliedOperations.push_back("Normalized 16-bit grayscale to 8-bit");
    } else {
        inputMat.copyTo(gray);
    }

    // Guarantee contiguous memory layout for C-style pointer ingestion in FingerJetFX
    if (!gray.isContinuous()) {
        gray = gray.clone();
        res.appliedOperations.push_back("Reallocated non-contiguous rows to contiguous buffer");
    }

    // 3. Compute initial quality metrics
    cv::Scalar meanVal, stdVal;
    cv::meanStdDev(gray, meanVal, stdVal);
    res.originalMean = meanVal[0];
    res.originalStdDev = stdVal[0];

    // Check for blank / completely uniform images
    if (res.originalStdDev < config_.minStdDev) {
        res.status = PreprocessingStatus::IMAGE_QUALITY_INSUFFICIENT;
        res.errorMessage = "Image quality insufficient: Intensity variance (" + std::to_string(res.originalStdDev) + ") below threshold (" + std::to_string(config_.minStdDev) + "), image is nearly uniform/blank";
        return res;
    }

    // 4. Auto-Polarity / Inversion Check (Ensure bright-field: dark ridges on light background)
    if (config_.autoPolarity) {
        const int marginX = std::max(2, gray.cols / 20);
        const int marginY = std::max(2, gray.rows / 20);

        double borderSum = 0.0;
        int borderCount = 0;
        for (int y = 0; y < gray.rows; ++y) {
            const uchar* pRow = gray.ptr<uchar>(y);
            for (int x = 0; x < gray.cols; ++x) {
                if (x < marginX || x >= gray.cols - marginX || y < marginY || y >= gray.rows - marginY) {
                    borderSum += pRow[x];
                    borderCount++;
                }
            }
        }
        const double borderMean = borderCount > 0 ? (borderSum / borderCount) : 255.0;

        const cv::Rect centerRect(gray.cols / 4, gray.rows / 4, gray.cols / 2, gray.rows / 2);
        const double centerMean = cv::mean(gray(centerRect))[0];

        // If background border is dark (< 100) and center is substantially brighter, invert to bright-field
        if (borderMean < 100.0 && centerMean > (borderMean + 15.0)) {
            cv::bitwise_not(gray, gray);
            res.wasInverted = true;
            res.appliedOperations.push_back("Inverted dark-field platen polarity to standard bright-field (dark ridges on light background)");
        }
    }

    // 5. Conservative Contrast Normalization
    if (config_.autoContrast) {
        double minVal = 0.0, maxVal = 255.0;
        cv::minMaxLoc(gray, &minVal, &maxVal);

        // If dynamic range is compressed (< 140 gray levels), apply conservative min-max linear stretch
        if ((maxVal - minVal) < 140.0 && (maxVal - minVal) > 15.0) {
            gray.convertTo(gray, CV_8U, 255.0 / (maxVal - minVal), -minVal * 255.0 / (maxVal - minVal));
            res.wasContrastAdjusted = true;
            res.appliedOperations.push_back("Applied conservative dynamic range linear stretch (original span: [" + std::to_string(static_cast<int>(minVal)) + ", " + std::to_string(static_cast<int>(maxVal)) + "])");
        }
    }

    // 6. Safe Border Cropping
    if (config_.cropBlankBorders) {
        const cv::Rect cropRect = findCropBounds(gray, config_.minForegroundFraction);
        if (cropRect.width < gray.cols || cropRect.height < gray.rows) {
            // Ensure crop result still satisfies minimum physical dimension requirements
            std::string cropBoundErr;
            if (validateExtractorBounds(cropRect.width, cropRect.height, dpi, cropBoundErr)) {
                gray = gray(cropRect).clone();
                res.wasCropped = true;
                res.appliedOperations.push_back("Safely cropped uninformative blank border: " + std::to_string(cropRect.width) + "x" + std::to_string(cropRect.height) + " px");
            } else {
                res.appliedOperations.push_back("Preserved uncropped dimensions to satisfy physical size requirements (" + cropBoundErr + ")");
            }
        }
    }

    // 7. Final Dimension and Physical Bounds Validation against FingerJetFX requirements
    std::string boundErr;
    if (!validateExtractorBounds(gray.cols, gray.rows, dpi, boundErr)) {
        res.status = PreprocessingStatus::INPUT_UNSUPPORTED_DIMENSIONS;
        res.errorMessage = "Input unsupported: " + boundErr;
        return res;
    }

    cv::meanStdDev(gray, meanVal, stdVal);
    res.processedMean = meanVal[0];
    res.processedStdDev = stdVal[0];
    res.processedWidth = gray.cols;
    res.processedHeight = gray.rows;
    res.processedImage = std::move(gray);
    res.status = PreprocessingStatus::SUCCESS;

    return res;
}

} // namespace kipsma::fingerprint

