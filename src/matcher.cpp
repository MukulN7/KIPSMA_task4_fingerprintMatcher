#include "kipsma_fingerprint/matcher.h"
#include "kipsma_fingerprint/triplet.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace kipsma::fingerprint {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float normalizeAngle(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

inline float angleDiff(float a, float b) {
    return std::abs(normalizeAngle(a - b));
}

inline float minutiaAngleDiff(float a, float b,
                              MinutiaType typeA = MinutiaType::Unknown,
                              MinutiaType typeB = MinutiaType::Unknown) {
    const float rawDiff = normalizeAngle(a - b);
    float d = std::abs(rawDiff);
    // Bifurcations have 3 branches separated by ~120 degrees (2*pi/3).
    // Under varying pressure, noise, and skeleton tracing, branch designation flips by +-120 deg.
    // Account for 3-fold branch ambiguity only when comparing two bifurcations.
    if (typeA == MinutiaType::Bifurcation && typeB == MinutiaType::Bifurcation) {
        const float dBranch1 = std::abs(normalizeAngle(rawDiff - (kTwoPi / 3.0f)));
        const float dBranch2 = std::abs(normalizeAngle(rawDiff + (kTwoPi / 3.0f)));
        d = std::min({d, dBranch1, dBranch2});
    }
    return d;
}

// Evaluates minutia orientation difference with validated 3-branch and 180-deg ridge ambiguity
inline float validatedMinutiaAngleDiff(float a, float b,
                                      MinutiaType typeA = MinutiaType::Unknown,
                                      MinutiaType typeB = MinutiaType::Unknown,
                                      float maxAngle = 0.40f) {
    (void)typeA;
    (void)typeB;
    const float rawDiff = std::abs(normalizeAngle(a - b));
    const float piDiff = std::abs(rawDiff - kPi);
    return std::min(rawDiff, piDiff);
}

inline float dist2d(float x1, float y1, float x2, float y2) {
    const float dx = x1 - x2;
    const float dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

struct MinutiaEdge {
    int i = -1;
    int j = -1;
    float length = 0.0f;
    float dirAngle = 0.0f;
    float betaI = 0.0f;
    float betaJ = 0.0f;
    float midX = 0.0f;
    float midY = 0.0f;
};

// Build spatial minutiae edge descriptors (Bozorth3-style edge relations)
std::vector<MinutiaEdge> buildEdges(const FingerprintTemplate& tpl,
                                    int maxNeighbors,
                                    float minRadius,
                                    float maxRadius) {
    std::vector<MinutiaEdge> edges;
    const size_t N = tpl.minutiae.size();
    if (N < 2) return edges;

    edges.reserve(N * maxNeighbors);

    for (size_t i = 0; i < N; ++i) {
        const auto& mi = tpl.minutiae[i];

        struct NeighborCand {
            int idx;
            float dist;
        };
        std::vector<NeighborCand> cands;
        cands.reserve(N);

        for (size_t j = 0; j < N; ++j) {
            if (i == j) continue;
            const float d = dist2d(mi.x, mi.y, tpl.minutiae[j].x, tpl.minutiae[j].y);
            if (d >= minRadius && d <= maxRadius) {
                cands.push_back({static_cast<int>(j), d});
            }
        }

        std::sort(cands.begin(), cands.end(), [](const NeighborCand& a, const NeighborCand& b) {
            return a.dist < b.dist;
        });

        const size_t count = std::min(cands.size(), static_cast<size_t>(maxNeighbors));
        for (size_t k = 0; k < count; ++k) {
            const int j = cands[k].idx;
            const auto& mj = tpl.minutiae[j];
            const float dx = mj.x - mi.x;
            const float dy = mj.y - mi.y;
            const float dir = std::atan2(dy, dx);
            const float bI = normalizeAngle(mi.angle - dir);
            const float bJ = normalizeAngle(mj.angle - dir);

            edges.push_back({static_cast<int>(i), j, cands[k].dist, dir, bI, bJ,
                             (mi.x + mj.x) * 0.5f, (mi.y + mj.y) * 0.5f});
        }
    }

    return edges;
}

// Evaluate rigid alignment and find greedy 1:1 matched minutiae pairs
std::vector<MatchPair> evaluateAlignment(const FingerprintTemplate& probe,
                                         const FingerprintTemplate& candidate,
                                         float theta,
                                         float tx,
                                         float ty,
                                         float maxDist,
                                         float maxAngle) {
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);

    struct CandPair {
        int pIdx;
        int cIdx;
        float dist;
        float aDiff;
        float cost;
    };
    std::vector<CandPair> candidates;
    candidates.reserve(probe.minutiae.size() * 3);

    for (size_t i = 0; i < probe.minutiae.size(); ++i) {
        const auto& p = probe.minutiae[i];
        const float pxAl = p.x * cosT - p.y * sinT + tx;
        const float pyAl = p.x * sinT + p.y * cosT + ty;
        const float pAngleAl = normalizeAngle(p.angle + theta);

        for (size_t j = 0; j < candidate.minutiae.size(); ++j) {
            const auto& c = candidate.minutiae[j];
            const float d = dist2d(pxAl, pyAl, c.x, c.y);
            if (d > maxDist) continue;

            const float dAngle = validatedMinutiaAngleDiff(pAngleAl, c.angle, p.type, c.type, maxAngle);
            if (dAngle > maxAngle) continue;

            float typePenalty = 0.0f;
            if (p.type != MinutiaType::Unknown && c.type != MinutiaType::Unknown && p.type != c.type) {
                typePenalty = 0.15f;
            }

            const float cost = (d / maxDist) * 0.55f + (dAngle / maxAngle) * 0.35f + typePenalty;
            candidates.push_back({static_cast<int>(i), static_cast<int>(j), d, dAngle, cost});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const CandPair& a, const CandPair& b) {
        return a.cost < b.cost;
    });

    std::vector<bool> pUsed(probe.minutiae.size(), false);
    std::vector<bool> cUsed(candidate.minutiae.size(), false);
    std::vector<MatchPair> resultPairs;
    resultPairs.reserve(candidates.size());

    for (const auto& cp : candidates) {
        if (!pUsed[cp.pIdx] && !cUsed[cp.cIdx]) {
            pUsed[cp.pIdx] = true;
            cUsed[cp.cIdx] = true;
            resultPairs.push_back({cp.pIdx, cp.cIdx, cp.dist, cp.aDiff});
        }
    }

    return resultPairs;
}

// Evaluate rigid alignment strictly within a pre-filtered candidate pair pool (OpenAFIS methodology)
std::vector<MatchPair> evaluateAlignmentCandidatePool(const FingerprintTemplate& probe,
                                                      const FingerprintTemplate& candidate,
                                                      const std::vector<std::pair<int, int>>& candidatePool,
                                                      float theta,
                                                      float tx,
                                                      float ty,
                                                      float maxDist,
                                                      float maxAngle) {
    if (candidatePool.empty()) return {};

    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);

    struct ScoredPair {
        int pIdx;
        int cIdx;
        float dist;
        float aDiff;
        float cost;
    };
    std::vector<ScoredPair> candidates;
    candidates.reserve(candidatePool.size());

    for (const auto& cp : candidatePool) {
        const int i = cp.first;
        const int j = cp.second;
        if (i < 0 || i >= static_cast<int>(probe.minutiae.size())) continue;
        if (j < 0 || j >= static_cast<int>(candidate.minutiae.size())) continue;

        const auto& p = probe.minutiae[i];
        const auto& c = candidate.minutiae[j];

        const float pxAl = p.x * cosT - p.y * sinT + tx;
        const float pyAl = p.x * sinT + p.y * cosT + ty;
        const float d = dist2d(pxAl, pyAl, c.x, c.y);
        if (d > maxDist) continue;

        const float pAngleAl = normalizeAngle(p.angle + theta);
        const float dAngle = validatedMinutiaAngleDiff(pAngleAl, c.angle, p.type, c.type, maxAngle);
        if (dAngle > maxAngle) continue;

        float typePenalty = 0.0f;
        if (p.type != MinutiaType::Unknown && c.type != MinutiaType::Unknown && p.type != c.type) {
            typePenalty = 0.15f;
        }

        const float cost = (d / maxDist) * 0.55f + (dAngle / maxAngle) * 0.35f + typePenalty;
        candidates.push_back({i, j, d, dAngle, cost});
    }

    std::sort(candidates.begin(), candidates.end(), [](const ScoredPair& a, const ScoredPair& b) {
        return a.cost < b.cost;
    });

    std::vector<bool> pUsed(probe.minutiae.size(), false);
    std::vector<bool> cUsed(candidate.minutiae.size(), false);
    std::vector<MatchPair> resultPairs;
    resultPairs.reserve(candidates.size());

    for (const auto& sc : candidates) {
        if (!pUsed[sc.pIdx] && !cUsed[sc.cIdx]) {
            pUsed[sc.pIdx] = true;
            cUsed[sc.cIdx] = true;
            resultPairs.push_back({sc.pIdx, sc.cIdx, sc.dist, sc.aDiff});
        }
    }

    return resultPairs;
}

// Closed-form least squares rigid transform refinement (Kabsch)
void refineAlignment(const FingerprintTemplate& probe,
                     const FingerprintTemplate& candidate,
                     const std::vector<MatchPair>& pairs,
                     float& inOutTheta,
                     float& inOutTx,
                     float& inOutTy) {
    if (pairs.size() < 2) return;

    float meanPx = 0.0f, meanPy = 0.0f;
    float meanCx = 0.0f, meanCy = 0.0f;
    const float M = static_cast<float>(pairs.size());

    for (const auto& pr : pairs) {
        meanPx += probe.minutiae[pr.probeIndex].x;
        meanPy += probe.minutiae[pr.probeIndex].y;
        meanCx += candidate.minutiae[pr.candidateIndex].x;
        meanCy += candidate.minutiae[pr.candidateIndex].y;
    }
    meanPx /= M;
    meanPy /= M;
    meanCx /= M;
    meanCy /= M;

    float Sxx = 0.0f;
    float Sxy = 0.0f;

    for (const auto& pr : pairs) {
        const float dxP = probe.minutiae[pr.probeIndex].x - meanPx;
        const float dyP = probe.minutiae[pr.probeIndex].y - meanPy;
        const float dxC = candidate.minutiae[pr.candidateIndex].x - meanCx;
        const float dyC = candidate.minutiae[pr.candidateIndex].y - meanCy;

        Sxx += (dxP * dxC + dyP * dyC);
        Sxy += (dxP * dyC - dyP * dxC);
    }

    const float norm = std::sqrt(Sxx * Sxx + Sxy * Sxy);
    if (norm > 1e-4f) {
        inOutTheta = normalizeAngle(std::atan2(Sxy, Sxx));
    }

    const float cosT = std::cos(inOutTheta);
    const float sinT = std::sin(inOutTheta);
    inOutTx = meanCx - (meanPx * cosT - meanPy * sinT);
    inOutTy = meanCy - (meanPx * sinT + meanPy * cosT);
}

// Mutual distance consistency filter: prunes matches that violate rigid distance preservation
std::vector<MatchPair> filterMutualConsistency(const FingerprintTemplate& probe,
                                               const FingerprintTemplate& candidate,
                                               const std::vector<MatchPair>& rawPairs,
                                               float maxEdgeDistDiff,
                                               float& outConsistencyRatio) {
    const size_t M = rawPairs.size();
    if (M < 3) {
        outConsistencyRatio = (M >= 2) ? 1.0f : 0.0f;
        return rawPairs;
    }

    std::vector<int> consistentCounts(M, 0);
    int totalInterEdges = 0;
    int consistentInterEdges = 0;

    for (size_t a = 0; a < M; ++a) {
        const auto& pa = probe.minutiae[rawPairs[a].probeIndex];
        const auto& ca = candidate.minutiae[rawPairs[a].candidateIndex];
        for (size_t b = a + 1; b < M; ++b) {
            const auto& pb = probe.minutiae[rawPairs[b].probeIndex];
            const auto& cb = candidate.minutiae[rawPairs[b].candidateIndex];

            const float dP = dist2d(pa.x, pa.y, pb.x, pb.y);
            const float dC = dist2d(ca.x, ca.y, cb.x, cb.y);

            ++totalInterEdges;
            const float allowedDiff = std::max(maxEdgeDistDiff, std::max(dP, dC) * 0.08f);
            if (std::abs(dP - dC) <= allowedDiff) {
                // Also verify relative edge orientation angle consistency (rigid invariant)
                const float dirP = std::atan2(pb.y - pa.y, pb.x - pa.x);
                const float dirC = std::atan2(cb.y - ca.y, cb.x - ca.x);
                const float betaPa = normalizeAngle(pa.angle - dirP);
                const float betaCa = normalizeAngle(ca.angle - dirC);
                const float betaPb = normalizeAngle(pb.angle - dirP);
                const float betaCb = normalizeAngle(cb.angle - dirC);

                const float dBetaA = validatedMinutiaAngleDiff(betaPa, betaCa, pa.type, ca.type, 0.50f);
                const float dBetaB = validatedMinutiaAngleDiff(betaPb, betaCb, pb.type, cb.type, 0.50f);

                if (dBetaA <= 0.50f && dBetaB <= 0.50f) {
                    ++consistentCounts[a];
                    ++consistentCounts[b];
                    ++consistentInterEdges;
                }
            }
        }
    }

    outConsistencyRatio = (totalInterEdges > 0)
        ? (static_cast<float>(consistentInterEdges) / static_cast<float>(totalInterEdges))
        : 0.0f;

    // Retain minutiae consistent with at least 40% of other pairs (tolerant to elastic skin stretch)
    const int minRequiredConsistent = std::max(1, static_cast<int>(std::floor(static_cast<float>(M - 1) * 0.40f)));

    std::vector<MatchPair> filtered;
    filtered.reserve(M);
    for (size_t a = 0; a < M; ++a) {
        if (consistentCounts[a] >= minRequiredConsistent) {
            filtered.push_back(rawPairs[a]);
        }
    }

    if (outConsistencyRatio < 0.25f) {
        filtered.clear();
    }

    return filtered;
}

bool estimateAlignmentSmallTemplates(const FingerprintTemplate& probe,
                                     const FingerprintTemplate& candidate,
                                     AlignmentTransform& outTransform,
                                     const MatcherParams& params) {
    AlignmentTransform bestTf{};
    int bestCount = -1;
    float bestCost = 1e9f;

    for (size_t i = 0; i < probe.size(); ++i) {
        const auto& pi = probe.minutiae[i];
        for (size_t j = 0; j < candidate.size(); ++j) {
            const auto& cj = candidate.minutiae[j];
            const float th = normalizeAngle(cj.angle - pi.angle);
            const float cosT = std::cos(th);
            const float sinT = std::sin(th);
            const float tx = cj.x - (pi.x * cosT - pi.y * sinT);
            const float ty = cj.y - (pi.x * sinT + pi.y * cosT);

            auto pairs = evaluateAlignment(probe, candidate, th, tx, ty,
                                           params.maxSpatialDistance, params.maxAngleDifference);
            float cost = 0.0f;
            for (const auto& pr : pairs) cost += pr.distance;

            const int cnt = static_cast<int>(pairs.size());
            if (cnt > bestCount || (cnt == bestCount && cost < bestCost)) {
                bestCount = cnt;
                bestCost = cost;
                bestTf.theta = th;
                bestTf.tx = tx;
                bestTf.ty = ty;
            }
        }
    }
    outTransform = bestTf;
    return (bestCount > 0);
}

MatchResult computeMatchResult(const FingerprintTemplate& probe,
                               const FingerprintTemplate& candidate,
                               float rotation,
                               float tx,
                               float ty,
                               std::vector<MatchPair> pairs,
                               const MatcherParams& params) {
    MatchResult res;
    res.rotation = rotation;
    res.translationX = tx;
    res.translationY = ty;
    res.pairs = std::move(pairs);
    res.matchedPairs = static_cast<int>(res.pairs.size());

    if (res.matchedPairs == 0) {
        res.isMatch = false;
        res.score = 0.0f;
        res.rmsError = 0.0f;
        res.message = "Zero minutiae survived verification";
        return res;
    }

    float sumSqDist = 0.0f;
    float sumAngleDiff = 0.0f;
    for (const auto& pr : res.pairs) {
        sumSqDist += (pr.distance * pr.distance);
        sumAngleDiff += pr.angleDiff;
    }

    res.rmsError = std::sqrt(sumSqDist / static_cast<float>(res.matchedPairs));
    const float meanAngleDiff = sumAngleDiff / static_cast<float>(res.matchedPairs);

    const float cosT = std::cos(res.rotation);
    const float sinT = std::sin(res.rotation);

    float minPx = 1e9f, maxPx = -1e9f, minPy = 1e9f, maxPy = -1e9f;
    for (const auto& p : probe.minutiae) {
        const float pxAl = p.x * cosT - p.y * sinT + res.translationX;
        const float pyAl = p.x * sinT + p.y * cosT + res.translationY;
        minPx = std::min(minPx, pxAl); maxPx = std::max(maxPx, pxAl);
        minPy = std::min(minPy, pyAl); maxPy = std::max(maxPy, pyAl);
    }

    float minCx = 1e9f, maxCx = -1e9f, minCy = 1e9f, maxCy = -1e9f;
    for (const auto& c : candidate.minutiae) {
        minCx = std::min(minCx, c.x); maxCx = std::max(maxCx, c.x);
        minCy = std::min(minCy, c.y); maxCy = std::max(maxCy, c.y);
    }

    const float overlapMinX = std::max(minPx, minCx);
    const float overlapMaxX = std::min(maxPx, maxCx);
    const float overlapMinY = std::max(minPy, minCy);
    const float overlapMaxY = std::min(maxPy, maxCy);

    int probeInOverlap = 0;
    for (const auto& p : probe.minutiae) {
        const float pxAl = p.x * cosT - p.y * sinT + res.translationX;
        const float pyAl = p.x * sinT + p.y * cosT + res.translationY;
        if (pxAl >= overlapMinX && pxAl <= overlapMaxX &&
            pyAl >= overlapMinY && pyAl <= overlapMaxY) {
            ++probeInOverlap;
        }
    }

    int candInOverlap = 0;
    for (const auto& c : candidate.minutiae) {
        if (c.x >= overlapMinX && c.x <= overlapMaxX &&
            c.y >= overlapMinY && c.y <= overlapMaxY) {
            ++candInOverlap;
        }
    }

    const int maxOverlapMinutiae = std::max(1, std::max(probeInOverlap, candInOverlap));
    const float overlapRatio = std::min(1.0f, static_cast<float>(res.matchedPairs) / static_cast<float>(maxOverlapMinutiae));

    const int effectiveMinPairs = std::min(params.minMatchedPairs,
                                           static_cast<int>(std::min(probe.size(), candidate.size())));

    const float M = static_cast<float>(res.matchedPairs);

    const float geomQuality = std::max(0.0f, 1.0f - (res.rmsError / params.maxSpatialDistance));
    const float angQuality = std::max(0.0f, 1.0f - (meanAngleDiff / params.maxAngleDifference));
    const float fitQuality = 0.60f * geomQuality + 0.40f * angQuality;

    const float overlapDenom = std::max(1.0f, static_cast<float>(std::max(M, static_cast<float>(probeInOverlap)) *
                                                                 std::max(M, static_cast<float>(candInOverlap))));
    const float baseScore = (M * M * 100.0f) / overlapDenom;

    float rawScore = baseScore * (0.60f * fitQuality + 0.40f * overlapRatio);

    if (overlapRatio > 0.85f && res.matchedPairs >= effectiveMinPairs) {
        rawScore = std::max(rawScore, 100.0f * overlapRatio * fitQuality);
    }

    if (res.matchedPairs < effectiveMinPairs) {
        const float penalty = M / static_cast<float>(effectiveMinPairs);
        rawScore *= (penalty * penalty);
    }

    res.score = std::clamp(rawScore, 0.0f, 100.0f);
    res.isMatch = (res.score >= params.matchThreshold && res.matchedPairs >= effectiveMinPairs);

    if (res.isMatch) {
        res.message = "Verified genuine match";
    } else if (res.matchedPairs < effectiveMinPairs) {
        res.message = "Non-match: Insufficient verified minutiae pairs";
    } else {
        res.message = "Non-match: Match score below decision threshold";
    }

    return res;
}

struct NeighborhoodConsensusResult {
    bool hasConsensus = false;
    AlignmentTransform transform{};
    std::vector<MatchPair> verifiedPairs;
};

NeighborhoodConsensusResult findLocalNeighborhoodConsensus(
    const FingerprintTemplate& probe,
    const FingerprintTemplate& candidate,
    const MatcherParams& params) {
    NeighborhoodConsensusResult res;
    if (probe.empty() || candidate.empty()) return res;

    if (probe.size() < 10 || candidate.size() < 10) {
        AlignmentTransform tf;
        if (!estimateAlignmentSmallTemplates(probe, candidate, tf, params)) {
            return res;
        }
        auto raw = evaluateAlignment(probe, candidate, tf.theta, tf.tx, tf.ty,
                                     params.maxSpatialDistance, params.maxAngleDifference);
        float dummy = 0.0f;
        auto ver = filterMutualConsistency(probe, candidate, raw, 9.0f, dummy);
        if (ver.size() >= 2) {
            res.hasConsensus = true;
            res.transform = tf;
            res.verifiedPairs = std::move(ver);
        }
        return res;
    }

    const auto edgesP = buildEdges(probe, params.maxNeighbors, params.minNeighborRadius, params.maxNeighborRadius);
    const auto edgesC = buildEdges(candidate, params.maxNeighbors, params.minNeighborRadius, params.maxNeighborRadius);
    if (edgesP.empty() || edgesC.empty()) return res;

    struct Cluster {
        float theta;
        float tx;
        float ty;
        int edgeCount = 0;
        std::vector<std::pair<int, int>> pairs;
    };
    std::vector<Cluster> clusters;

    for (const auto& ep : edgesP) {
        for (const auto& ec : edgesC) {
            const float maxLenDiff = std::max(params.descDistTolerance, 0.12f * std::max(ep.length, ec.length));
            if (std::abs(ep.length - ec.length) > maxLenDiff) continue;

            const float dBetaI = validatedMinutiaAngleDiff(ep.betaI, ec.betaI,
                                                           probe.minutiae[ep.i].type, candidate.minutiae[ec.i].type,
                                                           params.descAngleTolerance);
            if (dBetaI > params.descAngleTolerance) continue;

            const float dBetaJ = validatedMinutiaAngleDiff(ep.betaJ, ec.betaJ,
                                                           probe.minutiae[ep.j].type, candidate.minutiae[ec.j].type,
                                                           params.descAngleTolerance);
            if (dBetaJ > params.descAngleTolerance) continue;

            const float th = normalizeAngle(ec.dirAngle - ep.dirAngle);
            if (std::abs(th) > params.maxRotation) continue;

            const float cosT = std::cos(th);
            const float sinT = std::sin(th);
            const float predMidX = ep.midX * cosT - ep.midY * sinT;
            const float predMidY = ep.midX * sinT + ep.midY * cosT;
            const float tx = ec.midX - predMidX;
            const float ty = ec.midY - predMidY;

            if (std::sqrt(tx * tx + ty * ty) > params.maxTranslation) continue;

            bool merged = false;
            for (auto& cl : clusters) {
                if (angleDiff(cl.theta, th) <= 0.16f && dist2d(cl.tx, cl.ty, tx, ty) <= 20.0f) {
                    const float w = 1.0f / static_cast<float>(cl.edgeCount + 1);
                    cl.theta = normalizeAngle(cl.theta + w * normalizeAngle(th - cl.theta));
                    cl.tx += w * (tx - cl.tx);
                    cl.ty += w * (ty - cl.ty);
                    cl.edgeCount++;
                    cl.pairs.push_back({ep.i, ec.i});
                    cl.pairs.push_back({ep.j, ec.j});
                    merged = true;
                    break;
                }
            }

            if (!merged) {
                Cluster cl;
                cl.theta = th;
                cl.tx = tx;
                cl.ty = ty;
                cl.edgeCount = 1;
                cl.pairs.push_back({ep.i, ec.i});
                cl.pairs.push_back({ep.j, ec.j});
                clusters.push_back(std::move(cl));
            }
        }
    }

    struct ScoredCluster {
        size_t index;
        int distinctMinutiae;
        int edgeCount;
    };
    std::vector<ScoredCluster> scored;
    for (size_t k = 0; k < clusters.size(); ++k) {
        if (clusters[k].edgeCount < params.minClusterConsensus) continue;
        std::unordered_set<int> uP, uC;
        for (const auto& pr : clusters[k].pairs) {
            uP.insert(pr.first);
            uC.insert(pr.second);
        }
        const int dm = static_cast<int>(std::min(uP.size(), uC.size()));
        if (dm >= 3) {
            scored.push_back({k, dm, clusters[k].edgeCount});
        }
    }

    if (scored.empty()) {
        return res;
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredCluster& a, const ScoredCluster& b) {
        if (a.distinctMinutiae != b.distinctMinutiae) return a.distinctMinutiae > b.distinctMinutiae;
        return a.edgeCount > b.edgeCount;
    });

    const int maxHyp = std::min(static_cast<int>(scored.size()), params.maxHypothesesToTest);
    int bestMatchedCount = 0;
    float bestRms = 1e9f;
    std::vector<MatchPair> bestVerifiedPairs;
    AlignmentTransform bestTf{};

    for (int h = 0; h < maxHyp; ++h) {
        const auto& cl = clusters[scored[h].index];
        float curTheta = cl.theta;
        float curTx = cl.tx;
        float curTy = cl.ty;

        // Build candidate pair pool strictly from clusters compatible with this hypothesis pose
        // (OpenAFIS-style candidate pool bounded to compatible local neighborhood evidence)
        struct CandKey {
            int p;
            int c;
            bool operator==(const CandKey& o) const { return p == o.p && c == o.c; }
        };
        struct CandKeyHash {
            size_t operator()(const CandKey& k) const {
                return (static_cast<size_t>(k.p) << 16) ^ static_cast<size_t>(k.c);
            }
        };
        std::unordered_set<CandKey, CandKeyHash> candSet;
        for (const auto& pr : cl.pairs) {
            candSet.insert({pr.first, pr.second});
        }
        for (const auto& otherCl : clusters) {
            if (&otherCl == &cl) continue;
            if (angleDiff(otherCl.theta, curTheta) <= 0.18f &&
                dist2d(otherCl.tx, otherCl.ty, curTx, curTy) <= 25.0f) {
                for (const auto& pr : otherCl.pairs) {
                    candSet.insert({pr.first, pr.second});
                }
            }
        }

        std::vector<std::pair<int, int>> candidatePool;
        candidatePool.reserve(candSet.size());
        for (const auto& ck : candSet) {
            candidatePool.push_back({ck.p, ck.c});
        }

        // 1. Initial evaluation over candidate pool
        auto initialPairs = evaluateAlignmentCandidatePool(probe, candidate, candidatePool,
                                                           curTheta, curTx, curTy,
                                                           params.maxSpatialDistance, params.maxAngleDifference);
        if (initialPairs.size() < 2) continue;

        // 2. Refine alignment pose using Kabsch
        refineAlignment(probe, candidate, initialPairs, curTheta, curTx, curTy);

        // 3. Re-evaluate candidate pool with refined pose
        auto refinedPairs = evaluateAlignmentCandidatePool(probe, candidate, candidatePool,
                                                           curTheta, curTx, curTy,
                                                           params.maxSpatialDistance, params.maxAngleDifference);
        if (refinedPairs.size() < 2) continue;

        // 4. Verification via mutual distance & angle consistency
        float consistencyRatio = 0.0f;
        auto verifiedPairs = filterMutualConsistency(probe, candidate, refinedPairs, 9.0f, consistencyRatio);

        const int cnt = static_cast<int>(verifiedPairs.size());
        if (cnt == 0) continue;

        float sumSq = 0.0f;
        for (const auto& pr : verifiedPairs) sumSq += (pr.distance * pr.distance);
        const float rms = std::sqrt(sumSq / static_cast<float>(cnt));

        if (cnt > bestMatchedCount || (cnt == bestMatchedCount && rms < bestRms)) {
            bestMatchedCount = cnt;
            bestRms = rms;
            bestVerifiedPairs = std::move(verifiedPairs);
            bestTf.theta = curTheta;
            bestTf.tx = curTx;
            bestTf.ty = curTy;
        }
    }

    if (bestMatchedCount >= 2) {
        res.hasConsensus = true;
        res.transform = bestTf;
        res.verifiedPairs = std::move(bestVerifiedPairs);
    }
    return res;
}

} // namespace

MinutiaeMatcher::MinutiaeMatcher(MatcherParams params)
    : params_(params) {}

bool MinutiaeMatcher::estimateAlignment(const FingerprintTemplate& probe,
                                        const FingerprintTemplate& candidate,
                                        AlignmentTransform& outTransform) const {
    if (params_.strategy == MatcherStrategy::DelaunayTripletBaseline) {
        return estimateAlignmentDelaunay(probe, candidate, outTransform);
    }
    return estimateAlignmentLocalNeighborhood(probe, candidate, outTransform);
}

MatchResult MinutiaeMatcher::match(const FingerprintTemplate& probe,
                                   const FingerprintTemplate& candidate) const {
    if (params_.strategy == MatcherStrategy::DelaunayTripletBaseline) {
        return matchDelaunayTriplet(probe, candidate);
    }
    return matchLocalNeighborhood(probe, candidate);
}

bool MinutiaeMatcher::estimateAlignmentDelaunay(const FingerprintTemplate& probe,
                                                const FingerprintTemplate& candidate,
                                                AlignmentTransform& outTransform) const {
    if (probe.empty() || candidate.empty()) {
        outTransform = AlignmentTransform{};
        return false;
    }

    if (probe.size() < 10 || candidate.size() < 10) {
        return estimateAlignmentSmallTemplates(probe, candidate, outTransform, params_);
    }

    DelaunayTripletExtractor extractor;
    const auto probeTriplets = extractor.extractTriplets(probe);
    const auto candTriplets  = extractor.extractTriplets(candidate);

    if (!probeTriplets.empty() && !candTriplets.empty()) {
        TripletMatcher tripletMatcher;
        const auto candidatePool = tripletMatcher.generateCandidatePool(probe, probeTriplets, candidate, candTriplets, 4);

        if (candidatePool.isViable && !candidatePool.candidatePairs.empty()) {
            const auto& pairs = candidatePool.candidatePairs;
            const size_t numPairs = pairs.size();

            int maxMatched = 0;
            std::vector<MatchPair> bestVerifiedPairs;
            AlignmentTransform bestTf{};

            for (size_t i = 0; i < numPairs; ++i) {
                const auto& p1 = pairs[i];
                const auto& mp1 = probe.minutiae[p1.probeIndex];
                const auto& mc1 = candidate.minutiae[p1.candidateIndex];

                const float theta = normalizeAngle(mc1.angle - mp1.angle);
                if (std::abs(theta) > params_.maxRotation) {
                    continue;
                }

                const float cosTheta = std::cos(theta);
                const float sinTheta = std::sin(theta);

                std::vector<MatchPair> currentVerified;
                currentVerified.push_back({p1.probeIndex, p1.candidateIndex, 0.0f, 0.0f});

                for (size_t j = 0; j < numPairs; ++j) {
                    if (i == j) continue;

                    const auto& p2 = pairs[j];
                    if (p2.probeIndex == p1.probeIndex || p2.candidateIndex == p1.candidateIndex) {
                        continue;
                    }

                    const auto& mp2 = probe.minutiae[p2.probeIndex];
                    const auto& mc2 = candidate.minutiae[p2.candidateIndex];

                    const float predX = mc1.x + cosTheta * (mp2.x - mp1.x) - sinTheta * (mp2.y - mp1.y);
                    const float predY = mc1.y + sinTheta * (mp2.x - mp1.x) + cosTheta * (mp2.y - mp1.y);
                    const float dx = predX - mc2.x;
                    const float dy = predY - mc2.y;
                    const float distSq = dx * dx + dy * dy;

                    if (distSq > params_.maxSpatialDistance * params_.maxSpatialDistance) {
                        continue;
                    }

                    const float rotAng = normalizeAngle(mp2.angle + theta);
                    const float dirDiff = minutiaAngleDiff(rotAng, mc2.angle, mp2.type, mc2.type);
                    if (dirDiff > params_.maxAngleDifference) {
                        continue;
                    }

                    const float dirP = std::atan2(mp2.y - mp1.y, mp2.x - mp1.x);
                    const float dirC = std::atan2(mc2.y - mc1.y, mc2.x - mc1.x);
                    const float betaP = normalizeAngle(mp1.angle - dirP);
                    const float betaC = normalizeAngle(mc1.angle - dirC);
                    const float betaDiff = minutiaAngleDiff(betaP, betaC, mp1.type, mc1.type);

                    if (betaDiff > params_.maxAngleDifference) {
                        continue;
                    }

                    currentVerified.push_back({p2.probeIndex, p2.candidateIndex, std::sqrt(distSq), dirDiff});
                }

                const int matchedCount = static_cast<int>(currentVerified.size());
                if (matchedCount > maxMatched) {
                    maxMatched = matchedCount;
                    bestVerifiedPairs = std::move(currentVerified);
                    bestTf.theta = theta;
                    bestTf.tx = mc1.x - (mp1.x * cosTheta - mp1.y * sinTheta);
                    bestTf.ty = mc1.y - (mp1.x * sinTheta + mp1.y * cosTheta);
                }
            }

            if (maxMatched >= 3) {
                if (bestVerifiedPairs.size() >= 2) {
                    refineAlignment(probe, candidate, bestVerifiedPairs, bestTf.theta, bestTf.tx, bestTf.ty);
                }
                outTransform = bestTf;
                return true;
            }
        }
    }

    outTransform = AlignmentTransform{};
    return false;
}

bool MinutiaeMatcher::estimateAlignmentLocalNeighborhood(const FingerprintTemplate& probe,
                                                         const FingerprintTemplate& candidate,
                                                         AlignmentTransform& outTransform) const {
    const auto consensus = findLocalNeighborhoodConsensus(probe, candidate, params_);
    if (consensus.hasConsensus && consensus.verifiedPairs.size() >= 3) {
        outTransform = consensus.transform;
        return true;
    }
    outTransform = AlignmentTransform{};
    return false;
}

MatchResult MinutiaeMatcher::matchDelaunayTriplet(const FingerprintTemplate& probe,
                                                  const FingerprintTemplate& candidate) const {
    MatchResult res;

    if (probe.empty() || candidate.empty()) {
        res.isMatch = false;
        res.score = 0.0f;
        res.matchedPairs = 0;
        res.rmsError = 0.0f;
        res.message = "Empty template provided";
        return res;
    }

    DelaunayTripletExtractor extractor;
    const auto probeTriplets = extractor.extractTriplets(probe);
    const auto candTriplets  = extractor.extractTriplets(candidate);

    const bool hasEnoughTriplets = (probe.size() >= 10 && candidate.size() >= 10 &&
                                    !probeTriplets.empty() && !candTriplets.empty());

    if (hasEnoughTriplets) {
        TripletMatcher tripletMatcher;
        const auto candidatePool = tripletMatcher.generateCandidatePool(probe, probeTriplets, candidate, candTriplets, 4);

        if (!candidatePool.isViable) {
            res.isMatch = false;
            res.score = 0.0f;
            res.matchedPairs = 0;
            res.rmsError = 0.0f;
            res.message = "Non-match: Insufficient compatible triplet evidence (< 4 triplets)";
            return res;
        }

        const auto& pairs = candidatePool.candidatePairs;
        const size_t numPairs = pairs.size();

        int maxMatched = 0;
        std::vector<MatchPair> bestVerifiedPairs;
        AlignmentTransform bestTf{};

        for (size_t i = 0; i < numPairs; ++i) {
            const auto& p1 = pairs[i];
            const auto& mp1 = probe.minutiae[p1.probeIndex];
            const auto& mc1 = candidate.minutiae[p1.candidateIndex];

            const float theta = normalizeAngle(mc1.angle - mp1.angle);
            if (std::abs(theta) > params_.maxRotation) {
                continue;
            }

            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            std::vector<MatchPair> currentVerified;
            currentVerified.push_back({p1.probeIndex, p1.candidateIndex, 0.0f, 0.0f});

            for (size_t j = 0; j < numPairs; ++j) {
                if (i == j) continue;

                const auto& p2 = pairs[j];
                if (p2.probeIndex == p1.probeIndex || p2.candidateIndex == p1.candidateIndex) {
                    continue;
                }

                const auto& mp2 = probe.minutiae[p2.probeIndex];
                const auto& mc2 = candidate.minutiae[p2.candidateIndex];

                const float predX = mc1.x + cosTheta * (mp2.x - mp1.x) - sinTheta * (mp2.y - mp1.y);
                const float predY = mc1.y + sinTheta * (mp2.x - mp1.x) + cosTheta * (mp2.y - mp1.y);
                const float dx = predX - mc2.x;
                const float dy = predY - mc2.y;
                const float distSq = dx * dx + dy * dy;

                if (distSq > params_.maxSpatialDistance * params_.maxSpatialDistance) {
                    continue;
                }

                const float rotAng = normalizeAngle(mp2.angle + theta);
                const float dirDiff = minutiaAngleDiff(rotAng, mc2.angle, mp2.type, mc2.type);
                if (dirDiff > params_.maxAngleDifference) {
                    continue;
                }

                const float dirP = std::atan2(mp2.y - mp1.y, mp2.x - mp1.x);
                const float dirC = std::atan2(mc2.y - mc1.y, mc2.x - mc1.x);
                const float betaP = normalizeAngle(mp1.angle - dirP);
                const float betaC = normalizeAngle(mc1.angle - dirC);
                const float betaDiff = minutiaAngleDiff(betaP, betaC, mp1.type, mc1.type);

                if (betaDiff > params_.maxAngleDifference) {
                    continue;
                }

                currentVerified.push_back({p2.probeIndex, p2.candidateIndex, std::sqrt(distSq), dirDiff});
            }

            const int matchedCount = static_cast<int>(currentVerified.size());
            if (matchedCount > maxMatched) {
                maxMatched = matchedCount;
                bestVerifiedPairs = std::move(currentVerified);
                bestTf.theta = theta;
                bestTf.tx = mc1.x - (mp1.x * cosTheta - mp1.y * sinTheta);
                bestTf.ty = mc1.y - (mp1.x * sinTheta + mp1.y * cosTheta);
            }
        }

        if (maxMatched < params_.minMatchedPairs / 2) {
            res.isMatch = false;
            res.score = 0.0f;
            res.matchedPairs = maxMatched;
            res.rmsError = 0.0f;
            res.message = "Non-match: Pivot verification yielded insufficient correspondences";
            return res;
        }

        if (bestVerifiedPairs.size() >= 2) {
            refineAlignment(probe, candidate, bestVerifiedPairs, bestTf.theta, bestTf.tx, bestTf.ty);
        }

        return computeMatchResult(probe, candidate, bestTf.theta, bestTf.tx, bestTf.ty,
                                  std::move(bestVerifiedPairs), params_);
    } else {
        AlignmentTransform tf;
        const bool aligned = estimateAlignmentDelaunay(probe, candidate, tf);
        if (!aligned) {
            res.isMatch = false;
            res.score = 0.0f;
            res.matchedPairs = 0;
            res.rmsError = 0.0f;
            res.message = "No plausible alignment found";
            return res;
        }

        auto rawPairs = evaluateAlignment(probe, candidate, tf.theta, tf.tx, tf.ty,
                                          params_.maxSpatialDistance, params_.maxAngleDifference);
        float dummyRatio = 0.0f;
        auto pairs = filterMutualConsistency(probe, candidate, rawPairs, 9.0f, dummyRatio);
        return computeMatchResult(probe, candidate, tf.theta, tf.tx, tf.ty,
                                  std::move(pairs), params_);
    }
}

MatchResult MinutiaeMatcher::matchLocalNeighborhood(const FingerprintTemplate& probe,
                                                    const FingerprintTemplate& candidate) const {
    if (probe.empty() || candidate.empty()) {
        MatchResult res;
        res.isMatch = false;
        res.score = 0.0f;
        res.matchedPairs = 0;
        res.rmsError = 0.0f;
        res.message = "Empty template provided";
        return res;
    }

    const auto consensus = findLocalNeighborhoodConsensus(probe, candidate, params_);
    if (!consensus.hasConsensus) {
        MatchResult res;
        res.isMatch = false;
        res.score = 0.0f;
        res.matchedPairs = 0;
        res.rmsError = 0.0f;
        res.message = "Non-match: No local neighborhood geometric consensus found";
        return res;
    }

    if (consensus.verifiedPairs.size() < static_cast<size_t>(params_.minMatchedPairs / 2)) {
        MatchResult res;
        res.isMatch = false;
        res.score = 0.0f;
        res.matchedPairs = static_cast<int>(consensus.verifiedPairs.size());
        res.rmsError = 0.0f;
        res.message = "Non-match: Local neighborhood consensus yielded insufficient correspondences";
        return res;
    }

    return computeMatchResult(probe, candidate,
                              consensus.transform.theta,
                              consensus.transform.tx,
                              consensus.transform.ty,
                              consensus.verifiedPairs,
                              params_);
}

cv::Mat MinutiaeMatcher::visualizeMatch(const cv::Mat& probeImg,
                                        const cv::Mat& candImg,
                                        const FingerprintTemplate& probeTpl,
                                        const FingerprintTemplate& candTpl,
                                        const MatchResult& result) const {
    cv::Mat pGray, cGray;
    if (probeImg.channels() == 3) cv::cvtColor(probeImg, pGray, cv::COLOR_BGR2GRAY);
    else probeImg.copyTo(pGray);

    if (candImg.channels() == 3) cv::cvtColor(candImg, cGray, cv::COLOR_BGR2GRAY);
    else candImg.copyTo(cGray);

    const int bannerHeight = 60;
    const int gap = 30;
    const int targetH = std::max(pGray.rows, cGray.rows);
    const int totalW = pGray.cols + gap + cGray.cols;
    const int totalH = targetH + bannerHeight;

    cv::Mat vis(totalH, totalW, CV_8UC3, cv::Scalar(30, 30, 30));

    cv::Mat pColor, cColor;
    cv::cvtColor(pGray, pColor, cv::COLOR_GRAY2BGR);
    cv::cvtColor(cGray, cColor, cv::COLOR_GRAY2BGR);

    cv::Rect roiP(0, bannerHeight, pColor.cols, pColor.rows);
    cv::Rect roiC(pColor.cols + gap, bannerHeight, cColor.cols, cColor.rows);
    pColor.copyTo(vis(roiP));
    cColor.copyTo(vis(roiC));

    std::unordered_set<int> matchedP;
    std::unordered_set<int> matchedC;
    for (const auto& pr : result.pairs) {
        matchedP.insert(pr.probeIndex);
        matchedC.insert(pr.candidateIndex);
    }

    const int cOffsetX = pColor.cols + gap;

    for (size_t i = 0; i < probeTpl.minutiae.size(); ++i) {
        const auto& m = probeTpl.minutiae[i];
        const cv::Point pt(static_cast<int>(std::round(m.x)),
                           bannerHeight + static_cast<int>(std::round(m.y)));
        const bool isMatched = (matchedP.count(static_cast<int>(i)) > 0);
        const cv::Scalar color = isMatched ? cv::Scalar(0, 230, 0) : cv::Scalar(0, 0, 230);

        cv::circle(vis, pt, 4, color, isMatched ? 2 : 1);
        const int dx = static_cast<int>(std::round(10.0f * std::cos(m.angle)));
        const int dy = static_cast<int>(std::round(10.0f * std::sin(m.angle)));
        cv::line(vis, pt, cv::Point(pt.x + dx, pt.y + dy), color, 1);
    }

    for (size_t j = 0; j < candTpl.minutiae.size(); ++j) {
        const auto& m = candTpl.minutiae[j];
        const cv::Point pt(cOffsetX + static_cast<int>(std::round(m.x)),
                           bannerHeight + static_cast<int>(std::round(m.y)));
        const bool isMatched = (matchedC.count(static_cast<int>(j)) > 0);
        const cv::Scalar color = isMatched ? cv::Scalar(0, 230, 0) : cv::Scalar(0, 0, 230);

        cv::circle(vis, pt, 4, color, isMatched ? 2 : 1);
        const int dx = static_cast<int>(std::round(10.0f * std::cos(m.angle)));
        const int dy = static_cast<int>(std::round(10.0f * std::sin(m.angle)));
        cv::line(vis, pt, cv::Point(pt.x + dx, pt.y + dy), color, 1);
    }

    for (const auto& pr : result.pairs) {
        if (pr.probeIndex < 0 || pr.probeIndex >= static_cast<int>(probeTpl.minutiae.size())) continue;
        if (pr.candidateIndex < 0 || pr.candidateIndex >= static_cast<int>(candTpl.minutiae.size())) continue;

        const auto& mp = probeTpl.minutiae[pr.probeIndex];
        const auto& mc = candTpl.minutiae[pr.candidateIndex];

        const cv::Point pt1(static_cast<int>(std::round(mp.x)),
                            bannerHeight + static_cast<int>(std::round(mp.y)));
        const cv::Point pt2(cOffsetX + static_cast<int>(std::round(mc.x)),
                            bannerHeight + static_cast<int>(std::round(mc.y)));

        cv::line(vis, pt1, pt2, cv::Scalar(0, 255, 120), 1, cv::LINE_AA);
    }

    const cv::Scalar bannerColor = result.isMatch ? cv::Scalar(20, 100, 20) : cv::Scalar(20, 20, 100);
    cv::rectangle(vis, cv::Point(0, 0), cv::Point(totalW, bannerHeight), bannerColor, cv::FILLED);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << (result.isMatch ? "[MATCH] " : "[NON-MATCH] ")
       << "Score: " << result.score << "/100  |  "
       << "Pairs: " << result.matchedPairs << "  |  "
       << "RMS: " << result.rmsError << "px  |  "
       << "Rot: " << (result.rotation * 180.0f / kPi) << " deg  |  "
       << "Trans: (" << result.translationX << ", " << result.translationY << ")";

    cv::putText(vis, ss.str(), cv::Point(15, 38), cv::FONT_HERSHEY_SIMPLEX, 0.65,
                cv::Scalar(255, 255, 255), 2, cv::LINE_AA);

    return vis;
}

} // namespace kipsma::fingerprint

