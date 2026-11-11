#include "kipsma_fingerprint/enhancer.h"

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

cv::Mat generateSyntheticRidges(int width, int height, float angleRad, float periodPx, bool invert = false) {
    cv::Mat img(height, width, CV_8UC1);
    const float freq = 1.0f / periodPx;
    const float cosA = std::cos(angleRad);
    const float sinA = std::sin(angleRad);
    constexpr float kPi = 3.14159265358979323846f;

    for (int y = 0; y < height; ++y) {
        uchar* pRow = img.ptr<uchar>(y);
        for (int x = 0; x < width; ++x) {
            // Coordinate orthogonal to ridges
            const float u = -x * sinA + y * cosA;
            // Valleys bright (220), ridges dark (40)
            float val = 130.0f + 90.0f * std::cos(2.0f * kPi * freq * u);
            if (invert) val = 255.0f - val; // Invert: ridges bright, valleys dark
            pRow[x] = cv::saturate_cast<uchar>(val);
        }
    }
    return img;
}

} // namespace

static void testInvalidInputs() {
    std::printf("--- Testing Invalid & Edge-Case Inputs ---\n");
    kipsma::fingerprint::ImageEnhancer enhancer;

    // 1. Empty image
    cv::Mat emptyMat;
    auto resEmpty = enhancer.process(emptyMat);
    CHECK(!resEmpty.success, "Empty image rejected safely (success == false)");
    CHECK(!resEmpty.isUsable, "Empty image flagged as unusable");

    // 2. Tiny image (< 32x32)
    cv::Mat tinyMat(16, 16, CV_8UC1, cv::Scalar(100));
    auto resTiny = enhancer.process(tinyMat);
    CHECK(!resTiny.success, "Tiny image (<32x32) rejected safely");

    // 3. Flat image with 0 variance
    cv::Mat flatMat(200, 200, CV_8UC1, cv::Scalar(128));
    auto resFlat = enhancer.process(flatMat);
    CHECK(resFlat.success, "Flat image processed without crashing");
    CHECK(resFlat.foregroundCoverage < 0.05f, "Flat image has near-zero foreground coverage");
    CHECK(!resFlat.isUsable, "Flat image flagged as unusable");
}

static void testSyntheticRidges() {
    std::printf("--- Testing Synthetic Ridge Enhancements ---\n");
    kipsma::fingerprint::ImageEnhancer enhancer;

    // 1. Horizontal ridges (angle = 0 rad, period = 9 px)
    cv::Mat synthHoriz = generateSyntheticRidges(240, 240, 0.0f, 9.0f, false);
    auto resHoriz = enhancer.process(synthHoriz);
    CHECK(resHoriz.success, "Synthetic horizontal ridges processed successfully");
    CHECK(resHoriz.foregroundCoverage > 0.80f, "High foreground coverage on synthetic image");
    CHECK(resHoriz.averageCoherence > 0.70f, "High ridge flow coherence on clean sinusoidal ridges");
    CHECK(resHoriz.isUsable, "Synthetic print is flagged as usable");

    // 2. Color 3-channel input
    cv::Mat colorSynth;
    cv::cvtColor(synthHoriz, colorSynth, cv::COLOR_GRAY2BGR);
    auto resColor = enhancer.process(colorSynth);
    CHECK(resColor.success, "3-channel BGR input handled seamlessly");
    CHECK(resColor.enhancedImage.channels() == 1, "Enhanced output is single-channel grayscale");

    // 3. Polarity variation (inverted ridges: white ridges on dark background)
    cv::Mat synthInvert = generateSyntheticRidges(240, 240, 0.0f, 9.0f, true);
    auto resInvert = enhancer.process(synthInvert);
    CHECK(resInvert.success, "Inverted polarity ridges processed successfully");
    CHECK(resInvert.averageCoherence > 0.70f, "Inverted print maintains high coherence");
    CHECK(resInvert.isUsable, "Inverted print is flagged as usable");

    // 4. Visualization helpers
    cv::Mat visOri = enhancer.visualizeOrientation(resHoriz.grayscaleImage, resHoriz.orientationMap, resHoriz.foregroundMask);
    CHECK(!visOri.empty() && visOri.size() == synthHoriz.size(), "visualizeOrientation generates valid visualization");
    cv::Mat visFreq = enhancer.visualizeFrequency(resHoriz.frequencyMap, resHoriz.foregroundMask);
    CHECK(!visFreq.empty() && visFreq.size() == synthHoriz.size(), "visualizeFrequency generates valid visualization");
}

static void testRealFVCSamples() {
    std::printf("--- Testing Real FVC2002 DB1_B Samples ---\n");
    kipsma::fingerprint::ImageEnhancer enhancer;

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
        auto res = enhancer.process(img);
        const auto end = std::chrono::high_resolution_clock::now();
        const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

        std::printf("  Sample: %s (%.1f ms)\n", filePath.c_str(), elapsedMs);
        std::printf("    Coverage: %.2f%%, Coherence: %.3f, Quality: %.1f/100, Usable: %s\n",
                    res.foregroundCoverage * 100.0f, res.averageCoherence, res.qualityScore,
                    res.isUsable ? "YES" : "NO");

        CHECK(res.success, "Real FVC sample enhanced successfully");
        CHECK(res.foregroundCoverage >= 0.25f, "FVC sample foreground coverage >= 25%");
        CHECK(res.averageCoherence >= 0.35f, "FVC sample average coherence >= 0.35");
        CHECK(res.isUsable, "FVC sample deemed usable for matching");
        CHECK(res.enhancedImage.size() == img.size(), "Enhanced image matches input dimensions");
    }
}

int main() {
    std::printf("=== KIPSMA Fingerprint Image Enhancer Unit Tests ===\n");
    testInvalidInputs();
    testSyntheticRidges();
    testRealFVCSamples();
    std::printf("====================================================\n");

    if (g_failures == 0) {
        std::printf("ALL ENHANCER TESTS PASSED.\n");
        return 0;
    } else {
        std::printf("TESTS FAILED: %d failure(s).\n", g_failures);
        return 1;
    }
}
