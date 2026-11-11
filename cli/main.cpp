#include "kipsma_fingerprint/fingerprint_api.h"
#include "kipsma_fingerprint/enhancer.h"
#include "kipsma_fingerprint/extractor.h"
#include "kipsma_fingerprint/matcher.h"

#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void printHelp() {
    std::cout << "KIPSMA Fingerprint Recognition CLI (Version " << kipsma::fingerprint::GetVersion() << ")\n"
              << "Usage: fingerprint_cli <command> [options]\n\n"
              << "Commands:\n"
              << "  help, -h, --help                 Display this help information\n"
              << "  version, -v, --version           Display the version string\n"
              << "  verify <image1> <image2> [dpi] [threshold]\n"
              << "                                   Perform 1:1 verification returning YES/NO with FingerJetFX + OpenAFIS\n"
              << "  enhance <image> [output_dir]     Enhance fingerprint image and export diagnostic masks\n"
              << "  extract <image> [output_dir]     Extract minutiae template from fingerprint image\n"
              << "  match <image1> <image2> [vis] [--strategy <baseline|local>]\n"
              << "                                   Perform 1:1 match and optionally save visualization\n"
              << "  benchmark <dataset_dir> [report] [--strategy <baseline|local>] [--all-impostors]\n"
              << "                                   Run evaluation benchmark over a dataset directory\n\n"
              << "Examples:\n"
              << "  fingerprint_cli verify probe.tif candidate.tif\n"
              << "  fingerprint_cli verify probe.tif candidate.tif 500 2.0\n"
              << "  fingerprint_cli match img1.tif img2.tif match_overlay.png --strategy local\n"
              << "  fingerprint_cli benchmark D:/Career/KIPSMA/FingerprintMatcher-Data/FVC2002/DB1_B report.json --strategy local\n";
}

void printVersion() {
    std::cout << "fingerprint_cli version " << kipsma::fingerprint::GetVersion() << "\n";
}

struct ImpressionItem {
    std::string path;
    std::string filename;
    int fingerId = 0;
    int impressionId = 0;
    kipsma::fingerprint::FingerprintTemplate tpl;
    double extractTimeMs = 0.0;
};

struct PairResult {
    int probeFinger;
    int probeImpression;
    int candFinger;
    int candImpression;
    float score;
    int matchedPairs;
    float rmsError;
    bool isMatch;
    bool isGenuine;
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp();
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        printHelp();
        return 0;
    }

    if (cmd == "version" || cmd == "-v" || cmd == "--version") {
        printVersion();
        return 0;
    }

    if (cmd == "verify") {
        if (argc < 4) {
            std::cerr << "Error: 'verify' command requires two image paths.\n"
                      << "Usage: fingerprint_cli verify <image1> <image2> [dpi] [threshold]\n";
            return 1;
        }
        const std::string imgPath1 = argv[2];
        const std::string imgPath2 = argv[3];
        unsigned short fallbackDpi = 500;
        float threshold = 2.0f;

        if (argc >= 5) {
            fallbackDpi = static_cast<unsigned short>(std::stoi(argv[4]));
        }
        if (argc >= 6) {
            threshold = std::stof(argv[5]);
        }

        kipsma::fingerprint::FingerprintEngine engine;
        const auto vRes = engine.verifyFiles(imgPath1, imgPath2, fallbackDpi, threshold);

        std::cout << "=== 1:1 Biometric Fingerprint Verification ===\n"
                  << "  Probe Image     : " << imgPath1 << "\n"
                  << "  Candidate Image : " << imgPath2 << "\n"
                  << "  Fallback DPI    : " << fallbackDpi << "\n"
                  << "  Decision Cutoff : " << threshold << "\n";

        if (!vRes.probeOperations.empty()) {
            std::cout << "  Probe Preprocessing:\n";
            for (const auto& op : vRes.probeOperations) {
                std::cout << "    - " << op << "\n";
            }
        }
        if (!vRes.candOperations.empty()) {
            std::cout << "  Candidate Preprocessing:\n";
            for (const auto& op : vRes.candOperations) {
                std::cout << "    - " << op << "\n";
            }
        }

        std::cout << "  Status Message  : " << vRes.message << "\n";

        if (vRes.decision == kipsma::fingerprint::VerificationDecision::ERROR_UNSUPPORTED_INPUT ||
            vRes.decision == kipsma::fingerprint::VerificationDecision::ERROR_EXTRACTION_FAILED ||
            vRes.decision == kipsma::fingerprint::VerificationDecision::ERROR_MATCHING_FAILED) {
            std::cout << "  Match Result    : ERROR (Verification could not be performed)\n"
                      << "==============================================\n"
                      << "VERIFICATION DECISION: ERROR (" << vRes.message << ")\n"
                      << "==============================================\n";
            return 2;
        }

        std::cout << "  Match Score     : " << vRes.score << " / 100\n"
                  << "==============================================\n"
                  << "VERIFICATION DECISION: " << (vRes.isMatch ? "YES" : "NO") << "\n"
                  << "==============================================\n";

        return 0;
    }

    if (cmd == "enhance") {
        if (argc < 3) {
            std::cerr << "Error: 'enhance' command requires an input image path.\n"
                      << "Usage: fingerprint_cli enhance <image> [output_dir]\n";
            return 1;
        }
        const std::string imgPath = argv[2];
        const std::string outDir = (argc >= 4) ? argv[3] : ".";

        cv::Mat img = cv::imread(imgPath, cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            std::cerr << "Error: Failed to load image: " << imgPath << "\n";
            return 1;
        }

        kipsma::fingerprint::ImageEnhancer enhancer;
        const auto res = enhancer.process(img);

        if (!res.success) {
            std::cerr << "Error: Enhancement failed: " << res.statusMessage << "\n";
            return 1;
        }

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "=== Fingerprint Enhancement Summary ===\n"
                  << "  Input Image        : " << imgPath << " (" << img.cols << "x" << img.rows << ", " << img.channels() << " ch)\n"
                  << "  Foreground Coverage: " << (res.foregroundCoverage * 100.0f) << " %\n"
                  << "  Average Coherence  : " << res.averageCoherence << "\n"
                  << "  Quality Score      : " << res.qualityScore << " / 100\n"
                  << "  Usable for Match   : " << (res.isUsable ? "YES" : "NO") << "\n"
                  << "  Status             : " << res.statusMessage << "\n";

        fs::create_directories(outDir);
        const std::string prefix = outDir + "/";
        cv::imwrite(prefix + "gray.png", res.grayscaleImage);
        cv::imwrite(prefix + "mask.png", res.foregroundMask);
        cv::imwrite(prefix + "enhanced.png", res.enhancedImage);

        cv::Mat visOrient = enhancer.visualizeOrientation(res.grayscaleImage, res.orientationMap, res.foregroundMask);
        cv::imwrite(prefix + "orientation.png", visOrient);

        cv::Mat visFreq = enhancer.visualizeFrequency(res.frequencyMap, res.foregroundMask);
        cv::imwrite(prefix + "frequency.png", visFreq);

        std::cout << "  Saved diagnostic outputs (gray.png, mask.png, orientation.png, frequency.png, enhanced.png) to: " << outDir << "\n";
        return 0;
    }

    if (cmd == "extract") {
        if (argc < 3) {
            std::cerr << "Error: 'extract' command requires an input image path.\n"
                      << "Usage: fingerprint_cli extract <image> [output_dir]\n";
            return 1;
        }
        const std::string imgPath = argv[2];
        const std::string outDir = (argc >= 4) ? argv[3] : ".";

        cv::Mat img = cv::imread(imgPath, cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            std::cerr << "Error: Failed to load image: " << imgPath << "\n";
            return 1;
        }

        const auto startTotal = std::chrono::high_resolution_clock::now();

        kipsma::fingerprint::ImageEnhancer enhancer;
        const auto enhRes = enhancer.process(img);
        if (!enhRes.success) {
            std::cerr << "Error: Enhancement failed: " << enhRes.statusMessage << "\n";
            return 1;
        }

        kipsma::fingerprint::MinutiaeExtractor extractor;
        const auto extRes = extractor.extract(enhRes);
        if (!extRes.success) {
            std::cerr << "Error: Extraction failed: " << extRes.statusMessage << "\n";
            return 1;
        }

        const auto endTotal = std::chrono::high_resolution_clock::now();
        const double totalTimeMs = std::chrono::duration<double, std::milli>(endTotal - startTotal).count();

        const auto& stats = extRes.filterStats;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "=== Fingerprint Minutiae Extraction Summary ===\n"
                  << "  Input Image              : " << imgPath << " (" << img.cols << "x" << img.rows << ")\n"
                  << "  Foreground Coverage      : " << (enhRes.foregroundCoverage * 100.0f) << " %\n"
                  << "  Quality Score            : " << enhRes.qualityScore << " / 100\n"
                  << "  --- Raw Detections (Rutovitz Crossing Number) ---\n"
                  << "    Raw Ridge Endings      : " << stats.rawEndings << "\n"
                  << "    Raw Bifurcations       : " << stats.rawBifurcations << "\n"
                  << "    Raw Total Candidates   : " << stats.rawTotal() << "\n"
                  << "  --- Filtering Stages ---\n"
                  << "    Removed Boundary Margin: " << stats.removedBorderMargin << "\n"
                  << "    Removed Low Coherence  : " << stats.removedLowCoherence << "\n"
                  << "    Removed Broken Ridges  : " << stats.removedBrokenRidges << "\n"
                  << "    Removed Short Spurs    : " << stats.removedSpurs << "\n"
                  << "    Removed Near-Duplicates: " << stats.removedDuplicateClusters << "\n"
                  << "    Total Filtered Out     : " << stats.totalRemoved() << "\n"
                  << "  --- Final Retained Minutiae ---\n"
                  << "    Valid Ridge Endings    : " << stats.retainedEndings << "\n"
                  << "    Valid Bifurcations     : " << stats.retainedBifurcations << "\n"
                  << "    Total Retained Minutiae: " << stats.retainedTotal() << "\n"
                  << "  --- Performance ---\n"
                  << "    Extraction Time        : " << extRes.processingTimeMs << " ms\n"
                  << "    Total Pipeline Time    : " << totalTimeMs << " ms\n";

        fs::create_directories(outDir);
        const std::string prefix = outDir + "/";
        cv::imwrite(prefix + "binarized.png", extRes.binarizedImage);
        cv::imwrite(prefix + "skeleton.png", extRes.skeletonImage);

        cv::Mat visOverlay = extractor.visualizeMinutiae(enhRes.grayscaleImage, extRes.minutiae, extRes.skeletonImage);
        cv::imwrite(prefix + "minutiae_overlay.png", visOverlay);

        std::cout << "  Saved diagnostic outputs (binarized.png, skeleton.png, minutiae_overlay.png) to: " << outDir << "\n";
        return 0;
    }

    if (cmd == "match") {
        if (argc < 4) {
            std::cerr << "Error: 'match' command requires two image paths.\n"
                      << "Usage: fingerprint_cli match <image1> <image2> [vis_out]\n";
            return 1;
        }
        const std::string imgPath1 = argv[2];
        const std::string imgPath2 = argv[3];
        std::string visOut = "";
        std::string strategyStr = "baseline";
        for (int i = 4; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--strategy" || arg == "-s") {
                if (i + 1 < argc) strategyStr = argv[++i];
            } else if (visOut.empty() && arg.rfind("--", 0) != 0) {
                visOut = arg;
            }
        }

        cv::Mat img1 = cv::imread(imgPath1, cv::IMREAD_UNCHANGED);
        cv::Mat img2 = cv::imread(imgPath2, cv::IMREAD_UNCHANGED);

        if (img1.empty()) {
            std::cerr << "Error: Failed to load probe image: " << imgPath1 << "\n";
            return 1;
        }
        if (img2.empty()) {
            std::cerr << "Error: Failed to load candidate image: " << imgPath2 << "\n";
            return 1;
        }

        kipsma::fingerprint::FingerprintEngine engine;
        auto params = engine.getMatcherParams();
        if (strategyStr == "baseline" || strategyStr == "delaunay") {
            params.strategy = kipsma::fingerprint::MatcherStrategy::DelaunayTripletBaseline;
        } else {
            params.strategy = kipsma::fingerprint::MatcherStrategy::LocalNeighborhoodConsensus;
        }
        engine.setMatcherParams(params);

        const auto t0 = std::chrono::high_resolution_clock::now();
        const auto tpl1 = engine.extractTemplate(img1);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto tpl2 = engine.extractTemplate(img2);
        const auto t2 = std::chrono::high_resolution_clock::now();

        const auto matchRes = engine.match(tpl1, tpl2);
        const auto t3 = std::chrono::high_resolution_clock::now();

        const double extract1Ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double extract2Ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        const double matchMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
        const double totalMs = std::chrono::duration<double, std::milli>(t3 - t0).count();

        constexpr float kPi = 3.14159265358979323846f;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "=== 1:1 Fingerprint Verification Result ===\n"
                  << "  Probe Image        : " << imgPath1 << " (" << img1.cols << "x" << img1.rows << ", " << tpl1.size() << " minutiae)\n"
                  << "  Candidate Image    : " << imgPath2 << " (" << img2.cols << "x" << img2.rows << ", " << tpl2.size() << " minutiae)\n"
                  << "  Verification Result: " << (matchRes.isMatch ? "[MATCH - GENUINE]" : "[NON-MATCH - IMPOSTOR]") << "\n"
                  << "  Match Score        : " << matchRes.score << " / 100.0\n"
                  << "  Matched Pairs      : " << matchRes.matchedPairs << " pairs\n"
                  << "  Spatial RMS Error  : " << matchRes.rmsError << " px\n"
                  << "  Estimated Rotation : " << (matchRes.rotation * 180.0f / kPi) << " deg (" << matchRes.rotation << " rad)\n"
                  << "  Estimated Trans (X): " << matchRes.translationX << " px\n"
                  << "  Estimated Trans (Y): " << matchRes.translationY << " px\n"
                  << "  Status Message     : " << matchRes.message << "\n"
                  << "  --- Performance Latency ---\n"
                  << "    Probe Extraction : " << extract1Ms << " ms\n"
                  << "    Cand Extraction  : " << extract2Ms << " ms\n"
                  << "    1:1 Match Time   : " << matchMs << " ms\n"
                  << "    Total Latency    : " << totalMs << " ms\n";

        if (!visOut.empty()) {
            kipsma::fingerprint::MinutiaeMatcher matcher;
            cv::Mat vis = matcher.visualizeMatch(img1, img2, tpl1, tpl2, matchRes);
            cv::imwrite(visOut, vis);
            std::cout << "  Saved side-by-side match overlay to: " << visOut << "\n";
        }

        return 0;
    }

    if (cmd == "benchmark") {
        if (argc < 3) {
            std::cerr << "Error: 'benchmark' command requires a dataset directory path.\n"
                      << "Usage: fingerprint_cli benchmark <dataset_dir> [report.json]\n";
            return 1;
        }
        const std::string datasetDir = argv[2];
        std::string reportPath = "";
        std::string strategyStr = "baseline";
        bool allImpostors = false;
        for (int a = 3; a < argc; ++a) {
            const std::string argStr = argv[a];
            if (argStr == "--all-impostors" || argStr == "-all") {
                allImpostors = true;
            } else if (argStr == "--strategy" || argStr == "-s") {
                if (a + 1 < argc) strategyStr = argv[++a];
            } else if (reportPath.empty() && argStr.rfind("--", 0) != 0) {
                reportPath = argStr;
            }
        }

        if (!fs::exists(datasetDir) || !fs::is_directory(datasetDir)) {
            std::cerr << "Error: Dataset directory does not exist: " << datasetDir << "\n";
            return 1;
        }

        kipsma::fingerprint::FingerprintEngine engine;
        auto params = engine.getMatcherParams();
        if (strategyStr == "baseline" || strategyStr == "delaunay") {
            params.strategy = kipsma::fingerprint::MatcherStrategy::DelaunayTripletBaseline;
        } else {
            params.strategy = kipsma::fingerprint::MatcherStrategy::LocalNeighborhoodConsensus;
        }
        engine.setMatcherParams(params);

        std::cout << "=== KIPSMA Fingerprint Evaluation Benchmark ===\n"
                  << "  Dataset Directory: " << datasetDir << "\n"
                  << "  Matcher Strategy : " << (params.strategy == kipsma::fingerprint::MatcherStrategy::DelaunayTripletBaseline ? "DelaunayTripletBaseline" : "LocalNeighborhoodConsensus") << "\n";

        // Regex for standard FVC naming: e.g. 101_1.tif
        const std::regex fvcRegex(R"(^(\d+)_(\d+)\.(tif|tiff|bmp|png|jpg)$)", std::regex_constants::icase);

        std::vector<ImpressionItem> items;

        for (const auto& entry : fs::directory_iterator(datasetDir)) {
            if (!entry.is_regular_file()) continue;
            const std::string filename = entry.path().filename().string();
            std::smatch match;
            if (std::regex_match(filename, match, fvcRegex)) {
                ImpressionItem it;
                it.path = entry.path().string();
                it.filename = filename;
                it.fingerId = std::stoi(match[1].str());
                it.impressionId = std::stoi(match[2].str());
                items.push_back(it);
            }
        }

        if (items.empty()) {
            std::cerr << "Error: No matching fingerprint impressions found in: " << datasetDir << "\n";
            return 1;
        }

        std::sort(items.begin(), items.end(), [](const ImpressionItem& a, const ImpressionItem& b) {
            if (a.fingerId != b.fingerId) return a.fingerId < b.fingerId;
            return a.impressionId < b.impressionId;
        });

        std::cout << "  Discovered Impressions: " << items.size() << " files across "
                  << (items.back().fingerId - items.front().fingerId + 1) << " fingers.\n";
        std::cout << "  Extracting templates...\n";

        double totalExtractMs = 0.0;
        for (size_t i = 0; i < items.size(); ++i) {
            cv::Mat img = cv::imread(items[i].path, cv::IMREAD_UNCHANGED);
            const auto tStart = std::chrono::high_resolution_clock::now();
            items[i].tpl = engine.extractTemplate(img);
            const auto tEnd = std::chrono::high_resolution_clock::now();
            items[i].extractTimeMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            totalExtractMs += items[i].extractTimeMs;
        }

        const double avgExtractMs = totalExtractMs / static_cast<double>(items.size());
        std::cout << "  Feature Extraction Complete: Avg " << std::fixed << std::setprecision(2)
                  << avgExtractMs << " ms per image.\n\n";

        // Generate Genuine Pairs: all (i, j) with same fingerId and i < j
        std::vector<PairResult> genuineResults;
        std::vector<PairResult> impostorResults;

        const auto matchStart = std::chrono::high_resolution_clock::now();

        // Group by fingerId
        std::map<int, std::vector<size_t>> fingerGroups;
        for (size_t i = 0; i < items.size(); ++i) {
            fingerGroups[items[i].fingerId].push_back(i);
        }

        // Genuine comparisons
        for (const auto& [fId, indices] : fingerGroups) {
            for (size_t i = 0; i < indices.size(); ++i) {
                for (size_t j = i + 1; j < indices.size(); ++j) {
                    const auto& p1 = items[indices[i]];
                    const auto& p2 = items[indices[j]];
                    const auto res = engine.match(p1.tpl, p2.tpl);

                    PairResult pr;
                    pr.probeFinger = p1.fingerId;
                    pr.probeImpression = p1.impressionId;
                    pr.candFinger = p2.fingerId;
                    pr.candImpression = p2.impressionId;
                    pr.score = res.score;
                    pr.matchedPairs = res.matchedPairs;
                    pr.rmsError = res.rmsError;
                    pr.isMatch = res.isMatch;
                    pr.isGenuine = true;
                    genuineResults.push_back(pr);
                }
            }
        }

        // Impostor comparisons
        // Default: first impressions cross-comparison (standard FVC protocol: 45 pairs)
        // If "--all-impostors" or "-all" argument passed: evaluate all distinct-finger cross pairs (2,880 pairs)
        if (allImpostors) {
            std::vector<int> fingerIds;
            for (const auto& [fId, _] : fingerGroups) fingerIds.push_back(fId);

            for (size_t fi = 0; fi < fingerIds.size(); ++fi) {
                for (size_t fj = fi + 1; fj < fingerIds.size(); ++fj) {
                    const auto& group1 = fingerGroups[fingerIds[fi]];
                    const auto& group2 = fingerGroups[fingerIds[fj]];
                    for (size_t i = 0; i < group1.size(); ++i) {
                        for (size_t j = 0; j < group2.size(); ++j) {
                            const auto& p1 = items[group1[i]];
                            const auto& p2 = items[group2[j]];
                            const auto res = engine.match(p1.tpl, p2.tpl);

                            PairResult pr;
                            pr.probeFinger = p1.fingerId;
                            pr.probeImpression = p1.impressionId;
                            pr.candFinger = p2.fingerId;
                            pr.candImpression = p2.impressionId;
                            pr.score = res.score;
                            pr.matchedPairs = res.matchedPairs;
                            pr.rmsError = res.rmsError;
                            pr.isMatch = res.isMatch;
                            pr.isGenuine = false;
                            impostorResults.push_back(pr);
                        }
                    }
                }
            }
        } else {
            std::vector<size_t> firstImpressions;
            for (const auto& [fId, indices] : fingerGroups) {
                if (!indices.empty()) {
                    firstImpressions.push_back(indices.front());
                }
            }

            for (size_t i = 0; i < firstImpressions.size(); ++i) {
                for (size_t j = i + 1; j < firstImpressions.size(); ++j) {
                    const auto& p1 = items[firstImpressions[i]];
                    const auto& p2 = items[firstImpressions[j]];
                    const auto res = engine.match(p1.tpl, p2.tpl);

                    PairResult pr;
                    pr.probeFinger = p1.fingerId;
                    pr.probeImpression = p1.impressionId;
                    pr.candFinger = p2.fingerId;
                    pr.candImpression = p2.impressionId;
                    pr.score = res.score;
                    pr.matchedPairs = res.matchedPairs;
                    pr.rmsError = res.rmsError;
                    pr.isMatch = res.isMatch;
                    pr.isGenuine = false;
                    impostorResults.push_back(pr);
                }
            }
        }

        const auto matchEnd = std::chrono::high_resolution_clock::now();
        const double totalMatchMs = std::chrono::duration<double, std::milli>(matchEnd - matchStart).count();
        const size_t totalComparisons = genuineResults.size() + impostorResults.size();
        const double avgMatchUs = (totalMatchMs * 1000.0) / static_cast<double>(totalComparisons);

        // Score statistics
        auto computeStats = [](const std::vector<PairResult>& pairs) {
            float minS = 1e9f, maxS = -1e9f, sumS = 0.0f;
            for (const auto& p : pairs) {
                minS = std::min(minS, p.score);
                maxS = std::max(maxS, p.score);
                sumS += p.score;
            }
            const float mean = pairs.empty() ? 0.0f : (sumS / static_cast<float>(pairs.size()));
            float varS = 0.0f;
            for (const auto& p : pairs) {
                varS += (p.score - mean) * (p.score - mean);
            }
            const float stddev = pairs.empty() ? 0.0f : std::sqrt(varS / static_cast<float>(pairs.size()));
            return std::make_tuple(minS, maxS, mean, stddev);
        };

        const auto [genMin, genMax, genMean, genStd] = computeStats(genuineResults);
        const auto [impMin, impMax, impMean, impStd] = computeStats(impostorResults);

        // Threshold sweep for ROC, FMR, FNMR, and EER
        struct RocPoint {
            float threshold;
            float fmr;
            float fnmr;
        };
        std::vector<RocPoint> rocCurve;

        float bestEerDiff = 1e9f;
        float bestEer = 100.0f;
        float bestEerThreshold = 0.0f;
        float zeroFmrFnmar = 100.0f;
        float fmr100Fnmr = 100.0f;

        // Fine-grained threshold sweep covering actual score range
        const float sweepMax = std::max(genMax, impMax) + 1.0f;
        const int numSweepSteps = 1000;

        for (int step = 0; step <= numSweepSteps; ++step) {
            const float t = (static_cast<float>(step) / static_cast<float>(numSweepSteps)) * sweepMax;

            // FMR: fraction of impostors with score >= t
            int falseMatches = 0;
            for (const auto& p : impostorResults) {
                if (p.score >= t) ++falseMatches;
            }
            const float fmr = impostorResults.empty() ? 0.0f
                : (100.0f * static_cast<float>(falseMatches) / static_cast<float>(impostorResults.size()));

            // FNMR: fraction of genuines with score < t
            int falseNonMatches = 0;
            for (const auto& p : genuineResults) {
                if (p.score < t) ++falseNonMatches;
            }
            const float fnmr = genuineResults.empty() ? 0.0f
                : (100.0f * static_cast<float>(falseNonMatches) / static_cast<float>(genuineResults.size()));

            rocCurve.push_back({t, fmr, fnmr});

            const float eerDiff = std::abs(fmr - fnmr);
            if (eerDiff < bestEerDiff) {
                bestEerDiff = eerDiff;
                bestEer = (fmr + fnmr) * 0.5f;
                bestEerThreshold = t;
            }

            if (falseMatches == 0 && fnmr < zeroFmrFnmar) {
                zeroFmrFnmar = fnmr;
            }
            if (fmr <= 1.0f && fnmr < fmr100Fnmr) {
                fmr100Fnmr = fnmr;
            }
        }

        std::cout << "--------------------------------------------------------\n"
                  << "  Performance Metrics (FVC Protocol Summary)\n"
                  << "--------------------------------------------------------\n"
                  << "  Genuine Comparisons : " << genuineResults.size() << " pairs\n"
                  << "    Score Range       : [" << genMin << " .. " << genMax << "]\n"
                  << "    Mean +/- StdDev   : " << genMean << " +/- " << genStd << "\n"
                  << "  Impostor Comparisons: " << impostorResults.size() << " pairs\n"
                  << "    Score Range       : [" << impMin << " .. " << impMax << "]\n"
                  << "    Mean +/- StdDev   : " << impMean << " +/- " << impStd << "\n"
                  << "  --- Error Rates ---\n"
                  << "    Equal Error Rate  : " << bestEer << " % (at threshold = " << bestEerThreshold << ")\n"
                  << "    ZeroFMR (FNMR@0%) : " << zeroFmrFnmar << " %\n"
                  << "    FMR100 (FNMR@1%)  : " << fmr100Fnmr << " %\n"
                  << "  --- Latency ---\n"
                  << "    Average 1:1 Match : " << avgMatchUs << " us (" << (avgMatchUs / 1000.0) << " ms)\n"
                  << "    Throughput        : " << static_cast<int>(1000000.0 / avgMatchUs) << " matches/sec\n"
                  << "--------------------------------------------------------\n\n";

        // Display sample ROC Operating Points
        std::cout << "  --- Selected Operating Points (Threshold Sweep) ---\n"
                  << "    Threshold |   FMR (%)  |  FNMR (%)  \n"
                  << "   -----------+------------+------------\n";
        for (float t : {10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 35.0f, 40.0f, 50.0f}) {
            for (const auto& rp : rocCurve) {
                if (std::abs(rp.threshold - t) < 0.25f) {
                    std::cout << "       " << std::setw(6) << rp.threshold << " | "
                              << std::setw(9) << rp.fmr << "% | "
                              << std::setw(9) << rp.fnmr << "%\n";
                    break;
                }
            }
        }
        std::cout << "\n";

        // Export JSON report with full pair-level raw results if requested
        if (!reportPath.empty()) {
            std::ofstream ofs(reportPath);
            if (ofs.is_open()) {
                std::string jsonDir = datasetDir;
                std::replace(jsonDir.begin(), jsonDir.end(), '\\', '/');
                ofs << "{\n"
                    << "  \"dataset\": \"" << jsonDir << "\",\n"
                    << "  \"strategy\": \"" << (params.strategy == kipsma::fingerprint::MatcherStrategy::DelaunayTripletBaseline ? "DelaunayTripletBaseline" : "LocalNeighborhoodConsensus") << "\",\n"
                    << "  \"impressionsCount\": " << items.size() << ",\n"
                    << "  \"genuinePairsCount\": " << genuineResults.size() << ",\n"
                    << "  \"impostorPairsCount\": " << impostorResults.size() << ",\n"
                    << "  \"genuineScore\": {\"min\": " << genMin << ", \"max\": " << genMax
                    << ", \"mean\": " << genMean << ", \"std\": " << genStd << "},\n"
                    << "  \"impostorScore\": {\"min\": " << impMin << ", \"max\": " << impMax
                    << ", \"mean\": " << impMean << ", \"std\": " << impStd << "},\n"
                    << "  \"eer\": " << bestEer << ",\n"
                    << "  \"eerThreshold\": " << bestEerThreshold << ",\n"
                    << "  \"zeroFmrFnmar\": " << zeroFmrFnmar << ",\n"
                    << "  \"fmr100Fnmr\": " << fmr100Fnmr << ",\n"
                    << "  \"avgExtractMs\": " << avgExtractMs << ",\n"
                    << "  \"avgMatchUs\": " << avgMatchUs << ",\n"
                    << "  \"pairs\": [\n";

                auto writePairJson = [&ofs](const PairResult& p, bool isLast) {
                    ofs << "    {\"probeFinger\": " << p.probeFinger
                        << ", \"probeImpression\": " << p.probeImpression
                        << ", \"candFinger\": " << p.candFinger
                        << ", \"candImpression\": " << p.candImpression
                        << ", \"isGenuine\": " << (p.isGenuine ? "true" : "false")
                        << ", \"score\": " << p.score
                        << ", \"matchedPairs\": " << p.matchedPairs
                        << ", \"rmsError\": " << p.rmsError
                        << ", \"isMatch\": " << (p.isMatch ? "true" : "false")
                        << "}" << (isLast ? "" : ",") << "\n";
                };

                for (size_t i = 0; i < genuineResults.size(); ++i) {
                    writePairJson(genuineResults[i], false);
                }
                for (size_t i = 0; i < impostorResults.size(); ++i) {
                    const bool isLast = (i + 1 == impostorResults.size());
                    writePairJson(impostorResults[i], isLast);
                }

                ofs << "  ]\n"
                    << "}\n";
                std::cout << "  Exported benchmark JSON report with pair-level data to: " << reportPath << "\n";
            }
        }

        return 0;
    }

    std::cerr << "Error: Unknown command '" << cmd << "'.\n";
    printHelp();
    return 1;
}
