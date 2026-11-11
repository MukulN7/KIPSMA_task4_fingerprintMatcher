#include "kipsma_fingerprint/extractor.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <vector>

namespace kipsma::fingerprint {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

// Normalizes angle to [0, 2*PI)
inline float normalizeAngle(float angle) {
    while (angle < 0.0f) angle += kTwoPi;
    while (angle >= kTwoPi) angle -= kTwoPi;
    return angle;
}

// Zhang-Suen morphological thinning algorithm on binary image (1 foreground, 0 background)
void zhangSuenThinning(cv::Mat& img) {
    CV_Assert(img.type() == CV_8UC1);

    const int rows = img.rows;
    const int cols = img.cols;

    std::vector<cv::Point> toErase;
    toErase.reserve(1024);

    bool hasChanged = true;
    int maxIterations = 100;

    while (hasChanged && maxIterations-- > 0) {
        hasChanged = false;

        // --- Sub-iteration 1 ---
        toErase.clear();
        for (int r = 1; r < rows - 1; ++r) {
            const uchar* pPrev = img.ptr<uchar>(r - 1);
            const uchar* pCurr = img.ptr<uchar>(r);
            const uchar* pNext = img.ptr<uchar>(r + 1);

            for (int c = 1; c < cols - 1; ++c) {
                if (pCurr[c] == 0) continue;

                // 8 neighbors in clockwise order from North:
                // P9 P2 P3
                // P8 P1 P4
                // P7 P6 P5
                const uchar p2 = pPrev[c];
                const uchar p3 = pPrev[c + 1];
                const uchar p4 = pCurr[c + 1];
                const uchar p5 = pNext[c + 1];
                const uchar p6 = pNext[c];
                const uchar p7 = pNext[c - 1];
                const uchar p8 = pCurr[c - 1];
                const uchar p9 = pPrev[c - 1];

                const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                if (B < 2 || B > 6) continue;

                // 0 -> 1 transitions in clockwise circular order
                int A = 0;
                if (p2 == 0 && p3 == 1) ++A;
                if (p3 == 0 && p4 == 1) ++A;
                if (p4 == 0 && p5 == 1) ++A;
                if (p5 == 0 && p6 == 1) ++A;
                if (p6 == 0 && p7 == 1) ++A;
                if (p7 == 0 && p8 == 1) ++A;
                if (p8 == 0 && p9 == 1) ++A;
                if (p9 == 0 && p2 == 1) ++A;
                if (A != 1) continue;

                if (p2 * p4 * p6 != 0) continue;
                if (p4 * p6 * p8 != 0) continue;

                toErase.emplace_back(c, r);
            }
        }

        if (!toErase.empty()) {
            hasChanged = true;
            for (const auto& pt : toErase) {
                img.at<uchar>(pt.y, pt.x) = 0;
            }
        }

        // --- Sub-iteration 2 ---
        toErase.clear();
        for (int r = 1; r < rows - 1; ++r) {
            const uchar* pPrev = img.ptr<uchar>(r - 1);
            const uchar* pCurr = img.ptr<uchar>(r);
            const uchar* pNext = img.ptr<uchar>(r + 1);

            for (int c = 1; c < cols - 1; ++c) {
                if (pCurr[c] == 0) continue;

                const uchar p2 = pPrev[c];
                const uchar p3 = pPrev[c + 1];
                const uchar p4 = pCurr[c + 1];
                const uchar p5 = pNext[c + 1];
                const uchar p6 = pNext[c];
                const uchar p7 = pNext[c - 1];
                const uchar p8 = pCurr[c - 1];
                const uchar p9 = pPrev[c - 1];

                const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                if (B < 2 || B > 6) continue;

                int A = 0;
                if (p2 == 0 && p3 == 1) ++A;
                if (p3 == 0 && p4 == 1) ++A;
                if (p4 == 0 && p5 == 1) ++A;
                if (p5 == 0 && p6 == 1) ++A;
                if (p6 == 0 && p7 == 1) ++A;
                if (p7 == 0 && p8 == 1) ++A;
                if (p8 == 0 && p9 == 1) ++A;
                if (p9 == 0 && p2 == 1) ++A;
                if (A != 1) continue;

                if (p2 * p4 * p8 != 0) continue;
                if (p2 * p6 * p8 != 0) continue;

                toErase.emplace_back(c, r);
            }
        }

        if (!toErase.empty()) {
            hasChanged = true;
            for (const auto& pt : toErase) {
                img.at<uchar>(pt.y, pt.x) = 0;
            }
        }
    }
}

// 8-neighbor circular relative offsets
constexpr int kDx[8] = { 0,  1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1,  1,  0, -1};

// Computes Rutovitz Crossing Number (CN)
inline int computeCrossingNumber(const cv::Mat& skel, int r, int c) {
    int sum = 0;
    for (int i = 0; i < 8; ++i) {
        const int ni = skel.at<uchar>(r + kDy[i], c + kDx[i]);
        const int nj = skel.at<uchar>(r + kDy[(i + 1) % 8], c + kDx[(i + 1) % 8]);
        sum += std::abs(ni - nj);
    }
    return sum / 2;
}

// Traces along skeleton from startPt through firstStep for up to maxSteps
cv::Point traceSkeletonBranch(const cv::Mat& skel, cv::Point startPt, cv::Point firstStep, int maxSteps) {
    cv::Point curr = firstStep;
    cv::Point prev = startPt;

    for (int step = 1; step < maxSteps; ++step) {
        cv::Point next(-1, -1);
        int nextNeighbors = 0;

        for (int i = 0; i < 8; ++i) {
            const int ny = curr.y + kDy[i];
            const int nx = curr.x + kDx[i];
            if (ny < 0 || ny >= skel.rows || nx < 0 || nx >= skel.cols) continue;
            if (skel.at<uchar>(ny, nx) == 0) continue;
            if (nx == prev.x && ny == prev.y) continue;

            next = cv::Point(nx, ny);
            ++nextNeighbors;
        }

        // If we reached an ending or branch junction, stop trace
        if (nextNeighbors != 1) break;

        prev = curr;
        curr = next;
    }

    return curr;
}


// Traces along skeleton to check if an ending belongs to a tiny isolated island (length <= maxSteps)
bool isSkeletonIsland(const cv::Mat& skel, cv::Point startPt, int maxSteps) {
    cv::Point curr(-1, -1);
    for (int i = 0; i < 8; ++i) {
        const int ny = startPt.y + kDy[i];
        const int nx = startPt.x + kDx[i];
        if (ny >= 0 && ny < skel.rows && nx >= 0 && nx < skel.cols && skel.at<uchar>(ny, nx) > 0) {
            curr = cv::Point(nx, ny);
            break;
        }
    }
    if (curr.x < 0) return true; // Isolated 1-pixel speck

    cv::Point prev = startPt;
    for (int step = 1; step <= maxSteps; ++step) {
        cv::Point next(-1, -1);
        int branchCount = 0;
        for (int i = 0; i < 8; ++i) {
            const int ny = curr.y + kDy[i];
            const int nx = curr.x + kDx[i];
            if (ny < 0 || ny >= skel.rows || nx < 0 || nx >= skel.cols) continue;
            if (skel.at<uchar>(ny, nx) == 0) continue;
            if (nx == prev.x && ny == prev.y) continue;
            next = cv::Point(nx, ny);
            ++branchCount;
        }
        if (branchCount == 0) {
            // Reached another ending within <= maxSteps steps: it is an isolated island
            return true;
        }
        if (branchCount > 1) {
            // Reached a junction/bifurcation: connected to the ridge network
            return false;
        }
        prev = curr;
        curr = next;
    }
    return false;
}

// Candidate raw minutia with tracking flags
struct RawMinutia {
    cv::Point   pt;
    MinutiaType type = MinutiaType::Unknown;
    float       angle = 0.0f;
    float       quality = 0.0f;
    bool        valid = true;
};

} // namespace

struct MinutiaeExtractor::Impl {
    ExtractorParams params;
    explicit Impl(const ExtractorParams& p) : params(p) {}
};

MinutiaeExtractor::MinutiaeExtractor(const ExtractorParams& params)
    : impl_(std::make_unique<Impl>(params)) {}

MinutiaeExtractor::~MinutiaeExtractor() = default;

MinutiaeExtractor::MinutiaeExtractor(const MinutiaeExtractor& other)
    : impl_(std::make_unique<Impl>(other.impl_->params)) {}

MinutiaeExtractor& MinutiaeExtractor::operator=(const MinutiaeExtractor& other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(other.impl_->params);
    }
    return *this;
}

MinutiaeExtractor::MinutiaeExtractor(MinutiaeExtractor&&) noexcept = default;
MinutiaeExtractor& MinutiaeExtractor::operator=(MinutiaeExtractor&&) noexcept = default;

const ExtractorParams& MinutiaeExtractor::params() const {
    return impl_->params;
}

void MinutiaeExtractor::setParams(const ExtractorParams& params) {
    impl_->params = params;
}

ExtractionResult MinutiaeExtractor::extract(const EnhancementResult& enhancement) const {
    const auto startTime = std::chrono::high_resolution_clock::now();
    ExtractionResult res;

    // 1. Validation & Pre-conditions
    if (!enhancement.success || enhancement.enhancedImage.empty() || enhancement.foregroundMask.empty()) {
        res.success = false;
        res.statusMessage = "Invalid or unsuccessful EnhancementResult provided";
        return res;
    }

    const cv::Mat& enhanced = enhancement.enhancedImage;
    const cv::Mat& mask     = enhancement.foregroundMask;
    const cv::Size imgSize  = enhanced.size();

    // 2. Adaptive Binarization
    // Since enhancedImage is already orientation-filtered and contrast-normalized,
    // thresholding pixels below the foreground mid-level (128) isolates ridge centers cleanly.
    cv::Mat binarized(imgSize, CV_8UC1, cv::Scalar(0));
    cv::threshold(enhanced, binarized, 128, 255, cv::THRESH_BINARY_INV);
    binarized.setTo(0, ~mask);
    res.binarizedImage = binarized.clone();

    // 3. Skeletonization (Zhang-Suen Thinning)
    cv::Mat skel = binarized / 255; // 0/1 representation for algorithm
    zhangSuenThinning(skel);

    res.skeletonImage = skel * 255;

    // Distance transform of foreground mask for border rejection
    cv::Mat distToBorder;
    cv::distanceTransform(mask, distToBorder, cv::DIST_L2, 3);

    // 4. Crossing Number (Rutovitz CN) Minutiae Detection
    std::vector<RawMinutia> rawDetections;
    rawDetections.reserve(256);

    for (int r = 2; r < imgSize.height - 2; ++r) {
        for (int c = 2; c < imgSize.width - 2; ++c) {
            if (skel.at<uchar>(r, c) == 0) continue;

            const int cn = computeCrossingNumber(skel, r, c);
            if (cn == 1) {
                // Ridge Ending
                ++res.filterStats.rawEndings;

                // Find the single outgoing neighbor
                cv::Point neighbor(-1, -1);
                for (int i = 0; i < 8; ++i) {
                    if (skel.at<uchar>(r + kDy[i], c + kDx[i]) > 0) {
                        neighbor = cv::Point(c + kDx[i], r + kDy[i]);
                        break;
                    }
                }

                float angle = 0.0f;
                if (neighbor.x >= 0) {
                    const cv::Point endPt = traceSkeletonBranch(skel, cv::Point(c, r), neighbor,
                                                               impl_->params.directionTraceSteps);
                    const float dx = static_cast<float>(endPt.x - c);
                    const float dy = static_cast<float>(endPt.y - r);
                    angle = (std::abs(dx) > 1e-4f || std::abs(dy) > 1e-4f)
                                ? std::atan2(dy, dx)
                                : std::atan2(static_cast<float>(neighbor.y - r), static_cast<float>(neighbor.x - c));
                }

                RawMinutia m;
                m.pt = cv::Point(c, r);
                m.type = MinutiaType::Ending;
                m.angle = normalizeAngle(angle);
                rawDetections.push_back(m);

            } else if (cn == 3) {
                // Ridge Bifurcation
                ++res.filterStats.rawBifurcations;

                // Identify the 3 branch neighbor pixels
                std::vector<cv::Point> branchNeighbors;
                for (int i = 0; i < 8; ++i) {
                    if (skel.at<uchar>(r + kDy[i], c + kDx[i]) > 0) {
                        // Avoid counting adjacent pixels that belong to the same 8-connected branch
                        const int prevI = (i + 7) % 8;
                        if (skel.at<uchar>(r + kDy[prevI], c + kDx[prevI]) == 0) {
                            branchNeighbors.emplace_back(c + kDx[i], r + kDy[i]);
                        }
                    }
                }

                float angle = 0.0f;
                if (branchNeighbors.size() == 3) {
                    // Trace each of the 3 branches
                    std::vector<cv::Point2f> branchDirs;
                    for (int k = 0; k < 3; ++k) {
                        const cv::Point ep = traceSkeletonBranch(skel, cv::Point(c, r), branchNeighbors[k],
                                                                 impl_->params.directionTraceSteps);
                        cv::Point2f dir(static_cast<float>(ep.x - c), static_cast<float>(ep.y - r));
                        const float len = std::hypot(dir.x, dir.y);
                        if (len > 1e-3f) {
                            dir.x /= len;
                            dir.y /= len;
                        }
                        branchDirs.push_back(dir);
                    }

                    // Find which pair forms the fork (highest dot product / smallest angle between them)
                    const float dot01 = branchDirs[0].x * branchDirs[1].x + branchDirs[0].y * branchDirs[1].y;
                    const float dot12 = branchDirs[1].x * branchDirs[2].x + branchDirs[1].y * branchDirs[2].y;
                    const float dot20 = branchDirs[2].x * branchDirs[0].x + branchDirs[2].y * branchDirs[0].y;

                    cv::Point2f trunkDir;
                    if (dot01 >= dot12 && dot01 >= dot20) {
                        // 0 and 1 are fork branches, 2 is trunk
                        trunkDir = branchDirs[2];
                    } else if (dot12 >= dot01 && dot12 >= dot20) {
                        // 1 and 2 are fork branches, 0 is trunk
                        trunkDir = branchDirs[0];
                    } else {
                        // 2 and 0 are fork branches, 1 is trunk
                        trunkDir = branchDirs[1];
                    }

                    angle = std::atan2(trunkDir.y, trunkDir.x);
                } else {
                    // Fallback to local orientation from enhancement map
                    angle = enhancement.orientationMap.at<float>(r, c);
                }

                RawMinutia m;
                m.pt = cv::Point(c, r);
                m.type = MinutiaType::Bifurcation;
                m.angle = normalizeAngle(angle);
                rawDetections.push_back(m);
            }
        }
    }

    // 5. Stage A: Filter Boundary Margin Detections
    for (auto& m : rawDetections) {
        if (!m.valid) continue;
        const float dist = distToBorder.at<float>(m.pt.y, m.pt.x);
        if (dist < static_cast<float>(impl_->params.borderMargin)) {
            m.valid = false;
            ++res.filterStats.removedBorderMargin;
        }
    }

    // 6. Stage B: Filter Low Coherence Detections
    for (auto& m : rawDetections) {
        if (!m.valid) continue;
        const float coh = enhancement.coherenceMap.at<float>(m.pt.y, m.pt.x);
        if (coh < impl_->params.minCoherence) {
            m.valid = false;
            ++res.filterStats.removedLowCoherence;
        }
    }

    // 7. Stage C: Filter Broken Ridges (Collinear Opposing Endings)
    for (size_t i = 0; i < rawDetections.size(); ++i) {
        if (!rawDetections[i].valid || rawDetections[i].type != MinutiaType::Ending) continue;

        for (size_t j = i + 1; j < rawDetections.size(); ++j) {
            if (!rawDetections[j].valid || rawDetections[j].type != MinutiaType::Ending) continue;

            const float dx = static_cast<float>(rawDetections[j].pt.x - rawDetections[i].pt.x);
            const float dy = static_cast<float>(rawDetections[j].pt.y - rawDetections[i].pt.y);
            const float dist = std::hypot(dx, dy);

            if (dist <= impl_->params.brokenRidgeMaxDist && dist > 1e-3f) {
                // Angle difference: opposing directions have cos(diff) near -1.0
                const float cosDiff = std::cos(rawDetections[i].angle - rawDetections[j].angle);
                if (cosDiff <= impl_->params.brokenRidgeCosThreshold) {
                    // Check alignment of connecting displacement vector with ridge direction
                    const float uX = dx / dist;
                    const float uY = dy / dist;
                    const float alignI = uX * std::cos(rawDetections[i].angle) + uY * std::sin(rawDetections[i].angle);
                    if (alignI > 0.5f) {
                        // Confirmed broken ridge gap
                        rawDetections[i].valid = false;
                        rawDetections[j].valid = false;
                        res.filterStats.removedBrokenRidges += 2;
                        break;
                    }
                }
            }
        }
    }

    // 8. Stage D: Filter Short Spurs (Bifurcation connected to an Ending by <= spurMaxDistance)
    for (size_t i = 0; i < rawDetections.size(); ++i) {
        if (!rawDetections[i].valid || rawDetections[i].type != MinutiaType::Bifurcation) continue;

        for (size_t j = 0; j < rawDetections.size(); ++j) {
            if (i == j || !rawDetections[j].valid || rawDetections[j].type != MinutiaType::Ending) continue;

            const float dist = std::hypot(static_cast<float>(rawDetections[i].pt.x - rawDetections[j].pt.x),
                                          static_cast<float>(rawDetections[i].pt.y - rawDetections[j].pt.y));

            if (dist <= impl_->params.spurMaxDistance) {
                rawDetections[j].valid = false; // Filter spur ending
                if (dist <= 6.0f) {
                    rawDetections[i].valid = false; // Very short spur; filter bifurcation bump
                    res.filterStats.removedSpurs += 2;
                } else {
                    ++res.filterStats.removedSpurs;
                }
                break;
            }
        }
    }

    // Stage D2: Filter Close Bifurcation Pairs (Bridge / Ladder Rung Artifacts)
    for (size_t i = 0; i < rawDetections.size(); ++i) {
        if (!rawDetections[i].valid || rawDetections[i].type != MinutiaType::Bifurcation) continue;

        for (size_t j = i + 1; j < rawDetections.size(); ++j) {
            if (!rawDetections[j].valid || rawDetections[j].type != MinutiaType::Bifurcation) continue;

            const float dist = std::hypot(static_cast<float>(rawDetections[i].pt.x - rawDetections[j].pt.x),
                                          static_cast<float>(rawDetections[i].pt.y - rawDetections[j].pt.y));

            if (dist <= 4.0f) {
                // Near-duplicate skeleton pixels of the same junction: keep one, discard the duplicate
                rawDetections[j].valid = false;
                ++res.filterStats.removedDuplicateClusters;
            } else if (dist <= impl_->params.minBifurcationDistance) {
                // False cross-bridge ladder artifact connecting adjacent ridges: discard both
                rawDetections[i].valid = false;
                rawDetections[j].valid = false;
                res.filterStats.removedSpurs += 2;
                break;
            }
        }
    }

    // Stage D3: Filter Short Islands (Isolated ridge fragments with two endings)
    for (size_t i = 0; i < rawDetections.size(); ++i) {
        if (!rawDetections[i].valid || rawDetections[i].type != MinutiaType::Ending) continue;

        if (isSkeletonIsland(skel, rawDetections[i].pt, static_cast<int>(impl_->params.islandMaxDistance))) {
            rawDetections[i].valid = false;
            ++res.filterStats.removedBrokenRidges;
        }
    }

    // 9. Stage E: Filter Duplicate Clusters / Too Close Minutiae
    // Sort candidates by distance from boundary descending (keep minutiae further inside ROI)
    for (size_t i = 0; i < rawDetections.size(); ++i) {
        if (!rawDetections[i].valid) continue;

        for (size_t j = i + 1; j < rawDetections.size(); ++j) {
            if (!rawDetections[j].valid) continue;

            const float dist = std::hypot(static_cast<float>(rawDetections[i].pt.x - rawDetections[j].pt.x),
                                          static_cast<float>(rawDetections[i].pt.y - rawDetections[j].pt.y));

            if (dist < impl_->params.minMinutiaeDistance) {
                // Reject the candidate closer to the boundary or with lower coherence
                const float cohI = enhancement.coherenceMap.at<float>(rawDetections[i].pt.y, rawDetections[i].pt.x);
                const float cohJ = enhancement.coherenceMap.at<float>(rawDetections[j].pt.y, rawDetections[j].pt.x);

                if (cohI >= cohJ) {
                    rawDetections[j].valid = false;
                } else {
                    rawDetections[i].valid = false;
                }
                ++res.filterStats.removedDuplicateClusters;
                if (!rawDetections[i].valid) break;
            }
        }
    }

    // 10. Populate Retained Minutiae and Quality Estimates
    for (const auto& raw : rawDetections) {
        if (!raw.valid) continue;

        const float coh = enhancement.coherenceMap.at<float>(raw.pt.y, raw.pt.x);
        const float dist = distToBorder.at<float>(raw.pt.y, raw.pt.x);
        const float borderFactor = std::min(1.0f, dist / 35.0f);
        const float quality = std::clamp(0.65f * coh + 0.35f * borderFactor, 0.05f, 1.0f);

        Minutia m(static_cast<float>(raw.pt.x),
                  static_cast<float>(raw.pt.y),
                  raw.angle,
                  raw.type,
                  quality);

        res.minutiae.push_back(m);

        if (m.type == MinutiaType::Ending) {
            ++res.filterStats.retainedEndings;
        } else if (m.type == MinutiaType::Bifurcation) {
            ++res.filterStats.retainedBifurcations;
        }
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    res.processingTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    res.success = true;
    res.statusMessage = "Extraction completed successfully";

    return res;
}

ExtractionResult MinutiaeExtractor::extract(const cv::Mat& inputImage, const ImageEnhancer& enhancer) const {
    const auto enhancement = enhancer.process(inputImage);
    if (!enhancement.success) {
        ExtractionResult res;
        res.success = false;
        res.statusMessage = "Enhancement failed: " + enhancement.statusMessage;
        return res;
    }
    return extract(enhancement);
}

cv::Mat MinutiaeExtractor::visualizeMinutiae(const cv::Mat& background,
                                             const std::vector<Minutia>& minutiae,
                                             const cv::Mat& skeleton) const {
    cv::Mat vis;
    if (background.channels() == 1) {
        cv::cvtColor(background, vis, cv::COLOR_GRAY2BGR);
    } else {
        vis = background.clone();
    }

    // Optional faint skeleton overlay
    if (!skeleton.empty()) {
        const cv::Vec3b skelColor(220, 200, 160); // Faint bluish-gray skeleton
        for (int y = 0; y < skeleton.rows; ++y) {
            const uchar* pSkel = skeleton.ptr<uchar>(y);
            cv::Vec3b* pVis = vis.ptr<cv::Vec3b>(y);
            for (int x = 0; x < skeleton.cols; ++x) {
                if (pSkel[x] > 0) {
                    pVis[x] = skelColor;
                }
            }
        }
    }

    // Draw minutiae circles and directional orientation rays
    const cv::Scalar endingColor(0, 0, 255);       // Red for Ridge Ending
    const cv::Scalar bifurcColor(0, 200, 0);       // Green for Bifurcation
    constexpr float kRayLength = 10.0f;

    for (const auto& m : minutiae) {
        const cv::Point pt(cvRound(m.x), cvRound(m.y));
        const cv::Scalar color = (m.type == MinutiaType::Ending) ? endingColor : bifurcColor;

        // Draw minutia circle
        cv::circle(vis, pt, 3, color, 1, cv::LINE_AA);

        // Draw directional tail indicating ridge exit orientation
        const cv::Point rayEnd(cvRound(m.x + kRayLength * std::cos(m.angle)),
                               cvRound(m.y + kRayLength * std::sin(m.angle)));
        cv::line(vis, pt, rayEnd, color, 1, cv::LINE_AA);
    }

    return vis;
}

} // namespace kipsma::fingerprint
