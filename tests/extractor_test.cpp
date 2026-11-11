#include "kipsma_fingerprint/extractor.h"
#include "kipsma_fingerprint/enhancer.h"
#include "kipsma_fingerprint/fingerprint_api.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
            ++g_failures; \
        } else { \
            std::printf("  [OK]   %s\n", msg); \
        } \
    } while (0)

#ifndef DATASET_DIR
#define DATASET_DIR "D:/Career/KIPSMA/FingerprintMatcher-Data"
#endif

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Creates a synthetic fingerprint patch with a single known horizontal ridge ending
cv::Mat createSyntheticEnding(int width, int height) {
    cv::Mat img(height, width, CV_8UC1, cv::Scalar(255));
    const int midY = height / 2;

    for (int k = -5; k <= 5; ++k) {
        const int y = midY + k * 14;
        if (y < 15 || y > height - 15) continue;
        if (k == 0) {
            // Middle ridge terminates at center (width / 2 = 100)
            cv::line(img, cv::Point(20, y), cv::Point(width / 2, y), cv::Scalar(40), 3);
        } else {
            // Full span ridges
            cv::line(img, cv::Point(20, y), cv::Point(width - 20, y), cv::Scalar(40), 3);
        }
    }
    return img;
}

// Creates a synthetic fingerprint patch with a single known bifurcation
cv::Mat createSyntheticBifurcation(int width, int height) {
    cv::Mat img(height, width, CV_8UC1, cv::Scalar(255));
    const int midY = height / 2;
    const int midX = width / 2;

    for (int k = -5; k <= 5; ++k) {
        const int y = midY + k * 14;
        if (y < 15 || y > height - 15) continue;
        if (k == 0) {
            // Trunk from left to center
            cv::line(img, cv::Point(20, midY), cv::Point(midX, midY), cv::Scalar(40), 3);
            // Splits into upper-right and lower-right branches
            cv::line(img, cv::Point(midX, midY), cv::Point(width - 20, midY - 14), cv::Scalar(40), 3);
            cv::line(img, cv::Point(midX, midY), cv::Point(width - 20, midY + 14), cv::Scalar(40), 3);
        } else if (k == -1 || k == 1) {
            continue; // Leave room for diverging branches
        } else {
            cv::line(img, cv::Point(20, y), cv::Point(width - 20, y), cv::Scalar(40), 3);
        }
    }
    return img;
}

// Helper to construct a synthetic EnhancementResult directly from a pattern
kipsma::fingerprint::EnhancementResult makeSyntheticEnhancement(const cv::Mat& pattern) {
    kipsma::fingerprint::EnhancementResult enh;
    enh.success = true;
    enh.isUsable = true;
    enh.grayscaleImage = pattern.clone();
    enh.enhancedImage = pattern.clone();
    enh.foregroundMask = cv::Mat(pattern.size(), CV_8UC1, cv::Scalar(255));
    enh.orientationMap = cv::Mat(pattern.size(), CV_32FC1, cv::Scalar(0.0f));
    enh.coherenceMap = cv::Mat(pattern.size(), CV_32FC1, cv::Scalar(0.95f));
    enh.qualityScore = 90.0f;
    enh.foregroundCoverage = 1.0f;
    return enh;
}

} // namespace

static void testInvalidAndEdgeInputs() {
    std::printf("--- Testing Invalid & Edge Cases ---\n");
    kipsma::fingerprint::MinutiaeExtractor extractor;

    kipsma::fingerprint::EnhancementResult emptyEnh;
    auto resEmpty = extractor.extract(emptyEnh);
    CHECK(!resEmpty.success, "Empty EnhancementResult rejected safely");
    CHECK(resEmpty.minutiae.empty(), "Empty input produces zero minutiae");

    cv::Mat emptyMat;
    kipsma::fingerprint::ImageEnhancer enhancer;
    auto resConvenienceEmpty = extractor.extract(emptyMat, enhancer);
    CHECK(!resConvenienceEmpty.success, "Convenience extract with empty image rejected safely");
}

static void testSyntheticMinutiae() {
    std::printf("--- Testing Synthetic Minutiae Ground Truth ---\n");
    kipsma::fingerprint::MinutiaeExtractor extractor;

    // 1. Synthetic Ending Test
    cv::Mat imgEnding = createSyntheticEnding(200, 200);
    auto enhEnding = makeSyntheticEnhancement(imgEnding);
    auto extEnding = extractor.extract(enhEnding);
    CHECK(extEnding.success, "Synthetic ending extracted successfully");
    CHECK(!extEnding.binarizedImage.empty(), "Binarized image generated");
    CHECK(!extEnding.skeletonImage.empty(), "Skeleton image generated");

    // Look for detected ridge ending near ground-truth termination (100, 100)
    bool foundEnding = false;
    for (const auto& m : extEnding.minutiae) {
        if (m.type == kipsma::fingerprint::MinutiaType::Ending) {
            const float dist = std::hypot(m.x - 100.0f, m.y - 100.0f);
            if (dist < 15.0f) {
                foundEnding = true;
                // Ending ridge extends toward the left (approx PI radians = 180 degrees)
                const float angleDiff = std::abs(m.angle - kPi);
                CHECK(angleDiff < 0.60f, "Ending orientation points correctly along the ridge into trunk");
                break;
            }
        }
    }
    std::printf("  extEnding minutiae count: %zu (rawEndings=%d, rawBif=%d)\n",
                extEnding.minutiae.size(), extEnding.filterStats.rawEndings, extEnding.filterStats.rawBifurcations);
    for (const auto& m : extEnding.minutiae) {
        std::printf("    Ending candidate: (%.1f, %.1f), type=%d, angle=%.2f, q=%.2f\n",
                    m.x, m.y, (int)m.type, m.angle, m.quality);
    }
    CHECK(foundEnding, "Known synthetic ridge ending detected accurately near ground truth (100, 100)");

    // 2. Synthetic Bifurcation Test
    cv::Mat imgBif = createSyntheticBifurcation(200, 200);
    auto enhBif = makeSyntheticEnhancement(imgBif);
    auto extBif = extractor.extract(enhBif);
    CHECK(extBif.success, "Synthetic bifurcation extracted successfully");
    std::printf("  extBif minutiae count: %zu (rawEndings=%d, rawBif=%d)\n",
                extBif.minutiae.size(), extBif.filterStats.rawEndings, extBif.filterStats.rawBifurcations);
    for (const auto& m : extBif.minutiae) {
        std::printf("    Bif candidate: (%.1f, %.1f), type=%d, angle=%.2f, q=%.2f\n",
                    m.x, m.y, (int)m.type, m.angle, m.quality);
    }

    bool foundBif = false;
    for (const auto& m : extBif.minutiae) {
        if (m.type == kipsma::fingerprint::MinutiaType::Bifurcation) {
            const float dist = std::hypot(m.x - 100.0f, m.y - 100.0f);
            if (dist < 18.0f) {
                foundBif = true;
                break;
            }
        }
    }
    CHECK(foundBif, "Known synthetic bifurcation detected accurately near ground truth junction (100, 100)");

    // 3. Visualization Test
    cv::Mat vis = extractor.visualizeMinutiae(imgEnding, extEnding.minutiae, extEnding.skeletonImage);
    CHECK(!vis.empty() && vis.size() == imgEnding.size(), "Minutiae visualization generated successfully");
    CHECK(vis.channels() == 3, "Visualization output is 3-channel BGR");
}

static void testRealFVCSamples() {
    std::printf("--- Testing Real FVC2002 DB1_B Impressions ---\n");
    kipsma::fingerprint::ImageEnhancer enhancer;
    kipsma::fingerprint::MinutiaeExtractor extractor;
    kipsma::fingerprint::FingerprintEngine engine;

    const std::vector<std::string> sampleFiles = {
        std::string(DATASET_DIR) + "/FVC2002/DB1_B/101_1.tif",
        std::string(DATASET_DIR) + "/FVC2002/DB1_B/101_2.tif",
        std::string(DATASET_DIR) + "/FVC2002/DB1_B/102_1.tif",
        std::string(DATASET_DIR) + "/FVC2002/DB1_B/103_1.tif"
    };

    for (const auto& filePath : sampleFiles) {
        cv::Mat img = cv::imread(filePath, cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            std::printf("  [WARN] Could not open dataset file: %s (skipping)\n", filePath.c_str());
            continue;
        }

        const auto start = std::chrono::high_resolution_clock::now();
        auto enh = enhancer.process(img);
        auto ext = extractor.extract(enh);
        const auto end = std::chrono::high_resolution_clock::now();
        const double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

        std::printf("  Sample: %s (%.1f ms total, %.1f ms ext)\n",
                    filePath.c_str(), totalMs, ext.processingTimeMs);
        std::printf("    Raw: %d (End: %d, Bif: %d) | Retained: %d (End: %d, Bif: %d)\n",
                    ext.filterStats.rawTotal(), ext.filterStats.rawEndings, ext.filterStats.rawBifurcations,
                    ext.filterStats.retainedTotal(), ext.filterStats.retainedEndings, ext.filterStats.retainedBifurcations);
        std::printf("    Removed: Border=%d, Coherence=%d, Broken=%d, Spur=%d, Cluster=%d\n",
                    ext.filterStats.removedBorderMargin, ext.filterStats.removedLowCoherence,
                    ext.filterStats.removedBrokenRidges, ext.filterStats.removedSpurs,
                    ext.filterStats.removedDuplicateClusters);

        CHECK(ext.success, "Extraction succeeded on real impression");
        CHECK(ext.minutiae.size() >= 15 && ext.minutiae.size() <= 125,
              "Retained minutiae count within canonical forensic range (15 - 125)");
        CHECK(ext.filterStats.retainedEndings > 0, "Non-zero retained ridge endings");
        CHECK(ext.filterStats.retainedBifurcations > 0, "Non-zero retained ridge bifurcations");
        CHECK(ext.binarizedImage.size() == img.size(), "Binarized image size matches input");
        CHECK(ext.skeletonImage.size() == img.size(), "Skeleton image size matches input");

        // Verify that FingerprintEngine::extractTemplate produces identical minutiae
        auto tpl = engine.extractTemplate(img);
        CHECK(tpl.size() == ext.minutiae.size(), "Engine template minutiae count matches extractor output");
        CHECK(tpl.width == static_cast<uint32_t>(img.cols), "Template width matches image cols");
        CHECK(tpl.height == static_cast<uint32_t>(img.rows), "Template height matches image rows");
    }
}

int main() {
    std::printf("=== KIPSMA Minutiae Extractor Unit Tests ===\n");
    testInvalidAndEdgeInputs();
    testSyntheticMinutiae();
    testRealFVCSamples();
    std::printf("============================================\n");

    if (g_failures == 0) {
        std::printf("ALL EXTRACTOR TESTS PASSED.\n");
        return 0;
    } else {
        std::printf("TESTS FAILED: %d failure(s).\n", g_failures);
        return 1;
    }
}
