#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <map>
#include <algorithm>

// OpenCV
#include <opencv2/opencv.hpp>

// FingerJetFX
#include "FJFX.h"

// OpenAFIS
#include "OpenAFIS.h"
#include "TemplateISO19794_2_2005.h"
#include "Match.h"

struct ImageMeta {
    int finger;
    int impression;
    std::string path;
};

struct FingerprintRecord {
    ImageMeta meta;
    bool extraction_ok = false;
    std::string error_msg;
    std::vector<uint8_t> iso_template;
    int minutiae_count = 0;
    double extract_time_ms = 0.0;
};

struct PairEval {
    int probeFinger;
    int probeImpression;
    int candFinger;
    int candImpression;
    bool isGenuine;
    uint8_t score;
    double match_time_us;
    bool comparison_ok;
    std::string error_msg;
};

FingerprintRecord processImage(const ImageMeta& meta, unsigned short dpi = 500) {
    FingerprintRecord rec;
    rec.meta = meta;

    auto t0 = std::chrono::high_resolution_clock::now();

    cv::Mat img = cv::imread(meta.path, cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        rec.extraction_ok = false;
        rec.error_msg = "cv::imread failed";
        return rec;
    }
    if (!img.isContinuous()) {
        img = img.clone();
    }

    const unsigned short height = static_cast<unsigned short>(img.rows);
    const unsigned short width = static_cast<unsigned short>(img.cols);

    std::vector<uint8_t> fmd_buffer(FJFX_FMD_BUFFER_SIZE, 0);
    unsigned int size_of_fmd = static_cast<unsigned int>(fmd_buffer.size());

    int rc = fjfx_create_fmd_from_raw(
        img.data,
        dpi,
        height,
        width,
        FJFX_FMD_ISO_19794_2_2005,
        fmd_buffer.data(),
        &size_of_fmd
    );

    auto t1 = std::chrono::high_resolution_clock::now();
    rec.extract_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (rc != FJFX_SUCCESS) {
        rec.extraction_ok = false;
        rec.error_msg = "fjfx_create_fmd_from_raw rc=" + std::to_string(rc);
        return rec;
    }

    fmd_buffer.resize(size_of_fmd);
    rec.iso_template = std::move(fmd_buffer);
    rec.extraction_ok = true;

    // ISO 19794-2:2005 minutiae count is at offset 27
    if (rec.iso_template.size() >= 28) {
        rec.minutiae_count = static_cast<int>(rec.iso_template[27]);
    }

    return rec;
}

int main(int argc, char** argv) {
    std::string dataset_dir = "D:/Career/KIPSMA/FingerprintMatcher-Data/FVC2002/DB1_B";
    std::string output_json = "D:/Career/KIPSMA/kipsma_fingerprint_matcher/benchmark_results/fingerjet_openafis_fvc2002_db1_b.json";
    unsigned short dpi = 500;

    if (argc >= 2) dataset_dir = argv[1];
    if (argc >= 3) output_json = argv[2];
    if (argc >= 4) dpi = static_cast<unsigned short>(std::stoi(argv[3]));

    std::cout << "=========================================================\n";
    std::cout << " FingerJetFX OSE + OpenAFIS Full Benchmark\n";
    std::cout << "=========================================================\n";
    std::cout << "Dataset: " << dataset_dir << "\n";
    std::cout << "Output:  " << output_json << "\n";
    std::cout << "DPI:     " << dpi << "\n\n";

    // 1. Gather the 80 images: fingers 101..110, impressions 1..8
    std::vector<ImageMeta> images;
    for (int f = 101; f <= 110; ++f) {
        for (int i = 1; i <= 8; ++i) {
            std::string path = dataset_dir + "/" + std::to_string(f) + "_" + std::to_string(i) + ".tif";
            images.push_back({f, i, path});
        }
    }

    std::cout << "Step 1: Extracting templates for " << images.size() << " images...\n";
    std::map<std::pair<int, int>, FingerprintRecord> records;
    std::map<std::pair<int, int>, OpenAFIS::TemplateISO19794_2_2005<std::string, OpenAFIS::Fingerprint>> parsed_templates;

    int extract_failures = 0;
    int parse_failures = 0;
    double total_extract_ms = 0.0;

    for (const auto& meta : images) {
        auto rec = processImage(meta, dpi);
        total_extract_ms += rec.extract_time_ms;

        if (!rec.extraction_ok) {
            std::cerr << "Extraction failed for " << meta.finger << "_" << meta.impression << ": " << rec.error_msg << "\n";
            extract_failures++;
        } else {
            // Ingest into OpenAFIS
            std::string id_str = std::to_string(meta.finger) + "_" + std::to_string(meta.impression);
            OpenAFIS::TemplateISO19794_2_2005<std::string, OpenAFIS::Fingerprint> t(id_str);
            if (!t.load(rec.iso_template.data(), rec.iso_template.size()) || t.fingerprints().empty()) {
                std::cerr << "OpenAFIS load failed for " << id_str << "\n";
                parse_failures++;
                rec.extraction_ok = false;
                rec.error_msg = "OpenAFIS load failed";
            } else {
                parsed_templates.emplace(std::make_pair(meta.finger, meta.impression), std::move(t));
            }
        }
        records[std::make_pair(meta.finger, meta.impression)] = std::move(rec);
    }

    std::cout << "Extraction complete: " << (images.size() - extract_failures) << "/" << images.size() << " extracted successfully.\n";
    std::cout << "Parse complete: " << parsed_templates.size() << "/" << (images.size() - extract_failures) << " templates parsed by OpenAFIS.\n";
    std::cout << "Average extraction time: " << (total_extract_ms / images.size()) << " ms/image.\n\n";

    // 2. Build standard FVC2002 DB1_B pair list
    // Genuine pairs: all distinct impression pairs for each finger (8 * 7 / 2 = 28 per finger * 10 fingers = 280)
    std::vector<std::pair<std::pair<int, int>, std::pair<int, int>>> genuine_pairs;
    for (int f = 101; f <= 110; ++f) {
        for (int i = 1; i <= 8; ++i) {
            for (int j = i + 1; j <= 8; ++j) {
                genuine_pairs.push_back({{f, i}, {f, j}});
            }
        }
    }

    // Impostor pairs: first impression vs first impression across distinct fingers (10 * 9 / 2 = 45)
    std::vector<std::pair<std::pair<int, int>, std::pair<int, int>>> impostor_pairs;
    for (int f1 = 101; f1 <= 110; ++f1) {
        for (int f2 = f1 + 1; f2 <= 110; ++f2) {
            impostor_pairs.push_back({{f1, 1}, {f2, 1}});
        }
    }

    std::cout << "Step 2: Evaluating " << genuine_pairs.size() << " genuine pairs and " << impostor_pairs.size() << " impostor pairs...\n";

    OpenAFIS::MatchSimilarity matcher;
    std::vector<PairEval> evals;
    evals.reserve(genuine_pairs.size() + impostor_pairs.size());

    int comparison_failures = 0;
    double total_match_us = 0.0;

    auto evaluatePair = [&](std::pair<int, int> p1, std::pair<int, int> p2, bool is_gen) {
        PairEval pe;
        pe.probeFinger = p1.first;
        pe.probeImpression = p1.second;
        pe.candFinger = p2.first;
        pe.candImpression = p2.second;
        pe.isGenuine = is_gen;
        pe.score = 0;
        pe.match_time_us = 0.0;
        pe.comparison_ok = false;

        auto it1 = parsed_templates.find(p1);
        auto it2 = parsed_templates.find(p2);

        if (it1 == parsed_templates.end() || it2 == parsed_templates.end()) {
            pe.error_msg = "Missing template";
            comparison_failures++;
            evals.push_back(pe);
            return;
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        uint8_t raw_score = 0;
        matcher.compute(raw_score, it1->second.fingerprints()[0], it2->second.fingerprints()[0]);
        auto t1 = std::chrono::high_resolution_clock::now();

        pe.score = raw_score;
        pe.match_time_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        pe.comparison_ok = true;
        total_match_us += pe.match_time_us;

        evals.push_back(pe);
    };

    for (const auto& p : genuine_pairs) {
        evaluatePair(p.first, p.second, true);
    }
    for (const auto& p : impostor_pairs) {
        evaluatePair(p.first, p.second, false);
    }

    std::cout << "Matching complete: " << evals.size() << " comparisons performed.\n";
    std::cout << "Comparison failures: " << comparison_failures << "\n";
    std::cout << "Average match time: " << (total_match_us / (evals.size() - comparison_failures)) << " us/pair.\n\n";

    // 3. Output results to JSON
    std::ofstream out(output_json);
    if (!out) {
        std::cerr << "Failed to open output json: " << output_json << "\n";
        return 1;
    }

    out << "{\n";
    out << "  \"pipeline\": \"FingerJetFX_OpenAFIS\",\n";
    out << "  \"dataset\": \"" << dataset_dir << "\",\n";
    out << "  \"impressionsCount\": " << images.size() << ",\n";
    out << "  \"extractFailures\": " << extract_failures << ",\n";
    out << "  \"parseFailures\": " << parse_failures << ",\n";
    out << "  \"comparisonFailures\": " << comparison_failures << ",\n";
    out << "  \"genuinePairsCount\": " << genuine_pairs.size() << ",\n";
    out << "  \"impostorPairsCount\": " << impostor_pairs.size() << ",\n";
    out << "  \"avgExtractMs\": " << (total_extract_ms / images.size()) << ",\n";
    out << "  \"avgMatchUs\": " << (total_match_us / (evals.size() - comparison_failures)) << ",\n";
    out << "  \"pairs\": [\n";

    for (size_t i = 0; i < evals.size(); ++i) {
        const auto& pe = evals[i];
        out << "    {\"probeFinger\": " << pe.probeFinger
            << ", \"probeImpression\": " << pe.probeImpression
            << ", \"candFinger\": " << pe.candFinger
            << ", \"candImpression\": " << pe.candImpression
            << ", \"isGenuine\": " << (pe.isGenuine ? "true" : "false")
            << ", \"score\": " << static_cast<int>(pe.score)
            << ", \"matchTimeUs\": " << pe.match_time_us
            << ", \"comparisonOk\": " << (pe.comparison_ok ? "true" : "false")
            << "}" << (i + 1 < evals.size() ? ",\n" : "\n");
    }

    out << "  ]\n";
    out << "}\n";
    out.close();

    std::cout << "Wrote raw benchmark results to: " << output_json << "\n";
    return 0;
}

