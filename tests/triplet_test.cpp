#include "kipsma_fingerprint/triplet.h"

#include <cmath>
#include <cstdio>
#include <iostream>
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

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline float normalizeAngle(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

} // namespace

static void testDegenerateAndEdgeInputs() {
    std::printf("--- Testing Degenerate and Boundary Inputs ---\n");
    using namespace kipsma::fingerprint;

    DelaunayTripletExtractor extractor;
    FingerprintTemplate emptyTpl;

    // 1. Empty template
    auto trips0 = extractor.extractTriplets(emptyTpl);
    CHECK(trips0.empty(), "Empty template returns 0 triplets");

    // 2. Only 2 minutiae
    FingerprintTemplate tpl2;
    tpl2.minutiae.emplace_back(50.0f, 50.0f, 0.0f, MinutiaType::Ending, 1.0f);
    tpl2.minutiae.emplace_back(100.0f, 100.0f, 0.0f, MinutiaType::Ending, 1.0f);
    auto trips2 = extractor.extractTriplets(tpl2);
    CHECK(trips2.empty(), "Template with < 3 minutiae returns 0 triplets");

    // 3. Duplicate minutiae coordinates
    FingerprintTemplate tplDupe;
    tplDupe.minutiae.emplace_back(50.0f, 50.0f, 0.0f, MinutiaType::Ending, 1.0f);
    tplDupe.minutiae.emplace_back(50.0f, 50.0f, 0.5f, MinutiaType::Ending, 1.0f);
    tplDupe.minutiae.emplace_back(100.0f, 100.0f, 1.0f, MinutiaType::Ending, 1.0f);
    auto tripsDupe = extractor.extractTriplets(tplDupe);
    CHECK(tripsDupe.empty(), "Template with duplicate points resulting in < 3 unique points returns 0 triplets");

    // 4. Collinear 3 points (zero area)
    FingerprintTemplate tplCollinear;
    tplCollinear.minutiae.emplace_back(50.0f, 50.0f, 0.0f, MinutiaType::Ending, 1.0f);
    tplCollinear.minutiae.emplace_back(100.0f, 100.0f, 0.0f, MinutiaType::Ending, 1.0f);
    tplCollinear.minutiae.emplace_back(150.0f, 150.0f, 0.0f, MinutiaType::Ending, 1.0f);
    auto tripsCol = extractor.extractTriplets(tplCollinear);
    CHECK(tripsCol.empty(), "Collinear triplet with area below threshold is safely rejected");
}

static void testRigidTransformInvariance() {
    std::printf("--- Testing Triplet Descriptor Rigid Transform Invariance ---\n");
    using namespace kipsma::fingerprint;

    DelaunayTripletExtractor extractor;

    // Construct a non-degenerate 5-point minutiae set
    FingerprintTemplate probe;
    probe.width = 400;
    probe.height = 400;
    probe.minutiae.emplace_back(80.0f, 90.0f, 0.40f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(160.0f, 70.0f, 1.20f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(200.0f, 180.0f, 2.50f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(110.0f, 210.0f, -1.10f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(130.0f, 130.0f, 0.00f, MinutiaType::Ending, 1.0f);

    auto probeTriplets = extractor.extractTriplets(probe);
    CHECK(!probeTriplets.empty(), "Extracted Delaunay triplets from probe template");

    // Transform by R(theta) + T(tx, ty)
    const float theta = 35.0f * kPi / 180.0f; // 35 degrees
    const float tx = 55.0f;
    const float ty = -40.0f;
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);

    FingerprintTemplate transformed;
    transformed.width = probe.width;
    transformed.height = probe.height;
    for (const auto& m : probe.minutiae) {
        const float x = m.x * cosT - m.y * sinT + tx;
        const float y = m.x * sinT + m.y * cosT + ty;
        const float ang = normalizeAngle(m.angle + theta);
        transformed.minutiae.emplace_back(x, y, ang, m.type, m.quality);
    }

    auto transTriplets = extractor.extractTriplets(transformed);
    CHECK(transTriplets.size() == probeTriplets.size(), "Triplet count is identical under rigid transform");

    // Match each probe triplet to its transformed counterpart and check invariance
    int invariantCount = 0;
    for (const auto& pt : probeTriplets) {
        for (const auto& tt : transTriplets) {
            // Compare sorted side lengths
            const float d0 = std::abs(pt.sideLengths[0] - tt.sideLengths[0]);
            const float d1 = std::abs(pt.sideLengths[1] - tt.sideLengths[1]);
            const float d2 = std::abs(pt.sideLengths[2] - tt.sideLengths[2]);

            // Compare internal angles
            const float a0 = std::abs(pt.internalAngles[0] - tt.internalAngles[0]);
            const float a1 = std::abs(pt.internalAngles[1] - tt.internalAngles[1]);
            const float a2 = std::abs(pt.internalAngles[2] - tt.internalAngles[2]);

            // Compare relative orientation angles
            const float r0 = std::abs(normalizeAngle(pt.relativeAngles[0] - tt.relativeAngles[0]));
            const float r1 = std::abs(normalizeAngle(pt.relativeAngles[1] - tt.relativeAngles[1]));
            const float r2 = std::abs(normalizeAngle(pt.relativeAngles[2] - tt.relativeAngles[2]));

            if (d0 < 1e-2f && d1 < 1e-2f && d2 < 1e-2f &&
                a0 < 1e-3f && a1 < 1e-3f && a2 < 1e-3f &&
                r0 < 1e-3f && r1 < 1e-3f && r2 < 1e-3f) {
                ++invariantCount;
                break;
            }
        }
    }

    CHECK(invariantCount == static_cast<int>(probeTriplets.size()),
          "All triplet descriptors are strictly invariant under rigid SE(2) transformations");
}

static void testTripletMatcherKnownTransform() {
    std::printf("--- Testing TripletMatcher Known Rigid Transform Recovery ---\n");
    using namespace kipsma::fingerprint;

    FingerprintTemplate probe;
    probe.width = 400;
    probe.height = 400;
    probe.minutiae.emplace_back(70.0f, 80.0f, 0.30f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(150.0f, 60.0f, 1.10f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(190.0f, 170.0f, 2.40f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(100.0f, 200.0f, -1.00f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(120.0f, 120.0f, 0.10f, MinutiaType::Ending, 1.0f);

    const float trueTheta = 22.0f * kPi / 180.0f;
    const float trueTx = 40.0f;
    const float trueTy = -25.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    for (const auto& m : probe.minutiae) {
        const float x = m.x * cosT - m.y * sinT + trueTx;
        const float y = m.x * sinT + m.y * cosT + trueTy;
        const float ang = normalizeAngle(m.angle + trueTheta);
        candidate.minutiae.emplace_back(x, y, ang, m.type, m.quality);
    }

    DelaunayTripletExtractor extractor;
    const auto probeTriplets = extractor.extractTriplets(probe);
    const auto candTriplets = extractor.extractTriplets(candidate);

    TripletMatcher matcher;
    const auto hyps = matcher.matchTriplets(probe, probeTriplets, candidate, candTriplets);

    CHECK(!hyps.empty(), "TripletMatcher generated hypotheses for rigid transformed pair");
    if (!hyps.empty()) {
        const auto& bestHyp = hyps[0];
        const float dTheta = std::abs(normalizeAngle(bestHyp.theta - trueTheta));
        const float dTx = std::abs(bestHyp.tx - trueTx);
        const float dTy = std::abs(bestHyp.ty - trueTy);

        CHECK(dTheta < 0.02f, "Best hypothesis recovered rotation within 0.02 rad");
        CHECK(dTx < 1.0f, "Best hypothesis recovered translation X within 1.0 px");
        CHECK(dTy < 1.0f, "Best hypothesis recovered translation Y within 1.0 px");
    }
}

static void testAmbiguousNearIsoscelesPermutations() {
    std::printf("--- Testing Ambiguous Near-Isosceles Triplet Vertex Permutations ---\n");
    using namespace kipsma::fingerprint;

    // Create a triangle with nearly equal sides (e.g. 100.0, 100.2, 95.0 px)
    // where noise or slight shear could swap the sorted side order
    FingerprintTemplate probe;
    probe.width = 300;
    probe.height = 300;
    probe.minutiae.emplace_back(100.0f, 100.0f, 0.0f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(200.0f, 100.0f, 1.57f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(150.0f, 186.6f, -1.57f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(150.0f, 120.0f, 0.5f, MinutiaType::Ending, 1.0f); // 4th point to form Delaunay triangulation

    // Candidate template with deliberate vertex index reordering and noise
    const float theta = -15.0f * kPi / 180.0f;
    const float tx = -10.0f;
    const float ty = 30.0f;
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    // Insert in reverse order to test index permutation independence
    for (int i = static_cast<int>(probe.minutiae.size()) - 1; i >= 0; --i) {
        const auto& m = probe.minutiae[i];
        const float x = m.x * cosT - m.y * sinT + tx;
        const float y = m.x * sinT + m.y * cosT + ty;
        const float ang = normalizeAngle(m.angle + theta);
        candidate.minutiae.emplace_back(x, y, ang, m.type, m.quality);
    }

    DelaunayTripletExtractor extractor;
    const auto probeTriplets = extractor.extractTriplets(probe);
    const auto candTriplets = extractor.extractTriplets(candidate);

    TripletMatcher matcher;
    const auto hyps = matcher.matchTriplets(probe, probeTriplets, candidate, candTriplets);

    CHECK(!hyps.empty(), "Matched near-isosceles triplet despite reversed vertex indices");
    if (!hyps.empty()) {
        const auto& bestHyp = hyps[0];
        const float dTheta = std::abs(normalizeAngle(bestHyp.theta - theta));
        const float dTx = std::abs(bestHyp.tx - tx);
        const float dTy = std::abs(bestHyp.ty - ty);

        CHECK(dTheta < 0.02f, "Permuted hypothesis recovered rotation accurately");
        CHECK(dTx < 1.0f, "Permuted hypothesis recovered translation accurately");
    }
}

static void testUnrelatedSyntheticImpostorRejection() {
    std::printf("--- Testing Unrelated Synthetic Impostor Triplet Matching ---\n");
    using namespace kipsma::fingerprint;

    FingerprintTemplate tplA;
    tplA.width = 300;
    tplA.height = 300;
    for (int i = 0; i < 15; ++i) {
        const float fi = static_cast<float>(i);
        const float x = 50.0f + std::fmod(fi * 41.0f + 13.0f, 200.0f);
        const float y = 50.0f + std::fmod(fi * 47.0f + 29.0f, 200.0f);
        const float ang = std::fmod(fi * 0.7f + 0.2f, 2.0f * kPi);
        tplA.minutiae.emplace_back(x, y, ang, MinutiaType::Ending, 0.9f);
    }

    FingerprintTemplate tplB;
    tplB.width = 300;
    tplB.height = 300;
    for (int i = 0; i < 15; ++i) {
        const float fi = static_cast<float>(i);
        const float x = 50.0f + std::fmod(fi * 67.0f + 31.0f, 200.0f);
        const float y = 50.0f + std::fmod(fi * 59.0f + 17.0f, 200.0f);
        const float ang = std::fmod(fi * 1.1f + 0.8f, 2.0f * kPi);
        tplB.minutiae.emplace_back(x, y, ang, MinutiaType::Ending, 0.9f);
    }

    DelaunayTripletExtractor extractor;
    const auto tripA = extractor.extractTriplets(tplA);
    const auto tripB = extractor.extractTriplets(tplB);

    TripletMatcher matcher;
    const auto hyps = matcher.matchTriplets(tplA, tripA, tplB, tripB);

    // Any accidental triplet match between random prints must not produce high global consensus
    std::printf("  Found %zu random candidate triplet matches between unrelated synthetic templates\n", hyps.size());
    CHECK(hyps.size() <= 4, "Unrelated templates generate very few or zero triplet match hypotheses");
}

static void testCandidatePoolGeneration() {
    std::printf("--- Testing Stage 1 Candidate Pool Generation & Deduplication ---\n");
    using namespace kipsma::fingerprint;

    // 1. Known rigid transformation test
    FingerprintTemplate probe;
    probe.width = 400;
    probe.height = 400;
    probe.minutiae.emplace_back(70.0f, 80.0f, 0.30f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(150.0f, 60.0f, 1.10f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(190.0f, 170.0f, 2.40f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(100.0f, 200.0f, -1.00f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(120.0f, 120.0f, 0.10f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(220.0f, 230.0f, 1.80f, MinutiaType::Bifurcation, 1.0f);

    const float trueTheta = 15.0f * kPi / 180.0f;
    const float trueTx = 25.0f;
    const float trueTy = -15.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    for (const auto& m : probe.minutiae) {
        const float x = m.x * cosT - m.y * sinT + trueTx;
        const float y = m.x * sinT + m.y * cosT + trueTy;
        const float ang = normalizeAngle(m.angle + trueTheta);
        candidate.minutiae.emplace_back(x, y, ang, m.type, m.quality);
    }

    DelaunayTripletExtractor extractor;
    const auto probeTriplets = extractor.extractTriplets(probe);
    const auto candTriplets = extractor.extractTriplets(candidate);

    TripletMatcher matcher;
    const auto pool = matcher.generateCandidatePool(probe, probeTriplets, candidate, candTriplets, 4);

    CHECK(pool.isViable, "Candidate pool is marked viable for rigid transformed genuine pair");
    CHECK(pool.matchedTriplets >= 4, "Found at least 4 compatible triplets for genuine pair");
    CHECK(!pool.candidatePairs.empty(), "Candidate pool contains candidate pairs");

    // Check strict deduplication: no duplicate (probeIndex, candidateIndex) pairs
    bool hasDuplicates = false;
    for (size_t i = 0; i < pool.candidatePairs.size(); ++i) {
        for (size_t j = i + 1; j < pool.candidatePairs.size(); ++j) {
            if (pool.candidatePairs[i].probeIndex == pool.candidatePairs[j].probeIndex &&
                pool.candidatePairs[i].candidateIndex == pool.candidatePairs[j].candidateIndex) {
                hasDuplicates = true;
                break;
            }
        }
    }
    CHECK(!hasDuplicates, "Candidate pairs are strictly deduplicated");

    // 2. Unrelated synthetic impostor templates test
    FingerprintTemplate tplA;
    tplA.width = 300;
    tplA.height = 300;
    for (int i = 0; i < 15; ++i) {
        const float fi = static_cast<float>(i);
        const float x = 50.0f + std::fmod(fi * 41.0f + 13.0f, 200.0f);
        const float y = 50.0f + std::fmod(fi * 47.0f + 29.0f, 200.0f);
        const float ang = std::fmod(fi * 0.7f + 0.2f, 2.0f * kPi);
        tplA.minutiae.emplace_back(x, y, ang, MinutiaType::Ending, 0.9f);
    }

    FingerprintTemplate tplB;
    tplB.width = 300;
    tplB.height = 300;
    for (int i = 0; i < 15; ++i) {
        const float fi = static_cast<float>(i);
        const float x = 50.0f + std::fmod(fi * 67.0f + 31.0f, 200.0f);
        const float y = 50.0f + std::fmod(fi * 59.0f + 17.0f, 200.0f);
        const float ang = std::fmod(fi * 1.1f + 0.8f, 2.0f * kPi);
        tplB.minutiae.emplace_back(x, y, ang, MinutiaType::Ending, 0.9f);
    }

    const auto tripA = extractor.extractTriplets(tplA);
    const auto tripB = extractor.extractTriplets(tplB);

    const auto impPool = matcher.generateCandidatePool(tplA, tripA, tplB, tripB, 4);
    CHECK(!impPool.isViable, "Unrelated impostor pair is non-viable (matchedTriplets < 4)");
    CHECK(impPool.candidatePairs.empty(), "Non-viable candidate pool has empty candidatePairs");
}

static void testPivotBasedRelativeVerification() {
    std::printf("--- Testing Pivot-Based Relative-Vector Verification ---\n");
    using namespace kipsma::fingerprint;

    // 1. Genuine pair under known rigid transformation (theta = +20 deg, tx = 30, ty = -20)
    FingerprintTemplate probe;
    probe.width = 400;
    probe.height = 400;
    probe.minutiae.emplace_back(80.0f, 90.0f, 0.40f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(160.0f, 70.0f, 1.20f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(200.0f, 180.0f, 2.50f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(110.0f, 210.0f, -0.90f, MinutiaType::Bifurcation, 1.0f);
    probe.minutiae.emplace_back(130.0f, 130.0f, 0.20f, MinutiaType::Ending, 1.0f);
    probe.minutiae.emplace_back(230.0f, 240.0f, 1.90f, MinutiaType::Bifurcation, 1.0f);

    const float trueTheta = 20.0f * kPi / 180.0f;
    const float trueTx = 30.0f;
    const float trueTy = -20.0f;
    const float cosT = std::cos(trueTheta);
    const float sinT = std::sin(trueTheta);

    FingerprintTemplate candidate;
    candidate.width = probe.width;
    candidate.height = probe.height;
    // Insert with index permutation (reverse order)
    for (int i = static_cast<int>(probe.minutiae.size()) - 1; i >= 0; --i) {
        const auto& m = probe.minutiae[i];
        const float x = m.x * cosT - m.y * sinT + trueTx;
        const float y = m.x * sinT + m.y * cosT + trueTy;
        const float ang = normalizeAngle(m.angle + trueTheta);
        candidate.minutiae.emplace_back(x, y, ang, m.type, m.quality);
    }

    DelaunayTripletExtractor extractor;
    const auto probeTriplets = extractor.extractTriplets(probe);
    const auto candTriplets = extractor.extractTriplets(candidate);

    TripletMatcher matcher;
    const auto pool = matcher.generateCandidatePool(probe, probeTriplets, candidate, candTriplets, 4);

    CHECK(pool.isViable, "Known rigid transform produces viable candidate pool");
    CHECK(pool.matchedTriplets >= 4, "Triplet evidence >= 4 for genuine pair");

    // Perform pivot verification over the candidate pool
    const auto& pairs = pool.candidatePairs;
    int maxMatched = 0;
    float bestTheta = 0.0f;

    for (size_t i = 0; i < pairs.size(); ++i) {
        const auto& p1 = pairs[i];
        const auto& mp1 = probe.minutiae[p1.probeIndex];
        const auto& mc1 = candidate.minutiae[p1.candidateIndex];
        const float th = normalizeAngle(mc1.angle - mp1.angle);
        const float cTh = std::cos(th);
        const float sTh = std::sin(th);

        int matched = 1;
        for (size_t j = 0; j < pairs.size(); ++j) {
            if (i == j) continue;
            const auto& p2 = pairs[j];
            if (p2.probeIndex == p1.probeIndex || p2.candidateIndex == p1.candidateIndex) continue;

            const auto& mp2 = probe.minutiae[p2.probeIndex];
            const auto& mc2 = candidate.minutiae[p2.candidateIndex];

            const float predX = mc1.x + cTh * (mp2.x - mp1.x) - sTh * (mp2.y - mp1.y);
            const float predY = mc1.y + sTh * (mp2.x - mp1.x) + cTh * (mp2.y - mp1.y);
            const float dist = std::hypot(predX - mc2.x, predY - mc2.y);

            if (dist <= 12.0f) {
                const float rotAng = normalizeAngle(mp2.angle + th);
                const float dirDiff = std::abs(normalizeAngle(rotAng - mc2.angle));
                if (dirDiff <= 0.45f) {
                    ++matched;
                }
            }
        }
        if (matched > maxMatched) {
            maxMatched = matched;
            bestTheta = th;
        }
    }

    CHECK(maxMatched == 6, "All 6 permuted minutiae verified via pivot relative vectors");
    const float errTheta = std::abs(normalizeAngle(bestTheta - trueTheta));
    CHECK(errTheta < 0.05f, "Pivot verification recovered ground-truth rotation within 0.05 rad");

    // 2. Insufficient triplet evidence test
    FingerprintTemplate smallTplA, smallTplB;
    smallTplA.minutiae.emplace_back(10.0f, 10.0f, 0.0f, MinutiaType::Ending, 1.0f);
    smallTplA.minutiae.emplace_back(20.0f, 20.0f, 0.0f, MinutiaType::Ending, 1.0f);
    const auto smallPool = matcher.generateCandidatePool(smallTplA, {}, smallTplB, {}, 4);
    CHECK(!smallPool.isViable, "Insufficient minutiae/triplets correctly flagged as non-viable");
    CHECK(smallPool.matchedTriplets == 0, "Matched triplet evidence is 0 for insufficient input");

    // 3. Unrelated synthetic impostor test
    FingerprintTemplate impA, impB;
    for (int i = 0; i < 15; ++i) {
        const float fi = static_cast<float>(i);
        impA.minutiae.emplace_back(50.0f + std::fmod(fi * 41.0f + 13.0f, 200.0f),
                                  50.0f + std::fmod(fi * 47.0f + 29.0f, 200.0f),
                                  std::fmod(fi * 0.7f + 0.2f, 2.0f * kPi), MinutiaType::Ending, 0.9f);
        impB.minutiae.emplace_back(50.0f + std::fmod(fi * 67.0f + 31.0f, 200.0f),
                                  50.0f + std::fmod(fi * 59.0f + 17.0f, 200.0f),
                                  std::fmod(fi * 1.1f + 0.8f, 2.0f * kPi), MinutiaType::Ending, 0.9f);
    }
    const auto impTripletsA = extractor.extractTriplets(impA);
    const auto impTripletsB = extractor.extractTriplets(impB);
    const auto impPool = matcher.generateCandidatePool(impA, impTripletsA, impB, impTripletsB, 4);
    CHECK(!impPool.isViable, "Synthetic impostor rejected: non-viable candidate pool");
    CHECK(impPool.candidatePairs.empty(), "Synthetic impostor produces 0 candidate pairs for verification");
}

int main() {
    std::printf("=== KIPSMA Delaunay Triplet Test Suite ===\n");
    testDegenerateAndEdgeInputs();
    testRigidTransformInvariance();
    testTripletMatcherKnownTransform();
    testAmbiguousNearIsoscelesPermutations();
    testUnrelatedSyntheticImpostorRejection();
    testCandidatePoolGeneration();
    testPivotBasedRelativeVerification();

    std::printf("==========================================\n");
    if (g_failures == 0) {
        std::printf("ALL TRIPLET TESTS PASSED.\n");
        return 0;
    } else {
        std::printf("TRIPLET TESTS FAILED: %d failure(s).\n", g_failures);
        return 1;
    }
}


