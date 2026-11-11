#include "kipsma_fingerprint/triplet.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace kipsma::fingerprint {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float normalizeAngle(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

inline float dist2d(float x1, float y1, float x2, float y2) {
    const float dx = x1 - x2;
    const float dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

inline float minutiaAngleDiff(float a, float b,
                              MinutiaType typeA = MinutiaType::Unknown,
                              MinutiaType typeB = MinutiaType::Unknown) {
    const float rawDiff = normalizeAngle(a - b);
    float d = std::abs(rawDiff);
    if (typeA == MinutiaType::Bifurcation && typeB == MinutiaType::Bifurcation) {
        const float dBranch1 = std::abs(normalizeAngle(rawDiff - (kTwoPi / 3.0f)));
        const float dBranch2 = std::abs(normalizeAngle(rawDiff + (kTwoPi / 3.0f)));
        d = std::min({d, dBranch1, dBranch2});
    }
    return d;
}

// Find nearest minutia index for a given Subdiv2D point
int findMinutiaIndex(const cv::Point2f& pt, const std::vector<Minutia>& minutiae, float tol = 0.5f) {
    int bestIdx = -1;
    float bestD = tol * tol;
    for (size_t i = 0; i < minutiae.size(); ++i) {
        const float dx = minutiae[i].x - pt.x;
        const float dy = minutiae[i].y - pt.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= bestD) {
            bestD = d2;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

} // namespace

DelaunayTripletExtractor::DelaunayTripletExtractor(TripletExtractorParams params)
    : params_(params) {}

std::vector<DelaunayTriplet> DelaunayTripletExtractor::extractTriplets(const FingerprintTemplate& tpl) const {
    std::vector<DelaunayTriplet> result;
    if (tpl.size() < 3) {
        return result;
    }

    // 1. Deduplicate / collect valid points with original indices
    struct UniquePt {
        float x;
        float y;
        int origIdx;
    };
    std::vector<UniquePt> uniquePts;
    uniquePts.reserve(tpl.size());

    float minX = 1e9f, maxX = -1e9f;
    float minY = 1e9f, maxY = -1e9f;

    for (size_t i = 0; i < tpl.size(); ++i) {
        const auto& m = tpl.minutiae[i];
        if (std::isnan(m.x) || std::isnan(m.y)) continue;

        // Check for duplicates within 0.75 px
        bool isDupe = false;
        for (const auto& u : uniquePts) {
            if (std::abs(u.x - m.x) < 0.75f && std::abs(u.y - m.y) < 0.75f) {
                isDupe = true;
                break;
            }
        }
        if (!isDupe) {
            uniquePts.push_back({m.x, m.y, static_cast<int>(i)});
            minX = std::min(minX, m.x);
            maxX = std::max(maxX, m.x);
            minY = std::min(minY, m.y);
            maxY = std::max(maxY, m.y);
        }
    }

    if (uniquePts.size() < 3) {
        return result;
    }

    // 2. Setup Subdiv2D bounding box safely covering all points plus margin
    const float margin = 50.0f;
    const float rectX = std::max(0.0f, minX - margin);
    const float rectY = std::max(0.0f, minY - margin);
    const float rectW = (maxX - minX) + 2.0f * margin + 100.0f;
    const float rectH = (maxY - minY) + 2.0f * margin + 100.0f;

    cv::Rect2f bbox(rectX, rectY, rectW, rectH);
    cv::Subdiv2D subdiv(bbox);

    for (const auto& u : uniquePts) {
        subdiv.insert(cv::Point2f(u.x, u.y));
    }

    // 3. Extract Delaunay triangle list
    std::vector<cv::Vec6f> triangleList;
    subdiv.getTriangleList(triangleList);

    // Map to identify unique triplets by sorted minutiae index
    std::vector<std::array<int, 3>> seenTriplets;
    seenTriplets.reserve(triangleList.size());

    for (const auto& t : triangleList) {
        const cv::Point2f p0(t[0], t[1]);
        const cv::Point2f p1(t[2], t[3]);
        const cv::Point2f p2(t[4], t[5]);

        // Points must be inside original bounding region (excluding subdiv virtual boundary vertices)
        if (p0.x < minX - 1.0f || p0.x > maxX + 1.0f || p0.y < minY - 1.0f || p0.y > maxY + 1.0f) continue;
        if (p1.x < minX - 1.0f || p1.x > maxX + 1.0f || p1.y < minY - 1.0f || p1.y > maxY + 1.0f) continue;
        if (p2.x < minX - 1.0f || p2.x > maxX + 1.0f || p2.y < minY - 1.0f || p2.y > maxY + 1.0f) continue;

        const int i0 = findMinutiaIndex(p0, tpl.minutiae);
        const int i1 = findMinutiaIndex(p1, tpl.minutiae);
        const int i2 = findMinutiaIndex(p2, tpl.minutiae);

        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 == i1 || i1 == i2 || i2 == i0) {
            continue;
        }

        // Check if triplet has already been processed
        std::array<int, 3> sortedIndices = {i0, i1, i2};
        std::sort(sortedIndices.begin(), sortedIndices.end());
        bool dupe = false;
        for (const auto& seen : seenTriplets) {
            if (seen == sortedIndices) {
                dupe = true;
                break;
            }
        }
        if (dupe) continue;
        seenTriplets.push_back(sortedIndices);

        // Minutiae objects
        const auto& m0 = tpl.minutiae[i0];
        const auto& m1 = tpl.minutiae[i1];
        const auto& m2 = tpl.minutiae[i2];

        // 4. Geometry & Side lengths
        // Side opposite to vertex 0 (m1 -> m2): s0
        // Side opposite to vertex 1 (m2 -> m0): s1
        // Side opposite to vertex 2 (m0 -> m1): s2
        const float s0 = dist2d(m1.x, m1.y, m2.x, m2.y);
        const float s1 = dist2d(m2.x, m2.y, m0.x, m0.y);
        const float s2 = dist2d(m0.x, m0.y, m1.x, m1.y);

        if (s0 < params_.minSideLength || s1 < params_.minSideLength || s2 < params_.minSideLength) continue;
        if (s0 > params_.maxSideLength || s1 > params_.maxSideLength || s2 > params_.maxSideLength) continue;

        // Area via cross product
        const float crossArea = (m1.x - m0.x) * (m2.y - m0.y) - (m1.y - m0.y) * (m2.x - m0.x);
        const float area = 0.5f * std::abs(crossArea);
        if (area < params_.minTriangleArea) continue;

        // 5. Canonicalize vertices by sorting side lengths descending:
        // Let side[0] >= side[1] >= side[2]
        struct VertexSide {
            int minutiaIdx;
            float oppositeSide;
        };
        std::array<VertexSide, 3> vs = {{{i0, s0}, {i1, s1}, {i2, s2}}};
        std::sort(vs.begin(), vs.end(), [](const VertexSide& a, const VertexSide& b) {
            return a.oppositeSide > b.oppositeSide;
        });

        // Canonical vertex 0 is opposite to longest side (vs[0].minutiaIdx)
        // Canonical vertex 1 is opposite to middle side (vs[1].minutiaIdx)
        // Canonical vertex 2 is opposite to shortest side (vs[2].minutiaIdx)
        const int c0 = vs[0].minutiaIdx;
        const int c1 = vs[1].minutiaIdx;
        const int c2 = vs[2].minutiaIdx;

        const auto& mc0 = tpl.minutiae[c0];
        const auto& mc1 = tpl.minutiae[c1];
        const auto& mc2 = tpl.minutiae[c2];

        const float l0 = vs[0].oppositeSide; // longest
        const float l1 = vs[1].oppositeSide; // middle
        const float l2 = vs[2].oppositeSide; // shortest

        // Compute internal angles using Law of Cosines
        auto calcAngle = [](float opp, float adj1, float adj2) {
            const float cosVal = std::clamp((adj1 * adj1 + adj2 * adj2 - opp * opp) / (2.0f * adj1 * adj2), -1.0f, 1.0f);
            return std::acos(cosVal);
        };
        const float ang0 = calcAngle(l0, l1, l2);
        const float ang1 = calcAngle(l1, l0, l2);
        const float ang2 = calcAngle(l2, l0, l1);

        // Relative minutiae angles with respect to edge directions
        // From c0 towards c1:
        const float dir01 = std::atan2(mc1.y - mc0.y, mc1.x - mc0.x);
        const float rel0 = normalizeAngle(mc0.angle - dir01);

        // From c1 towards c2:
        const float dir12 = std::atan2(mc2.y - mc1.y, mc2.x - mc1.x);
        const float rel1 = normalizeAngle(mc1.angle - dir12);

        // From c2 towards c0:
        const float dir20 = std::atan2(mc0.y - mc2.y, mc0.x - mc2.x);
        const float rel2 = normalizeAngle(mc2.angle - dir20);

        DelaunayTriplet trip;
        trip.minutiaeIndices = {c0, c1, c2};
        trip.sideLengths = {l0, l1, l2};
        trip.internalAngles = {ang0, ang1, ang2};
        trip.relativeAngles = {rel0, rel1, rel2};
        trip.area = area;
        trip.centroidX = (mc0.x + mc1.x + mc2.x) / 3.0f;
        trip.centroidY = (mc0.y + mc1.y + mc2.y) / 3.0f;

        result.push_back(trip);
    }

    return result;
}

TripletMatcher::TripletMatcher(TripletMatcherParams params)
    : params_(params) {}

bool TripletMatcher::estimateRigidTransform(
    const FingerprintTemplate& probeTpl,
    const std::array<int, 3>& probeIndices,
    const FingerprintTemplate& candTpl,
    const std::array<int, 3>& candIndices,
    float& outTheta, float& outTx, float& outTy) {

    float meanPx = 0.0f, meanPy = 0.0f;
    float meanCx = 0.0f, meanCy = 0.0f;

    for (int k = 0; k < 3; ++k) {
        const auto& p = probeTpl.minutiae[probeIndices[k]];
        const auto& c = candTpl.minutiae[candIndices[k]];
        meanPx += p.x;
        meanPy += p.y;
        meanCx += c.x;
        meanCy += c.y;
    }
    meanPx /= 3.0f;
    meanPy /= 3.0f;
    meanCx /= 3.0f;
    meanCy /= 3.0f;

    float Sxx = 0.0f, Sxy = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const auto& p = probeTpl.minutiae[probeIndices[k]];
        const auto& c = candTpl.minutiae[candIndices[k]];
        const float dxP = p.x - meanPx;
        const float dyP = p.y - meanPy;
        const float dxC = c.x - meanCx;
        const float dyC = c.y - meanCy;

        Sxx += (dxP * dxC + dyP * dyC);
        Sxy += (dxP * dyC - dyP * dxC);
    }

    const float norm = std::sqrt(Sxx * Sxx + Sxy * Sxy);
    if (norm < 1e-4f) {
        outTheta = 0.0f;
        outTx = meanCx - meanPx;
        outTy = meanCy - meanPy;
        return false;
    }

    outTheta = normalizeAngle(std::atan2(Sxy, Sxx));
    const float cosT = std::cos(outTheta);
    const float sinT = std::sin(outTheta);
    outTx = meanCx - (meanPx * cosT - meanPy * sinT);
    outTy = meanCy - (meanPx * sinT + meanPy * cosT);
    return true;
}

std::vector<TripletMatchHypothesis> TripletMatcher::matchTriplets(
    const FingerprintTemplate& probeTpl,
    const std::vector<DelaunayTriplet>& probeTriplets,
    const FingerprintTemplate& candTpl,
    const std::vector<DelaunayTriplet>& candTriplets) const {

    std::vector<TripletMatchHypothesis> hypotheses;
    if (probeTriplets.empty() || candTriplets.empty()) {
        return hypotheses;
    }

    // All 6 possible permutations of mapping probe vertices (0, 1, 2) to candidate vertices
    const std::array<std::array<int, 3>, 6> permutations = {{
        {0, 1, 2},
        {0, 2, 1},
        {1, 0, 2},
        {1, 2, 0},
        {2, 0, 1},
        {2, 1, 0}
    }};

    for (size_t pi = 0; pi < probeTriplets.size(); ++pi) {
        const auto& pt = probeTriplets[pi];
        const auto& p0 = probeTpl.minutiae[pt.minutiaeIndices[0]];
        const auto& p1 = probeTpl.minutiae[pt.minutiaeIndices[1]];
        const auto& p2 = probeTpl.minutiae[pt.minutiaeIndices[2]];

        // Compute actual probe side lengths between minutiae (0-1), (1-2), (2-0)
        const float pDist01 = dist2d(p0.x, p0.y, p1.x, p1.y);
        const float pDist12 = dist2d(p1.x, p1.y, p2.x, p2.y);
        const float pDist20 = dist2d(p2.x, p2.y, p0.x, p0.y);

        for (size_t ci = 0; ci < candTriplets.size(); ++ci) {
            const auto& ct = candTriplets[ci];

            // Coarse filter: compare sorted side lengths bounding box
            const float maxSideDiff = std::max({
                std::abs(pt.sideLengths[0] - ct.sideLengths[0]),
                std::abs(pt.sideLengths[1] - ct.sideLengths[1]),
                std::abs(pt.sideLengths[2] - ct.sideLengths[2])
            });
            if (maxSideDiff > params_.maxSideLengthDiff * 1.5f) {
                continue;
            }

            const auto& c0 = candTpl.minutiae[ct.minutiaeIndices[0]];
            const auto& c1 = candTpl.minutiae[ct.minutiaeIndices[1]];
            const auto& c2 = candTpl.minutiae[ct.minutiaeIndices[2]];

            const std::array<const Minutia*, 3> cMinutiae = {&c0, &c1, &c2};

            // Evaluate all 6 vertex permutations
            for (const auto& perm : permutations) {
                const auto& mc0 = *cMinutiae[perm[0]];
                const auto& mc1 = *cMinutiae[perm[1]];
                const auto& mc2 = *cMinutiae[perm[2]];

                // Corresponding candidate side lengths
                const float cDist01 = dist2d(mc0.x, mc0.y, mc1.x, mc1.y);
                const float cDist12 = dist2d(mc1.x, mc1.y, mc2.x, mc2.y);
                const float cDist20 = dist2d(mc2.x, mc2.y, mc0.x, mc0.y);

                const float diff01 = std::abs(pDist01 - cDist01);
                const float diff12 = std::abs(pDist12 - cDist12);
                const float diff20 = std::abs(pDist20 - cDist20);

                const float tol01 = std::max(params_.maxSideLengthDiff, std::max(pDist01, cDist01) * params_.maxSideRatioDiff);
                const float tol12 = std::max(params_.maxSideLengthDiff, std::max(pDist12, cDist12) * params_.maxSideRatioDiff);
                const float tol20 = std::max(params_.maxSideLengthDiff, std::max(pDist20, cDist20) * params_.maxSideRatioDiff);

                if (diff01 > tol01 || diff12 > tol12 || diff20 > tol20) {
                    continue;
                }

                // Check triangle chirality / orientation (rigid transforms preserve triangle vertex order orientation)
                const float crossP = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
                const float crossC = (mc1.x - mc0.x) * (mc2.y - mc0.y) - (mc1.y - mc0.y) * (mc2.x - mc0.x);
                if (crossP * crossC <= 0.0f) {
                    // Reflected triangle: not achievable via SE(2) rigid rotation/translation
                    continue;
                }

                // Check relative minutiae orientations with respect to corresponding edges
                const float dirP01 = std::atan2(p1.y - p0.y, p1.x - p0.x);
                const float dirC01 = std::atan2(mc1.y - mc0.y, mc1.x - mc0.x);
                const float relP0 = normalizeAngle(p0.angle - dirP01);
                const float relC0 = normalizeAngle(mc0.angle - dirC01);

                const float dirP12 = std::atan2(p2.y - p1.y, p2.x - p1.x);
                const float dirC12 = std::atan2(mc2.y - mc1.y, mc2.x - mc1.x);
                const float relP1 = normalizeAngle(p1.angle - dirP12);
                const float relC1 = normalizeAngle(mc1.angle - dirC12);

                const float dirP20 = std::atan2(p0.y - p2.y, p0.x - p2.x);
                const float dirC20 = std::atan2(mc0.y - mc2.y, mc0.x - mc2.x);
                const float relP2 = normalizeAngle(p2.angle - dirP20);
                const float relC2 = normalizeAngle(mc2.angle - dirC20);

                const float diffAng0 = minutiaAngleDiff(relP0, relC0, p0.type, mc0.type);
                const float diffAng1 = minutiaAngleDiff(relP1, relC1, p1.type, mc1.type);
                const float diffAng2 = minutiaAngleDiff(relP2, relC2, p2.type, mc2.type);

                if (diffAng0 > params_.maxDirectionDiff ||
                    diffAng1 > params_.maxDirectionDiff ||
                    diffAng2 > params_.maxDirectionDiff) {
                    continue;
                }

                // Estimate rigid transformation
                const std::array<int, 3> pIndices = pt.minutiaeIndices;
                const std::array<int, 3> cIndices = {
                    ct.minutiaeIndices[perm[0]],
                    ct.minutiaeIndices[perm[1]],
                    ct.minutiaeIndices[perm[2]]
                };

                float estTheta = 0.0f, estTx = 0.0f, estTy = 0.0f;
                if (!estimateRigidTransform(probeTpl, pIndices, candTpl, cIndices, estTheta, estTx, estTy)) {
                    continue;
                }

                // Verify residual distance error across the 3 triplet vertices
                const float cosT = std::cos(estTheta);
                const float sinT = std::sin(estTheta);
                float tripletRms = 0.0f;
                const std::array<const Minutia*, 3> pMinArray = {&p0, &p1, &p2};
                const std::array<const Minutia*, 3> cMinArray = {&mc0, &mc1, &mc2};

                for (int v = 0; v < 3; ++v) {
                    const float pxAl = pMinArray[v]->x * cosT - pMinArray[v]->y * sinT + estTx;
                    const float pyAl = pMinArray[v]->x * sinT + pMinArray[v]->y * cosT + estTy;
                    const float d = dist2d(pxAl, pyAl, cMinArray[v]->x, cMinArray[v]->y);
                    tripletRms += d * d;
                }
                tripletRms = std::sqrt(tripletRms / 3.0f);

                if (tripletRms > params_.maxSideLengthDiff) {
                    continue;
                }

                // Hypothesis score (lower residual = higher score)
                const float sideResidual = (diff01 + diff12 + diff20) / 3.0f;
                const float angleResidual = (diffAng0 + diffAng1 + diffAng2) / 3.0f;
                const float score = 1.0f / (1.0f + tripletRms * 0.4f + sideResidual * 0.3f + angleResidual * 10.0f);

                TripletMatchHypothesis hyp;
                hyp.probeTripletIdx = static_cast<int>(pi);
                hyp.candidateTripletIdx = static_cast<int>(ci);
                hyp.probeMinutiae = pIndices;
                hyp.candidateMinutiae = cIndices;
                hyp.theta = estTheta;
                hyp.tx = estTx;
                hyp.ty = estTy;
                hyp.score = score;

                hypotheses.push_back(hyp);
            }
        }
    }

    // Sort hypotheses descending by similarity score
    std::sort(hypotheses.begin(), hypotheses.end(), [](const TripletMatchHypothesis& a, const TripletMatchHypothesis& b) {
        return a.score > b.score;
    });

    return hypotheses;
}

CandidatePool TripletMatcher::generateCandidatePool(
    const FingerprintTemplate& probeTpl,
    const std::vector<DelaunayTriplet>& probeTriplets,
    const FingerprintTemplate& candTpl,
    const std::vector<DelaunayTriplet>& candTriplets,
    int minTripletsRequired) const {

    CandidatePool pool;
    if (probeTriplets.empty() || candTriplets.empty()) {
        return pool;
    }

    // All 6 possible permutations of mapping probe vertices (0, 1, 2) to candidate vertices
    const std::array<std::array<int, 3>, 6> permutations = {{
        {0, 1, 2},
        {0, 2, 1},
        {1, 0, 2},
        {1, 2, 0},
        {2, 0, 1},
        {2, 1, 0}
    }};

    // Track triplet pair matches and candidate minutia pairings
    struct TripletPairMatch {
        float similarity;
        std::array<int, 3> probeMinutiae;
        std::array<int, 3> candMinutiae;
    };
    std::vector<TripletPairMatch> matchedTripletPairs;

    for (size_t pi = 0; pi < probeTriplets.size(); ++pi) {
        const auto& pt = probeTriplets[pi];
        const auto& p0 = probeTpl.minutiae[pt.minutiaeIndices[0]];
        const auto& p1 = probeTpl.minutiae[pt.minutiaeIndices[1]];
        const auto& p2 = probeTpl.minutiae[pt.minutiaeIndices[2]];

        const float pDist01 = dist2d(p0.x, p0.y, p1.x, p1.y);
        const float pDist12 = dist2d(p1.x, p1.y, p2.x, p2.y);
        const float pDist20 = dist2d(p2.x, p2.y, p0.x, p0.y);

        for (size_t ci = 0; ci < candTriplets.size(); ++ci) {
            const auto& ct = candTriplets[ci];

            // Coarse filter on sorted side lengths
            const float maxSideDiff = std::max({
                std::abs(pt.sideLengths[0] - ct.sideLengths[0]),
                std::abs(pt.sideLengths[1] - ct.sideLengths[1]),
                std::abs(pt.sideLengths[2] - ct.sideLengths[2])
            });
            if (maxSideDiff > params_.maxSideLengthDiff * 1.5f) {
                continue;
            }

            const auto& c0 = candTpl.minutiae[ct.minutiaeIndices[0]];
            const auto& c1 = candTpl.minutiae[ct.minutiaeIndices[1]];
            const auto& c2 = candTpl.minutiae[ct.minutiaeIndices[2]];
            const std::array<const Minutia*, 3> cMinutiae = {&c0, &c1, &c2};

            float bestTripletSimilarity = -1.0f;
            std::array<int, 3> bestProbeIndices = pt.minutiaeIndices;
            std::array<int, 3> bestCandIndices = {-1, -1, -1};

            for (const auto& perm : permutations) {
                const auto& mc0 = *cMinutiae[perm[0]];
                const auto& mc1 = *cMinutiae[perm[1]];
                const auto& mc2 = *cMinutiae[perm[2]];

                const float cDist01 = dist2d(mc0.x, mc0.y, mc1.x, mc1.y);
                const float cDist12 = dist2d(mc1.x, mc1.y, mc2.x, mc2.y);
                const float cDist20 = dist2d(mc2.x, mc2.y, mc0.x, mc0.y);

                const float diff01 = std::abs(pDist01 - cDist01);
                const float diff12 = std::abs(pDist12 - cDist12);
                const float diff20 = std::abs(pDist20 - cDist20);

                const float tol01 = std::max(params_.maxSideLengthDiff, std::max(pDist01, cDist01) * params_.maxSideRatioDiff);
                const float tol12 = std::max(params_.maxSideLengthDiff, std::max(pDist12, cDist12) * params_.maxSideRatioDiff);
                const float tol20 = std::max(params_.maxSideLengthDiff, std::max(pDist20, cDist20) * params_.maxSideRatioDiff);

                if (diff01 > tol01 || diff12 > tol12 || diff20 > tol20) {
                    continue;
                }

                // Preserves chirality (non-reflected triangles)
                const float crossP = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
                const float crossC = (mc1.x - mc0.x) * (mc2.y - mc0.y) - (mc1.y - mc0.y) * (mc2.x - mc0.x);
                if (crossP * crossC <= 0.0f) {
                    continue;
                }

                // Check relative minutiae orientations with respect to corresponding edges
                const float dirP01 = std::atan2(p1.y - p0.y, p1.x - p0.x);
                const float dirC01 = std::atan2(mc1.y - mc0.y, mc1.x - mc0.x);
                const float relP0 = normalizeAngle(p0.angle - dirP01);
                const float relC0 = normalizeAngle(mc0.angle - dirC01);

                const float dirP12 = std::atan2(p2.y - p1.y, p2.x - p1.x);
                const float dirC12 = std::atan2(mc2.y - mc1.y, mc2.x - mc1.x);
                const float relP1 = normalizeAngle(p1.angle - dirP12);
                const float relC1 = normalizeAngle(mc1.angle - dirC12);

                const float dirP20 = std::atan2(p0.y - p2.y, p0.x - p2.x);
                const float dirC20 = std::atan2(mc0.y - mc2.y, mc0.x - mc2.x);
                const float relP2 = normalizeAngle(p2.angle - dirP20);
                const float relC2 = normalizeAngle(mc2.angle - dirC20);

                const float diffAng0 = minutiaAngleDiff(relP0, relC0, p0.type, mc0.type);
                const float diffAng1 = minutiaAngleDiff(relP1, relC1, p1.type, mc1.type);
                const float diffAng2 = minutiaAngleDiff(relP2, relC2, p2.type, mc2.type);

                if (diffAng0 > params_.maxDirectionDiff ||
                    diffAng1 > params_.maxDirectionDiff ||
                    diffAng2 > params_.maxDirectionDiff) {
                    continue;
                }

                const float sideResidual = (diff01 + diff12 + diff20) / 3.0f;
                const float angleResidual = (diffAng0 + diffAng1 + diffAng2) / 3.0f;
                const float sim = 1.0f / (1.0f + sideResidual * 0.2f + angleResidual * 4.0f);

                if (sim > bestTripletSimilarity) {
                    bestTripletSimilarity = sim;
                    bestCandIndices = {
                        ct.minutiaeIndices[perm[0]],
                        ct.minutiaeIndices[perm[1]],
                        ct.minutiaeIndices[perm[2]]
                    };
                }
            }

            if (bestTripletSimilarity > 0.0f) {
                matchedTripletPairs.push_back({bestTripletSimilarity, bestProbeIndices, bestCandIndices});
            }
        }
    }

    // Evidence count: number of compatible triplet matches
    pool.matchedTriplets = static_cast<int>(matchedTripletPairs.size());
    pool.isViable = (pool.matchedTriplets >= minTripletsRequired);

    if (!pool.isViable) {
        return pool;
    }

    // Sort matched triplet pairs descending by similarity (OpenAFIS Section 5.1.3)
    std::sort(matchedTripletPairs.begin(), matchedTripletPairs.end(),
              [](const TripletPairMatch& a, const TripletPairMatch& b) {
                  return a.similarity > b.similarity;
              });

    // Accumulate and deduplicate candidate (probeIndex, candidateIndex) pairs (OpenAFIS Section 5.1.4-5)
    std::vector<bool> probePaired(probeTpl.size(), false);
    std::vector<bool> candPaired(candTpl.size(), false);

    for (const auto& mt : matchedTripletPairs) {
        for (int v = 0; v < 3; ++v) {
            const int pIdx = mt.probeMinutiae[v];
            const int cIdx = mt.candMinutiae[v];

            if (pIdx < 0 || pIdx >= static_cast<int>(probeTpl.size()) ||
                cIdx < 0 || cIdx >= static_cast<int>(candTpl.size())) {
                continue;
            }

            // Deduplicate: avoid adding identical (probe, candidate) pairing multiple times
            bool alreadyExists = false;
            for (const auto& cp : pool.candidatePairs) {
                if (cp.probeIndex == pIdx && cp.candidateIndex == cIdx) {
                    alreadyExists = true;
                    break;
                }
            }

            if (!alreadyExists) {
                pool.candidatePairs.push_back({pIdx, cIdx, mt.similarity});
            }
        }
    }

    return pool;
}

} // namespace kipsma::fingerprint


