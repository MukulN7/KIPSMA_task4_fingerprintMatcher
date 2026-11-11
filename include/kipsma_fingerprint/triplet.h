#pragma once

#include "export.h"
#include "types.h"

#include <array>
#include <vector>

namespace kipsma::fingerprint {

/**
 * Deterministic representation of a Delaunay minutiae triplet.
 * Stores 3 minutiae indices with geometry and invariant descriptors.
 */
struct DelaunayTriplet {
    // Indices into the source FingerprintTemplate::minutiae vector
    std::array<int, 3> minutiaeIndices = {-1, -1, -1};

    // Sorted side lengths in descending order: d0 >= d1 >= d2 (in pixels)
    std::array<float, 3> sideLengths = {0.0f, 0.0f, 0.0f};

    // Internal triangle angles at the respective opposite vertices [0, PI]
    std::array<float, 3> internalAngles = {0.0f, 0.0f, 0.0f};

    // Relative minutiae orientation angles with respect to outgoing edges [-PI, PI]
    std::array<float, 3> relativeAngles = {0.0f, 0.0f, 0.0f};

    // Geometric area of the triangle
    float area = 0.0f;

    // Centroid of the triangle in template coordinates
    float centroidX = 0.0f;
    float centroidY = 0.0f;
};

struct TripletExtractorParams {
    float minSideLength = 10.0f;       // Minimum side length in pixels to avoid dense cluster noise
    float maxSideLength = 220.0f;      // Maximum side length in pixels (avoids spanning distant regions)
    float minTriangleArea = 30.0f;     // Minimum area to reject degenerate near-collinear triplets
    float maxCollinearAspect = 0.98f;  // Maximum cosine/ratio check to prune nearly flat triangles
};

class KIPSMA_FP_API DelaunayTripletExtractor {
public:
    explicit DelaunayTripletExtractor(TripletExtractorParams params = TripletExtractorParams{});

    /**
     * Extracts canonical, invariant Delaunay triplets from a minutiae template.
     * Safely handles edge cases: < 3 minutiae, duplicate coordinates, degenerate geometry,
     * and bounding rectangle margins.
     */
    std::vector<DelaunayTriplet> extractTriplets(const FingerprintTemplate& tpl) const;

    const TripletExtractorParams& params() const { return params_; }
    void setParams(const TripletExtractorParams& p) { params_ = p; }

private:
    TripletExtractorParams params_;
};

/**
 * Represents a correspondence hypothesis between two triangles.
 * vertexMap[k] gives the candidate minutia index corresponding to probe vertex k (k in {0, 1, 2}).
 */
struct TripletMatchHypothesis {
    int probeTripletIdx = -1;
    int candidateTripletIdx = -1;
    std::array<int, 3> probeMinutiae = {-1, -1, -1};
    std::array<int, 3> candidateMinutiae = {-1, -1, -1}; // Ordered to correspond to probeMinutiae[0..2]

    float theta = 0.0f;   // Estimated rotation in radians [-PI, PI]
    float tx = 0.0f;      // Estimated translation X in pixels
    float ty = 0.0f;      // Estimated translation Y in pixels
    float score = 0.0f;   // Similarity score based on side length and angular fit
};

struct TripletMatcherParams {
    float maxSideLengthDiff = 10.0f;     // Max allowable discrepancy on corresponding side lengths (px)
    float maxSideRatioDiff = 0.12f;      // Max allowable relative difference on side lengths
    float maxInternalAngleDiff = 0.26f;  // Max difference on internal triangle angles (rad, ~15 deg)
    float maxDirectionDiff = 0.40f;      // Max difference on relative minutiae directions (rad, ~23 deg)
};

/**
 * Candidate minutia correspondence pair generated from local triplet matching.
 */
struct CandidatePair {
    int probeIndex = -1;
    int candidateIndex = -1;
    float similarity = 0.0f;

    bool operator==(const CandidatePair& o) const {
        return probeIndex == o.probeIndex && candidateIndex == o.candidateIndex;
    }
};

/**
 * Result of Stage 1 local triplet matching.
 * Preserves evidence count (matchedTriplets) distinct from unique candidate pairs.
 */
struct CandidatePool {
    int matchedTriplets = 0;                     // Total number of compatible triplet matches found
    std::vector<CandidatePair> candidatePairs;   // Deduplicated candidate minutiae pairs
    bool isViable = false;                       // True if matchedTriplets >= minTripletsRequired (typically 4)
};

class KIPSMA_FP_API TripletMatcher {
public:
    explicit TripletMatcher(TripletMatcherParams params = TripletMatcherParams{});

    /**
     * Stage 1: Local triplet matching following OpenAFIS methodology.
     * Evaluates compatible side lengths and relative minutiae orientations,
     * enumerates vertex correspondences, and accumulates deduplicated candidate pairs.
     * Requires at least minTripletsRequired (default 4) compatible triplets for viability.
     */
    CandidatePool generateCandidatePool(
        const FingerprintTemplate& probeTpl,
        const std::vector<DelaunayTriplet>& probeTriplets,
        const FingerprintTemplate& candTpl,
        const std::vector<DelaunayTriplet>& candTriplets,
        int minTripletsRequired = 4) const;

    /**
     * Finds compatible triplet correspondences between probe and candidate Delaunay triangulations.
     * Evaluates all 6 vertex permutations explicitly to correctly handle isosceles / near-equilateral
     * ambiguity and arbitrary index assignments.
     */
    std::vector<TripletMatchHypothesis> matchTriplets(
        const FingerprintTemplate& probeTpl,
        const std::vector<DelaunayTriplet>& probeTriplets,
        const FingerprintTemplate& candTpl,
        const std::vector<DelaunayTriplet>& candTriplets) const;

    /**
     * Estimates rigid transformation SE(2) from 3 minutiae point correspondences via Kabsch.
     */
    static bool estimateRigidTransform(
        const FingerprintTemplate& probeTpl,
        const std::array<int, 3>& probeIndices,
        const FingerprintTemplate& candTpl,
        const std::array<int, 3>& candIndices,
        float& outTheta, float& outTx, float& outTy);

    const TripletMatcherParams& params() const { return params_; }
    void setParams(const TripletMatcherParams& p) { params_ = p; }

private:
    TripletMatcherParams params_;
};

} // namespace kipsma::fingerprint
