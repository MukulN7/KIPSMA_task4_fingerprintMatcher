#pragma once

#include "export.h"
#include "types.h"

#include <opencv2/core.hpp>
#include <vector>

namespace kipsma::fingerprint {

struct AlignmentTransform {
    float theta = 0.0f;    // Estimated rotation in radians [-PI, PI]
    float tx = 0.0f;       // Estimated translation X in pixels
    float ty = 0.0f;       // Estimated translation Y in pixels
};

enum class MatcherStrategy {
    DelaunayTripletBaseline,       // Milestone B Delaunay triplet + pivot verification (baseline)
    LocalNeighborhoodConsensus     // Milestone 4 Local K-NN descriptor + geometric consensus
};

struct MatcherParams {
    MatcherStrategy strategy = MatcherStrategy::DelaunayTripletBaseline;

    // Local edge descriptor parameters
    int maxNeighbors = 8;              // K nearest neighbors for local descriptor
    float minNeighborRadius = 15.0f;   // Min distance to avoid adjacent cluster noise
    float maxNeighborRadius = 120.0f;  // Radius for local neighborhood in pixels
    float descDistTolerance = 8.0f;    // Distance tolerance for edge matching in pixels
    float descAngleTolerance = 0.48f;  // Orientation angle tolerance in radians (~27.5 deg)
    int minClusterConsensus = 3;       // Minimum agreeing edges to accept alignment cluster

    // Global spatial-angular gating
    float maxSpatialDistance = 12.0f;  // Max Euclidean distance between aligned minutiae in pixels
    float maxAngleDifference = 0.48f;  // Max orientation difference in radians (~27.5 deg)
    float maxRotation = 0.75f;         // Max allowed sensor rotation in radians (~43 deg)
    float maxTranslation = 200.0f;     // Max allowed translation in pixels

    // Verification threshold and decision gating
    float matchThreshold = 25.0f;      // Score threshold for binary match decision [0, 100]
    int minMatchedPairs = 6;           // Minimum matched pairs required to consider match
    int maxHypothesesToTest = 12;      // Max alignment clusters to evaluate globally
};

class KIPSMA_FP_API MinutiaeMatcher {
public:
    explicit MinutiaeMatcher(MatcherParams params = MatcherParams{});

    // 1:1 Matching: compares two minutiae templates and returns match score
    MatchResult match(const FingerprintTemplate& probe, const FingerprintTemplate& candidate) const;

    // Rigid alignment estimation: finds optimal rigid transform (rotation, translation)
    bool estimateAlignment(const FingerprintTemplate& probe,
                           const FingerprintTemplate& candidate,
                           AlignmentTransform& outTransform) const;

    // Visualization: creates a side-by-side match overlay image
    cv::Mat visualizeMatch(const cv::Mat& probeImg,
                           const cv::Mat& candImg,
                           const FingerprintTemplate& probeTpl,
                           const FingerprintTemplate& candTpl,
                           const MatchResult& result) const;

    const MatcherParams& params() const { return params_; }
    void setParams(const MatcherParams& p) { params_ = p; }

private:
    MatchResult matchDelaunayTriplet(const FingerprintTemplate& probe, const FingerprintTemplate& candidate) const;
    MatchResult matchLocalNeighborhood(const FingerprintTemplate& probe, const FingerprintTemplate& candidate) const;
    bool estimateAlignmentDelaunay(const FingerprintTemplate& probe, const FingerprintTemplate& candidate, AlignmentTransform& outTransform) const;
    bool estimateAlignmentLocalNeighborhood(const FingerprintTemplate& probe, const FingerprintTemplate& candidate, AlignmentTransform& outTransform) const;

    MatcherParams params_;
};

} // namespace kipsma::fingerprint
