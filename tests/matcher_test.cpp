#include "kipsma_fingerprint/matcher.h"
#include "kipsma_fingerprint/triplet.h"
#include "kipsma_fingerprint/fingerprint_api.h"

#include <opencv2/imgcodecs.hpp>

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

// Creates a synthetic template with reproducible minutiae pattern
kipsma::fingerprint::FingerprintTemplate createSyntheticTemplate(int count, float seedOffset = 0.0f) {
    kipsma::fingerprint::FingerprintTemplate t;
    t.width = 300;
    t.height = 300;
    t.dpi = 500.0f;
    t.quality = 90.0f;

    for (int i = 0; i < count; ++i) {
        const float fi = static_cast<float>(i);
        const float x = 60.0f + std::fmod((fi * 37.0f + seedOffset * 19.0f), 180.0f);
        const float y = 60.0f + std::fmod((fi * 43.0f + seedOffset * 23.0f), 180.0f);
        const float ang = std::fmod((fi * 0.45f + seedOffset * 0.3f), 2.0f * kPi);
        const auto type = (i % 2 == 0) ? kipsma::fingerprint::MinutiaType::Ending
                                       : kipsma::fingerprint::MinutiaType::Bifurcation;

        t.minutiae.emplace_back(x, y, ang, type, 0.9f);
    }
    return t;
}

} // namespace

static void testEmptyTemplates() {
    std::printf("--- Testing Empty Templates ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate tpl1, tpl2;

    MatchResult r1 = matcher.match(tpl1, tpl2);
    CHECK(!r1.isMatch, "Both empty templates result in isMatch = false");
    CHECK(r1.score == 0.0f, "Both empty templates result in score = 0.0");
    CHECK(r1.matchedPairs == 0, "Both empty templates result in matchedPairs = 0");

    tpl1 = createSyntheticTemplate(10);
    MatchResult r2 = matcher.match(tpl1, tpl2);
    CHECK(!r2.isMatch, "One empty template results in isMatch = false");
    CHECK(r2.score == 0.0f, "One empty template results in score = 0.0");
}

static void testIdenticalTemplate() {
    std::printf("--- Testing Identical Template Match ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate tpl = createSyntheticTemplate(16);

    MatchResult res = matcher.match(tpl, tpl);
    CHECK(res.isMatch, "Identical template verified as match");
    CHECK(res.score >= 95.0f, "Identical template achieves high score (>= 95.0)");
    CHECK(res.matchedPairs == 16, "All 16 minutiae successfully paired");
    CHECK(res.rmsError < 1e-3f, "RMS residual error is near 0.0");
    CHECK(std::abs(res.rotation) < 1e-3f, "Rotation is 0.0");
    CHECK(std::abs(res.translationX) < 1e-3f, "Translation X is 0.0");
    CHECK(std::abs(res.translationY) < 1e-3f, "Translation Y is 0.0");
}

static void testKnownRigidTransformRecovery() {
    std::printf("--- Testing Known Rigid Transform Recovery ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate probe = createSyntheticTemplate(18);

    // Apply known rigid transformation: R(+15 deg) + T(+25.0 px, -18.0 px)
    const float trueTheta = 15.0f * kPi / 180.0f; // ~0.2618 rad
    const float trueTx = 25.0f;
    const float trueTy = -18.0f;

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    candidate.dpi = probe.dpi;
    candidate.quality = probe.quality;

    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    for (const auto& p : probe.minutiae) {
        const float cx = p.x * cosT - p.y * sinT + trueTx;
        const float cy = p.x * sinT + p.y * cosT + trueTy;
        float cAngle = p.angle + trueTheta;
        while (cAngle > kPi) cAngle -= 2.0f * kPi;
        while (cAngle < -kPi) cAngle += 2.0f * kPi;

        candidate.minutiae.emplace_back(cx, cy, cAngle, p.type, p.quality);
    }

    MatchResult res = matcher.match(probe, candidate);
    CHECK(res.isMatch, "Transformed template verified as match");
    CHECK(res.matchedPairs == 18, "All 18 transformed minutiae correctly paired");
    CHECK(res.score >= 90.0f, "Transformed template score is >= 90.0");

    const float dTheta = std::abs(res.rotation - trueTheta);
    const float dTx = std::abs(res.translationX - trueTx);
    const float dTy = std::abs(res.translationY - trueTy);

    CHECK(dTheta < 0.05f, "Recovered rotation is within 0.05 rad of ground truth");
    CHECK(dTx < 1.5f, "Recovered translation X is within 1.5 px of ground truth");
    CHECK(dTy < 1.5f, "Recovered translation Y is within 1.5 px of ground truth");
    CHECK(res.rmsError < 1.0f, "Spatial RMS error is under 1.0 px");
}

static void testSyntheticImpostor() {
    std::printf("--- Testing Synthetic Impostor Rejection ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate tplA = createSyntheticTemplate(20, 1.0f);
    FingerprintTemplate tplB;
    tplB.width = 300;
    tplB.height = 300;
    tplB.dpi = 500.0f;
    tplB.quality = 90.0f;
    for (int i = 0; i < 20; ++i) {
        const float fi = static_cast<float>(i);
        const float x = 50.0f + std::fmod(fi * 53.0f + 17.0f, 200.0f);
        const float y = 50.0f + std::fmod(fi * 71.0f + 29.0f, 200.0f);
        const float ang = std::fmod(fi * 1.3f + 0.8f, 2.0f * kPi);
        tplB.minutiae.emplace_back(x, y, ang, (i % 3 == 0) ? MinutiaType::Ending : MinutiaType::Bifurcation, 0.9f);
    }

    MatchResult res = matcher.match(tplA, tplB);
    CHECK(!res.isMatch, "Synthetic impostor rejected as non-match");
    CHECK(res.score < 25.0f, "Synthetic impostor score is low (< 25.0)");
    CHECK(res.matchedPairs < 6, "Synthetic impostor matched pairs is low (< 6)");
}

static void testStrict1to1Exclusivity() {
    std::printf("--- Testing Strict 1:1 Minutiae Pairing Exclusivity ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate probe;
    probe.minutiae.emplace_back(100.0f, 100.0f, 0.0f, MinutiaType::Ending, 1.0f);

    FingerprintTemplate candidate;
    // Three candidate minutiae close to the same probe position
    candidate.minutiae.emplace_back(101.0f, 100.0f, 0.05f, MinutiaType::Ending, 1.0f);
    candidate.minutiae.emplace_back(102.0f, 101.0f, 0.02f, MinutiaType::Ending, 1.0f);
    candidate.minutiae.emplace_back(99.0f, 100.0f, -0.04f, MinutiaType::Ending, 1.0f);

    MatchResult res = matcher.match(probe, candidate);
    CHECK(res.matchedPairs <= 1, "Single probe minutia paired at most once (strict 1:1 enforcement)");
}

static void testRealImpressionsFVC2002() {
    std::printf("--- Testing Real FVC2002 DB1_B Impressions ---\n");
    using namespace kipsma::fingerprint;

    const std::string dbDir = DATASET_DIR "/FVC2002/DB1_B";
    const std::string img101_1_path = dbDir + "/101_1.tif";
    const std::string img101_2_path = dbDir + "/101_2.tif";
    const std::string img102_1_path = dbDir + "/102_1.tif";

    cv::Mat img101_1 = cv::imread(img101_1_path, cv::IMREAD_UNCHANGED);
    cv::Mat img101_2 = cv::imread(img101_2_path, cv::IMREAD_UNCHANGED);
    cv::Mat img102_1 = cv::imread(img102_1_path, cv::IMREAD_UNCHANGED);

    if (img101_1.empty() || img101_2.empty() || img102_1.empty()) {
        std::printf("  [SKIP] FVC2002 DB1_B images not found at %s\n", dbDir.c_str());
        return;
    }

    FingerprintEngine engine;
    const auto tpl101_1 = engine.extractTemplate(img101_1);
    const auto tpl101_2 = engine.extractTemplate(img101_2);
    const auto tpl102_1 = engine.extractTemplate(img102_1);

    CHECK(!tpl101_1.empty(), "Template 101_1 successfully extracted");
    CHECK(!tpl101_2.empty(), "Template 101_2 successfully extracted");
    CHECK(!tpl102_1.empty(), "Template 102_1 successfully extracted");

    // 1. Genuine match test: 101_1 vs 101_2 (same finger, different impressions)
    MatchResult genRes = engine.match(tpl101_1, tpl101_2);
    std::printf("  Genuine (101_1 vs 101_2):\n"
                "    Score = %.2f, Pairs = %d, RMS = %.2f px, isMatch = %s\n"
                "    Rotation = %.2f deg (%.4f rad), Tx = %.2f, Ty = %.2f\n"
                "    Probe count = %zu, Candidate count = %zu\n",
                genRes.score, genRes.matchedPairs, genRes.rmsError, genRes.isMatch ? "TRUE" : "FALSE",
                genRes.rotation * 180.0f / kPi, genRes.rotation, genRes.translationX, genRes.translationY,
                tpl101_1.size(), tpl101_2.size());

    // 2. Impostor non-match test: 101_1 vs 102_1 (different fingers)
    MatchResult impRes = engine.match(tpl101_1, tpl102_1);
    std::printf("  Impostor (101_1 vs 102_1):\n"
                "    Score = %.2f, Pairs = %d, RMS = %.2f px, isMatch = %s\n"
                "    Rotation = %.2f deg (%.4f rad), Tx = %.2f, Ty = %.2f\n"
                "    Probe count = %zu, Candidate count = %zu\n",
                impRes.score, impRes.matchedPairs, impRes.rmsError, impRes.isMatch ? "TRUE" : "FALSE",
                impRes.rotation * 180.0f / kPi, impRes.rotation, impRes.translationX, impRes.translationY,
                tpl101_1.size(), tpl102_1.size());
}

static void testElasticPerturbation() {
    std::printf("--- Testing Elastic Perturbation Robustness ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate probe = createSyntheticTemplate(20);

    const float trueTheta = 10.0f * kPi / 180.0f;
    const float trueTx = 20.0f;
    const float trueTy = -15.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    candidate.dpi = probe.dpi;
    candidate.quality = probe.quality;

    for (size_t i = 0; i < probe.size(); ++i) {
        const auto& p = probe.minutiae[i];
        const float jitterX = 2.0f * std::sin(p.y * 0.05f);
        const float jitterY = 2.0f * std::cos(p.x * 0.05f);

        const float cx = (p.x * cosT - p.y * sinT + trueTx) + jitterX;
        const float cy = (p.x * sinT + p.y * cosT + trueTy) + jitterY;
        float cAngle = p.angle + trueTheta + 0.05f * std::sin(p.x * 0.03f);
        while (cAngle > kPi) cAngle -= 2.0f * kPi;
        while (cAngle < -kPi) cAngle += 2.0f * kPi;

        candidate.minutiae.emplace_back(cx, cy, cAngle, p.type, p.quality);
    }

    MatchResult res = matcher.match(probe, candidate);
    CHECK(res.isMatch, "Elastic perturbation template verified as match");
    CHECK(res.matchedPairs >= 15, "At least 15/20 minutiae correctly paired under elastic stretch");
    CHECK(res.score >= 50.0f, "Elastic perturbation score is high (>= 50.0)");
    CHECK(std::abs(res.rotation - trueTheta) < 0.05f, "Recovered rotation within 0.05 rad");
}

static void testMissingAndSpuriousMinutiae() {
    std::printf("--- Testing Missing and Spurious Minutiae ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate probe = createSyntheticTemplate(24);

    const float trueTheta = -8.0f * kPi / 180.0f;
    const float trueTx = 15.0f;
    const float trueTy = 10.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    candidate.dpi = probe.dpi;
    candidate.quality = probe.quality;

    for (size_t i = 0; i < probe.size(); ++i) {
        if (i % 6 == 0) continue; // drop 4 minutiae
        const auto& p = probe.minutiae[i];
        const float cx = p.x * cosT - p.y * sinT + trueTx;
        const float cy = p.x * sinT + p.y * cosT + trueTy;
        float cAngle = p.angle + trueTheta;
        while (cAngle > kPi) cAngle -= 2.0f * kPi;
        while (cAngle < -kPi) cAngle += 2.0f * kPi;
        candidate.minutiae.emplace_back(cx, cy, cAngle, p.type, p.quality);
    }

    candidate.minutiae.emplace_back(20.0f, 25.0f, 1.2f, MinutiaType::Ending, 0.5f);
    candidate.minutiae.emplace_back(270.0f, 280.0f, -0.5f, MinutiaType::Bifurcation, 0.5f);
    candidate.minutiae.emplace_back(35.0f, 260.0f, 2.1f, MinutiaType::Ending, 0.5f);
    candidate.minutiae.emplace_back(265.0f, 30.0f, -1.8f, MinutiaType::Bifurcation, 0.5f);

    MatchResult res = matcher.match(probe, candidate);
    CHECK(res.isMatch, "Template with missing and spurious minutiae verified as match");
    CHECK(res.matchedPairs >= 16, "At least 16 true corresponding minutiae paired");
    CHECK(res.score >= 50.0f, "Match score is high (>= 50.0)");
}

static void testOrientationAmbiguityHandling() {
    std::printf("--- Testing Orientation Ambiguity Handling ---\n");
    using namespace kipsma::fingerprint;

    MinutiaeMatcher matcher;
    FingerprintTemplate probe = createSyntheticTemplate(18);

    const float trueTheta = 12.0f * kPi / 180.0f;
    const float trueTx = -10.0f;
    const float trueTy = 20.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    candidate.dpi = probe.dpi;
    candidate.quality = probe.quality;

    auto normA = [](float a) {
        while (a > kPi) a -= 2.0f * kPi;
        while (a < -kPi) a += 2.0f * kPi;
        return a;
    };

    for (size_t i = 0; i < probe.size(); ++i) {
        const auto& p = probe.minutiae[i];
        const float cx = p.x * cosT - p.y * sinT + trueTx;
        const float cy = p.x * sinT + p.y * cosT + trueTy;
        float cAngle = normA(p.angle + trueTheta);

        if (p.type == MinutiaType::Ending && i % 3 == 0) {
            cAngle = normA(cAngle + kPi);
        } else if (p.type == MinutiaType::Bifurcation && i % 2 == 0) {
            cAngle = normA(cAngle + (2.0f * kPi / 3.0f));
        }

        candidate.minutiae.emplace_back(cx, cy, cAngle, p.type, p.quality);
    }

    MatchResult res = matcher.match(probe, candidate);
    CHECK(res.isMatch, "Template with orientation ambiguity verified as match");
    CHECK(res.matchedPairs >= 15, "Minutiae with orientation ambiguity correctly paired");
    CHECK(res.score >= 50.0f, "Match score is high (>= 50.0)");
}

static void testBaselineDelaunayStrategy() {
    std::printf("--- Testing Baseline Delaunay Triplet Strategy ---\n");
    using namespace kipsma::fingerprint;

    MatcherParams params;
    params.strategy = MatcherStrategy::DelaunayTripletBaseline;
    MinutiaeMatcher matcher(params);

    FingerprintTemplate probe = createSyntheticTemplate(18);
    const float trueTheta = 15.0f * kPi / 180.0f;
    const float trueTx = 25.0f;
    const float trueTy = -18.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    candidate.dpi = probe.dpi;
    candidate.quality = probe.quality;

    for (const auto& p : probe.minutiae) {
        const float cx = p.x * cosT - p.y * sinT + trueTx;
        const float cy = p.x * sinT + p.y * cosT + trueTy;
        float cAngle = p.angle + trueTheta;
        while (cAngle > kPi) cAngle -= 2.0f * kPi;
        while (cAngle < -kPi) cAngle += 2.0f * kPi;
        candidate.minutiae.emplace_back(cx, cy, cAngle, p.type, p.quality);
    }

    MatchResult res = matcher.match(probe, candidate);
    CHECK(res.isMatch, "Delaunay baseline matches rigid transform");
    CHECK(res.matchedPairs == 18, "Delaunay baseline pairs all 18 minutiae");
    CHECK(res.score >= 90.0f, "Delaunay baseline score is >= 90.0");
}

static void testSupportedCorrespondencesConstraint() {
    std::printf("--- Testing Supported Correspondences Constraint ---\n");
    using namespace kipsma::fingerprint;

    // Verify that minutiae matching occurs strictly through geometrically consistent
    // local neighborhood edge clusters, and uncoordinated random points are discarded.
    MatcherParams params;
    params.strategy = MatcherStrategy::LocalNeighborhoodConsensus;
    MinutiaeMatcher matcher(params);

    FingerprintTemplate probe = createSyntheticTemplate(18);
    const float trueTheta = 10.0f * kPi / 180.0f;
    const float trueTx = 20.0f;
    const float trueTy = -15.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    candidate.dpi = probe.dpi;
    candidate.quality = probe.quality;

    for (const auto& p : probe.minutiae) {
        const float cx = p.x * cosT - p.y * sinT + trueTx;
        const float cy = p.x * sinT + p.y * cosT + trueTy;
        float cAngle = p.angle + trueTheta;
        while (cAngle > kPi) cAngle -= 2.0f * kPi;
        while (cAngle < -kPi) cAngle += 2.0f * kPi;
        candidate.minutiae.emplace_back(cx, cy, cAngle, p.type, p.quality);
    }

    // Add uncoordinated spurious minutiae on both sides
    for (int k = 0; k < 10; ++k) {
        probe.minutiae.emplace_back(10.0f + k * 2.0f, 10.0f + k * 2.0f, 0.0f, MinutiaType::Ending, 0.4f);
        candidate.minutiae.emplace_back(280.0f - k * 2.0f, 280.0f - k * 2.0f, 1.5f, MinutiaType::Ending, 0.4f);
    }

    MatchResult res = matcher.match(probe, candidate);
    CHECK(res.isMatch, "Local neighborhood consensus correctly verifies genuine structure despite noise");
    CHECK(res.matchedPairs == 18, "Only the 18 geometrically supported genuine minutiae are matched");
    CHECK(res.score >= 50.0f, "Supported genuine score is high");
}

static void testIncorrectPoseHypothesisRejection() {
    std::printf("--- Testing Incorrect Pose Hypothesis Rejection ---\n");
    using namespace kipsma::fingerprint;

    MatcherParams params;
    params.strategy = MatcherStrategy::LocalNeighborhoodConsensus;
    MinutiaeMatcher matcher(params);

    FingerprintTemplate probe = createSyntheticTemplate(20, 1.0f);
    FingerprintTemplate candidate;
    candidate.width = 300;
    candidate.height = 300;
    candidate.dpi = 500.0f;
    candidate.quality = 90.0f;

    // Distinct pseudo-random generator parameters representing an unrelated fingerprint
    for (int i = 0; i < 20; ++i) {
        const float fi = static_cast<float>(i);
        const float x = 50.0f + std::fmod(fi * 61.0f + 23.0f, 200.0f);
        const float y = 50.0f + std::fmod(fi * 47.0f + 31.0f, 200.0f);
        const float ang = std::fmod(fi * 0.8f + 1.5f, 2.0f * kPi);
        candidate.minutiae.emplace_back(x, y, ang, (i % 3 == 0) ? MinutiaType::Ending : MinutiaType::Bifurcation, 0.9f);
    }

    MatchResult res = matcher.match(probe, candidate);
    CHECK(!res.isMatch, "Incorrect pose hypotheses on unrelated fingerprint rejected as non-match");
    CHECK(res.score < 25.0f, "Unrelated score is strictly bounded (< 25.0)");
    CHECK(res.matchedPairs < 6, "Accidental correspondences rejected (< 6 pairs)");
}

int main() {
    std::printf("=== KIPSMA Minutiae Matcher Test Suite ===\n");
    testEmptyTemplates();
    testIdenticalTemplate();
    testKnownRigidTransformRecovery();
    testSyntheticImpostor();
    testStrict1to1Exclusivity();
    testElasticPerturbation();
    testMissingAndSpuriousMinutiae();
    testOrientationAmbiguityHandling();
    testBaselineDelaunayStrategy();
    testSupportedCorrespondencesConstraint();
    testIncorrectPoseHypothesisRejection();
    testRealImpressionsFVC2002();

    std::printf("==========================================\n");
    if (g_failures == 0) {
        std::printf("ALL MATCHER TESTS PASSED.\n");
        return 0;
    } else {
        std::printf("TESTS FAILED: %d failure(s).\n", g_failures);
        return 1;
    }
}

