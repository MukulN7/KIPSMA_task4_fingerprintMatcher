#include "kipsma_fingerprint/fingerprint_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

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

static void testVersion() {
    std::printf("--- Testing GetVersion ---\n");
    const char* ver = kipsma::fingerprint::GetVersion();
    CHECK(ver != nullptr, "Version string is non-null");
    CHECK(std::strlen(ver) > 0, "Version string is not empty");
}

static void testDataStructures() {
    std::printf("--- Testing Core Data Structures ---\n");
    using namespace kipsma::fingerprint;

    Minutia m(120.5f, 240.25f, 1.57f, MinutiaType::Ending, 0.95f);
    CHECK(std::fabs(m.x - 120.5f) < 1e-4f, "Minutia X coordinate initialized correctly");
    CHECK(std::fabs(m.y - 240.25f) < 1e-4f, "Minutia Y coordinate initialized correctly");
    CHECK(std::fabs(m.angle - 1.57f) < 1e-4f, "Minutia angle initialized correctly");
    CHECK(m.type == MinutiaType::Ending, "Minutia type is Ending");
    CHECK(std::fabs(m.quality - 0.95f) < 1e-4f, "Minutia quality is 0.95");

    FingerprintTemplate tpl;
    CHECK(tpl.empty(), "New template is empty");
    CHECK(tpl.size() == 0, "New template size is 0");

    tpl.minutiae.push_back(m);
    CHECK(!tpl.empty(), "Template with minutia is not empty");
    CHECK(tpl.size() == 1, "Template size is 1");
}

static void testEngineLifecycle() {
    std::printf("--- Testing FingerprintEngine Lifecycle ---\n");
    using namespace kipsma::fingerprint;

    FingerprintEngine engine;
    cv::Mat emptyMat;
    FingerprintTemplate tplEmpty = engine.extractTemplate(emptyMat);
    CHECK(tplEmpty.empty(), "Extracting on empty cv::Mat returns empty template");

    cv::Mat dummyMat(100, 200, CV_8UC1, cv::Scalar(128));
    FingerprintTemplate tplDummy = engine.extractTemplate(dummyMat);
    CHECK(tplDummy.width == 200, "Template width matches image cols (200)");
    CHECK(tplDummy.height == 100, "Template height matches image rows (100)");

    // Test move constructor
    FingerprintEngine movedEngine(std::move(engine));
    FingerprintTemplate tplProbe;
    FingerprintTemplate tplCandidate;
    MatchResult res = movedEngine.match(tplProbe, tplCandidate);
    CHECK(!res.isMatch, "Matching empty templates returns isMatch = false");
    CHECK(res.score == 0.0f, "Matching empty templates returns score = 0.0");
}

int main() {
    std::printf("=== KIPSMA Fingerprint Scaffold Smoke Test ===\n");
    testVersion();
    testDataStructures();
    testEngineLifecycle();

    std::printf("==============================================\n");
    if (g_failures == 0) {
        std::printf("ALL SMOKE TESTS PASSED.\n");
        return 0;
    } else {
        std::printf("TESTS FAILED: %d failure(s).\n", g_failures);
        return 1;
    }
}

