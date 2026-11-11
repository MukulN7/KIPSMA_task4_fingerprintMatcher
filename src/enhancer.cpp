#include "kipsma_fingerprint/enhancer.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace kipsma::fingerprint {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Creates an even-symmetric Gabor filter kernel with zero DC component
cv::Mat createGaborKernel(int radius, float theta, float freq, float sigmaX, float sigmaY) {
    const int ksize = 2 * radius + 1;
    cv::Mat kernel(ksize, ksize, CV_32FC1);

    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);
    float sumVal = 0.0f;

    for (int y = -radius; y <= radius; ++y) {
        float* pRow = kernel.ptr<float>(y + radius);
        for (int x = -radius; x <= radius; ++x) {
            // Coordinate rotation
            const float xTheta =  x * cosT + y * sinT;
            const float yTheta = -x * sinT + y * cosT;

            const float gaussian = std::exp(-0.5f * ((xTheta * xTheta) / (sigmaX * sigmaX) +
                                                     (yTheta * yTheta) / (sigmaY * sigmaY)));
            const float val = gaussian * std::cos(2.0f * kPi * freq * xTheta);
            pRow[x + radius] = val;
            sumVal += val;
        }
    }

    // Zero-mean the kernel to remove DC bias
    const float meanVal = sumVal / static_cast<float>(ksize * ksize);
    kernel -= meanVal;

    return kernel;
}

// Estimates dominant frequency by orthogonal 1D profile projection over a window
float estimateBlockFrequency(const cv::Mat& patch, float thetaRidge, float minFreq, float maxFreq, float defaultFreq) {
    // We project along the ridge normal: thetaNormal = thetaRidge - PI/2
    const float thetaNormal = thetaRidge - (kPi / 2.0f);
    const int len = std::min(patch.cols, patch.rows);
    if (len < 16) return defaultFreq;

    const float cosN = std::cos(thetaNormal);
    const float sinN = std::sin(thetaNormal);
    const float cx = patch.cols * 0.5f;
    const float cy = patch.rows * 0.5f;

    std::vector<float> profile(len, 0.0f);
    std::vector<int> counts(len, 0);

    for (int y = 0; y < patch.rows; ++y) {
        const float* pRow = patch.ptr<float>(y);
        for (int x = 0; x < patch.cols; ++x) {
            const float proj = (x - cx) * cosN + (y - cy) * sinN + (len * 0.5f);
            const int idx = static_cast<int>(std::round(proj));
            if (idx >= 0 && idx < len) {
                profile[idx] += pRow[x];
                counts[idx]++;
            }
        }
    }

    for (int i = 0; i < len; ++i) {
        if (counts[i] > 0) profile[i] /= counts[i];
    }

    // 3-tap moving average filter on profile to suppress high-frequency image noise
    if (len >= 5) {
        std::vector<float> smooth(len);
        for (int i = 0; i < len; ++i) {
            float sum = 0.0f;
            int c = 0;
            for (int k = -1; k <= 1; ++k) {
                if (i + k >= 0 && i + k < len) {
                    sum += profile[i + k];
                    c++;
                }
            }
            smooth[i] = sum / c;
        }
        profile = std::move(smooth);
    }

    // Detect local peaks in the 1D projection
    std::vector<int> peaks;
    for (int i = 1; i < len - 1; ++i) {
        if (profile[i] > profile[i - 1] && profile[i] >= profile[i + 1]) {
            peaks.push_back(i);
        }
    }

    if (peaks.size() < 2) return defaultFreq;

    std::vector<float> peakGaps;
    for (size_t i = 1; i < peaks.size(); ++i) {
        peakGaps.push_back(static_cast<float>(peaks[i] - peaks[i - 1]));
    }

    std::sort(peakGaps.begin(), peakGaps.end());
    const float medianGap = peakGaps[peakGaps.size() / 2];

    if (medianGap > 0.0f) {
        const float freq = 1.0f / medianGap;
        if (freq >= minFreq && freq <= maxFreq) {
            return freq;
        }
    }

    return defaultFreq;
}

} // namespace

struct ImageEnhancer::Impl {
    EnhancerParams params;

    explicit Impl(const EnhancerParams& p) : params(p) {}
};

ImageEnhancer::ImageEnhancer(const EnhancerParams& params)
    : impl_(std::make_unique<Impl>(params)) {}

ImageEnhancer::~ImageEnhancer() = default;

ImageEnhancer::ImageEnhancer(const ImageEnhancer& other)
    : impl_(std::make_unique<Impl>(other.impl_->params)) {}

ImageEnhancer& ImageEnhancer::operator=(const ImageEnhancer& other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(other.impl_->params);
    }
    return *this;
}

ImageEnhancer::ImageEnhancer(ImageEnhancer&&) noexcept = default;
ImageEnhancer& ImageEnhancer::operator=(ImageEnhancer&&) noexcept = default;

const EnhancerParams& ImageEnhancer::params() const {
    return impl_->params;
}

void ImageEnhancer::setParams(const EnhancerParams& params) {
    impl_->params = params;
}

EnhancementResult ImageEnhancer::process(const cv::Mat& inputImage) const {
    EnhancementResult res;

    // 1. Ingress & Safety Validation
    if (inputImage.empty()) {
        res.success = false;
        res.isUsable = false;
        res.statusMessage = "Input image is empty";
        return res;
    }

    if (inputImage.cols < 32 || inputImage.rows < 32) {
        res.success = false;
        res.isUsable = false;
        res.statusMessage = "Input image dimensions too small (< 32x32)";
        return res;
    }

    // 2. Grayscale Ingress & Depth Normalization
    cv::Mat gray8;
    if (inputImage.channels() == 3) {
        cv::cvtColor(inputImage, gray8, cv::COLOR_BGR2GRAY);
    } else if (inputImage.channels() == 4) {
        cv::cvtColor(inputImage, gray8, cv::COLOR_BGRA2GRAY);
    } else {
        if (inputImage.depth() == CV_16U) {
            inputImage.convertTo(gray8, CV_8U, 1.0 / 256.0);
        } else if (inputImage.depth() == CV_32F || inputImage.depth() == CV_64F) {
            inputImage.convertTo(gray8, CV_8U, 255.0);
        } else {
            gray8 = inputImage.clone();
        }
    }

    // 3. Auto-Polarity Detection
    // Standard impression: ridges are dark, valleys/platen background are light
    // Inverted impression: ridges are bright, background is dark
    if (impl_->params.autoPolarity) {
        const int marginX = std::max(2, gray8.cols / 20);
        const int marginY = std::max(2, gray8.rows / 20);

        // Sample border perimeter (expected to be background platen)
        double borderSum = 0.0;
        int borderCount = 0;

        for (int y = 0; y < gray8.rows; ++y) {
            const uchar* pRow = gray8.ptr<uchar>(y);
            for (int x = 0; x < gray8.cols; ++x) {
                if (x < marginX || x >= gray8.cols - marginX || y < marginY || y >= gray8.rows - marginY) {
                    borderSum += pRow[x];
                    borderCount++;
                }
            }
        }

        const double borderMean = borderCount > 0 ? (borderSum / borderCount) : 255.0;

        // Sample center
        const cv::Rect centerRect(gray8.cols / 4, gray8.rows / 4, gray8.cols / 2, gray8.rows / 2);
        const double centerMean = cv::mean(gray8(centerRect))[0];

        // If background border is dark and center has brighter ridges, invert
        if (borderMean < 100.0 && centerMean > (borderMean + 15.0)) {
            cv::bitwise_not(gray8, gray8);
        }
    }

    res.grayscaleImage = gray8.clone();

    // 4. Local Contrast Normalization
    cv::Mat img32F;
    gray8.convertTo(img32F, CV_32FC1, 1.0 / 255.0);

    const int bSize = std::max(8, impl_->params.blockSize);
    const cv::Size blurSize(bSize, bSize);

    cv::Mat localMean, localSqMean, localVar, localStd;
    cv::blur(img32F, localMean, blurSize);
    cv::blur(img32F.mul(img32F), localSqMean, blurSize);

    localVar = localSqMean - localMean.mul(localMean);
    cv::max(localVar, 1e-6f, localVar);
    cv::sqrt(localVar, localStd);

    cv::Mat normImage = (img32F - localMean) / (localStd + 0.005f);
    res.normalizedImage = normImage;

    // 5. Foreground Segmentation (ROI Mask)
    cv::Mat mask(gray8.size(), CV_8UC1);
    for (int y = 0; y < gray8.rows; ++y) {
        const float* pVar = localVar.ptr<float>(y);
        uchar* pMask = mask.ptr<uchar>(y);
        for (int x = 0; x < gray8.cols; ++x) {
            pMask[x] = (pVar[x] > impl_->params.varThreshold) ? 255 : 0;
        }
    }

    // Clean up mask morphology
    const cv::Mat closeElem = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(11, 11));
    const cv::Mat openElem = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, closeElem);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, openElem);

    // Erode boundary margin to eliminate fuzzy/unreliable perimeter regions
    if (impl_->params.maskErosionRadius > 0) {
        const int erodeK = 2 * impl_->params.maskErosionRadius + 1;
        const cv::Mat erodeElem = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(erodeK, erodeK));
        cv::morphologyEx(mask, mask, cv::MORPH_ERODE, erodeElem);
    }

    res.foregroundMask = mask;

    // 6. Ridge Orientation Field & Coherence
    cv::Mat Gx, Gy;
    cv::Sobel(normImage, Gx, CV_32F, 1, 0, 3);
    cv::Sobel(normImage, Gy, CV_32F, 0, 1, 3);

    cv::Mat Jxx = Gx.mul(Gx) - Gy.mul(Gy);
    cv::Mat Jyy = 2.0f * Gx.mul(Gy);
    cv::Mat J0  = Gx.mul(Gx) + Gy.mul(Gy);

    const int smoothK = std::max(3, (impl_->params.orientSmoothSize | 1));
    cv::Mat Jxx_smooth, Jyy_smooth, J0_smooth;
    cv::GaussianBlur(Jxx, Jxx_smooth, cv::Size(smoothK, smoothK), 0);
    cv::GaussianBlur(Jyy, Jyy_smooth, cv::Size(smoothK, smoothK), 0);
    cv::GaussianBlur(J0, J0_smooth, cv::Size(smoothK, smoothK), 0);

    cv::Mat orientMap(gray8.size(), CV_32FC1, cv::Scalar(0.0f));
    cv::Mat coherenceMap(gray8.size(), CV_32FC1, cv::Scalar(0.0f));
    double sumCoherenceInFg = 0.0;
    int fgPixelsCount = 0;

    for (int y = 0; y < gray8.rows; ++y) {
        const float* pJxx = Jxx_smooth.ptr<float>(y);
        const float* pJyy = Jyy_smooth.ptr<float>(y);
        const float* pJ0  = J0_smooth.ptr<float>(y);
        const uchar* pM   = mask.ptr<uchar>(y);

        float* pOrient = orientMap.ptr<float>(y);
        float* pCoher  = coherenceMap.ptr<float>(y);

        for (int x = 0; x < gray8.cols; ++x) {
            // Gradient orientation
            const float thetaGrad = 0.5f * std::atan2(pJyy[x], pJxx[x]);
            // Ridge orientation is orthogonal to gradient
            float thetaRidge = thetaGrad + (kPi / 2.0f);
            while (thetaRidge < 0.0f) thetaRidge += kPi;
            while (thetaRidge >= kPi) thetaRidge -= kPi;

            pOrient[x] = thetaRidge;

            const float normMag = std::sqrt(pJxx[x] * pJxx[x] + pJyy[x] * pJyy[x]);
            const float coh = normMag / (pJ0[x] + 1e-5f);
            pCoher[x] = std::min(1.0f, std::max(0.0f, coh));

            if (pM[x] > 0) {
                sumCoherenceInFg += pCoher[x];
                fgPixelsCount++;
            }
        }
    }

    res.orientationMap = orientMap;
    res.coherenceMap = coherenceMap;

    // 7. Ridge Frequency Estimation
    cv::Mat freqMap(gray8.size(), CV_32FC1, cv::Scalar(impl_->params.defaultFrequency));
    float sumFreq = 0.0f;
    int freqCount = 0;
    const int winSize = std::max(16, impl_->params.freqWindowSize);
    const int halfWin = winSize / 2;

    for (int by = 0; by < gray8.rows; by += bSize) {
        for (int bx = 0; bx < gray8.cols; bx += bSize) {
            const int bw = std::min(bSize, gray8.cols - bx);
            const int bh = std::min(bSize, gray8.rows - by);
            const cv::Rect blockRect(bx, by, bw, bh);

            // Check if block center is in foreground and coherent
            const int cx = bx + bw / 2;
            const int cy = by + bh / 2;
            if (mask.at<uchar>(cy, cx) > 0 && coherenceMap.at<float>(cy, cx) >= impl_->params.coherenceThreshold) {
                const float blockOrient = orientMap.at<float>(cy, cx);

                // Sample a larger window centered at (cx, cy) to capture multiple ridge cycles
                const int wx0 = std::max(0, cx - halfWin);
                const int wy0 = std::max(0, cy - halfWin);
                const int wx1 = std::min(gray8.cols, cx + halfWin);
                const int wy1 = std::min(gray8.rows, cy + halfWin);
                const cv::Rect winRect(wx0, wy0, wx1 - wx0, wy1 - wy0);

                const float f = estimateBlockFrequency(normImage(winRect), blockOrient,
                                                       impl_->params.minFrequency,
                                                       impl_->params.maxFrequency,
                                                       impl_->params.defaultFrequency);
                freqMap(blockRect).setTo(f);
                sumFreq += f;
                freqCount++;
            }
        }
    }

    // Smooth frequency field to prevent block discontinuities
    cv::blur(freqMap, freqMap, cv::Size(bSize, bSize));
    res.frequencyMap = freqMap;

    const float meanFreq = freqCount > 0 ? (sumFreq / freqCount) : impl_->params.defaultFrequency;

    // 8. Orientation-Aware Gabor Enhancement
    // Quantize orientation into 16 discrete angles for efficient, seamless filtering
    constexpr int kNumAngles = 16;
    std::vector<cv::Mat> gaborFiltered(kNumAngles);

    const int gaborRadius = impl_->params.gaborKernelRadius;
    const float sigmaX = impl_->params.gaborSigmaX;
    const float sigmaY = impl_->params.gaborSigmaY;

    for (int k = 0; k < kNumAngles; ++k) {
        const float angle = k * kPi / static_cast<float>(kNumAngles);
        const cv::Mat kernel = createGaborKernel(gaborRadius, angle, meanFreq, sigmaX, sigmaY);
        cv::filter2D(normImage, gaborFiltered[k], CV_32F, kernel, cv::Point(-1, -1), 0, cv::BORDER_DEFAULT);
    }

    // Assemble enhanced image from the matched orientation slice
    cv::Mat enhanced32F(gray8.size(), CV_32FC1, cv::Scalar(0.0f));
    const float angleStep = kPi / static_cast<float>(kNumAngles);

    for (int y = 0; y < gray8.rows; ++y) {
        const float* pOrient = orientMap.ptr<float>(y);
        const uchar* pM = mask.ptr<uchar>(y);
        float* pOut = enhanced32F.ptr<float>(y);

        for (int x = 0; x < gray8.cols; ++x) {
            if (pM[x] == 0) {
                pOut[x] = 0.0f; // Background
                continue;
            }

            int bin = static_cast<int>(std::round(pOrient[x] / angleStep)) % kNumAngles;
            if (bin < 0) bin += kNumAngles;

            pOut[x] = gaborFiltered[bin].at<float>(y, x);
        }
    }

    // Robust contrast scaling to [0, 255] CV_8UC1 using foreground distribution
    cv::Scalar fgMean(0.0), fgStd(1.0);
    if (fgPixelsCount > 0) {
        cv::meanStdDev(enhanced32F, fgMean, fgStd, mask);
    }
    const float meanVal = static_cast<float>(fgMean[0]);
    const float stdVal  = std::max(static_cast<float>(fgStd[0]), 1e-4f);
    const float clipLimit = 2.5f * stdVal;
    const float lowVal  = meanVal - clipLimit;
    const float highVal = meanVal + clipLimit;
    const float spanVal = highVal - lowVal;

    cv::Mat enhanced8(gray8.size(), CV_8UC1, cv::Scalar(255));
    if (spanVal > 1e-5f) {
        for (int y = 0; y < gray8.rows; ++y) {
            const float* pIn = enhanced32F.ptr<float>(y);
            const uchar* pM  = mask.ptr<uchar>(y);
            uchar* pOut = enhanced8.ptr<uchar>(y);
            for (int x = 0; x < gray8.cols; ++x) {
                if (pM[x] > 0) {
                    const float clamped = std::clamp(pIn[x], lowVal, highVal);
                    const float normalized = (clamped - lowVal) / spanVal;
                    pOut[x] = cv::saturate_cast<uchar>(normalized * 255.0f);
                } else {
                    pOut[x] = 255; // Clean white background outside ROI
                }
            }
        }
    }
    res.enhancedImage = enhanced8;

    // 9. Quality Assessment & Decision Metrics
    const float totalPixels = static_cast<float>(gray8.cols * gray8.rows);
    res.foregroundCoverage = totalPixels > 0.0f ? (static_cast<float>(fgPixelsCount) / totalPixels) : 0.0f;
    res.averageCoherence = fgPixelsCount > 0 ? static_cast<float>(sumCoherenceInFg / fgPixelsCount) : 0.0f;

    // Engineering score [0.0 .. 100.0]
    res.qualityScore = std::min(100.0f, std::max(0.0f,
        100.0f * (0.35f * res.foregroundCoverage + 0.65f * res.averageCoherence)));

    // Usability criteria
    res.isUsable = (res.foregroundCoverage >= 0.12f) &&
                   (res.averageCoherence >= impl_->params.coherenceThreshold) &&
                   (res.qualityScore >= 18.0f);

    res.success = true;
    res.statusMessage = res.isUsable ? "Enhancement successful"
                                     : "Unusable: low foreground coverage or poor ridge coherence";

    return res;
}

cv::Mat ImageEnhancer::visualizeOrientation(const cv::Mat& image,
                                            const cv::Mat& orientationMap,
                                            const cv::Mat& mask,
                                            int step,
                                            int lineLength) const {
    cv::Mat vis;
    if (image.channels() == 1) {
        cv::cvtColor(image, vis, cv::COLOR_GRAY2BGR);
    } else {
        vis = image.clone();
    }

    if (orientationMap.empty()) return vis;

    const cv::Scalar lineColor(0, 0, 255); // Red orientation vectors
    const float halfLen = lineLength * 0.5f;

    for (int y = step / 2; y < vis.rows; y += step) {
        for (int x = step / 2; x < vis.cols; x += step) {
            if (!mask.empty() && mask.at<uchar>(y, x) == 0) continue;

            const float theta = orientationMap.at<float>(y, x);
            const float dx = halfLen * std::cos(theta);
            const float dy = halfLen * std::sin(theta);

            const cv::Point pt1(cvRound(x - dx), cvRound(y - dy));
            const cv::Point pt2(cvRound(x + dx), cvRound(y + dy));

            cv::line(vis, pt1, pt2, lineColor, 1, cv::LINE_AA);
        }
    }

    return vis;
}

cv::Mat ImageEnhancer::visualizeFrequency(const cv::Mat& frequencyMap,
                                          const cv::Mat& mask) const {
    cv::Mat vis(frequencyMap.size(), CV_8UC3, cv::Scalar(255, 255, 255));
    if (frequencyMap.empty()) return vis;

    const float minF = impl_->params.minFrequency;
    const float maxF = impl_->params.maxFrequency;
    const float span = std::max(1e-5f, maxF - minF);

    cv::Mat grayFreq(frequencyMap.size(), CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < frequencyMap.rows; ++y) {
        const float* pF = frequencyMap.ptr<float>(y);
        const uchar* pM = mask.empty() ? nullptr : mask.ptr<uchar>(y);
        uchar* pG = grayFreq.ptr<uchar>(y);
        for (int x = 0; x < frequencyMap.cols; ++x) {
            if (pM && pM[x] == 0) {
                pG[x] = 0;
            } else {
                const float norm = std::clamp((pF[x] - minF) / span, 0.0f, 1.0f);
                pG[x] = cv::saturate_cast<uchar>(norm * 255.0f);
            }
        }
    }

    cv::Mat colorMap;
    cv::applyColorMap(grayFreq, colorMap, cv::COLORMAP_JET);

    for (int y = 0; y < frequencyMap.rows; ++y) {
        const uchar* pM = mask.empty() ? nullptr : mask.ptr<uchar>(y);
        const cv::Vec3b* pC = colorMap.ptr<cv::Vec3b>(y);
        cv::Vec3b* pV = vis.ptr<cv::Vec3b>(y);
        for (int x = 0; x < frequencyMap.cols; ++x) {
            if (pM && pM[x] == 0) {
                pV[x] = cv::Vec3b(255, 255, 255);
            } else {
                pV[x] = pC[x];
            }
        }
    }

    return vis;
}

} // namespace kipsma::fingerprint

