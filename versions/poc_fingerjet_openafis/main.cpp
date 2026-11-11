#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <iomanip>

// OpenCV
#include <opencv2/opencv.hpp>

// FingerJetFX
#include "FJFX.h"

// OpenAFIS
#include "OpenAFIS.h"
#include "TemplateISO19794_2_2005.h"
#include "Match.h"

// Preprocessor
#include "kipsma_fingerprint/preprocessor.h"

struct ExtractionResult {
    bool success = false;
    kipsma::fingerprint::PreprocessingStatus status = kipsma::fingerprint::PreprocessingStatus::SUCCESS;
    std::string error_message;
    std::vector<std::string> log_ops;
    std::vector<uint8_t> template_data;
    int minutiae_count = 0;
    unsigned short used_dpi = 0;
};

ExtractionResult extractTemplateValidated(const std::string& image_path, unsigned short fallbackDpi = 500) {
    ExtractionResult result;

    kipsma::fingerprint::PreprocessingConfig cfg;
    cfg.configuredDpi = fallbackDpi;
    cfg.autoPolarity = true;
    cfg.autoContrast = true;
    cfg.cropBlankBorders = true;
    cfg.minStdDev = 5.0f;

    kipsma::fingerprint::ImagePreprocessor preprocessor(cfg);
    auto prepRes = preprocessor.processFile(image_path);
    result.log_ops = prepRes.appliedOperations;
    result.status = prepRes.status;
    result.used_dpi = prepRes.detectedDpi;

    if (prepRes.status != kipsma::fingerprint::PreprocessingStatus::SUCCESS) {
        result.success = false;
        result.error_message = prepRes.errorMessage;
        return result;
    }

    const cv::Mat& img = prepRes.processedImage;
    const unsigned short height = static_cast<unsigned short>(img.rows);
    const unsigned short width = static_cast<unsigned short>(img.cols);

    std::vector<uint8_t> fmd_buffer(FJFX_FMD_BUFFER_SIZE, 0);
    unsigned int size_of_fmd = static_cast<unsigned int>(fmd_buffer.size());

    int rc = fjfx_create_fmd_from_raw(
        img.data,
        prepRes.detectedDpi,
        height,
        width,
        FJFX_FMD_ISO_19794_2_2005,
        fmd_buffer.data(),
        &size_of_fmd
    );

    if (rc != FJFX_SUCCESS) {
        result.status = kipsma::fingerprint::PreprocessingStatus::EXTRACTION_FAILED;
        std::string err_desc;
        switch (rc) {
            case FJFX_FAIL_IMAGE_SIZE_NOT_SUP: err_desc = "IMAGE_SIZE_NOT_SUP"; break;
            case FJFX_FAIL_EXTRACTION_UNSPEC: err_desc = "EXTRACTION_UNSPEC"; break;
            case FJFX_FAIL_EXTRACTION_BAD_IMP: err_desc = "EXTRACTION_BAD_IMP (blank/faint ridge contact)"; break;
            case FJFX_FAIL_INVALID_OUTPUT_FORMAT: err_desc = "INVALID_OUTPUT_FORMAT"; break;
            case FJFX_FAIL_OUTPUT_BUFFER_IS_TOO_SMALL: err_desc = "OUTPUT_BUFFER_IS_TOO_SMALL"; break;
            default: err_desc = "CODE_" + std::to_string(rc); break;
        }
        result.error_message = "FingerJetFX extraction rejected impression: " + err_desc;
        return result;
    }

    fmd_buffer.resize(size_of_fmd);
    result.template_data = std::move(fmd_buffer);
    result.success = true;

    if (result.template_data.size() >= 28) {
        result.minutiae_count = static_cast<int>(result.template_data[27]);
    }

    return result;
}

int main(int argc, char** argv) {
    std::cout << "=====================================================================\n";
    std::cout << " KIPSMA Biometric Verification: Preprocessing + FingerJetFX + OpenAFIS\n";
    std::cout << "=====================================================================\n\n";

    std::string path_probe = "D:/Career/KIPSMA/FingerprintMatcher-Data/FVC2002/DB1_B/101_1.tif";
    std::string path_candidate = "D:/Career/KIPSMA/FingerprintMatcher-Data/FVC2002/DB1_B/101_2.tif";
    unsigned short fallbackDpi = 500;
    uint8_t threshold = 2; // Calibrated standard decision threshold

    if (argc >= 3) {
        path_probe = argv[1];
        path_candidate = argv[2];
    }
    if (argc >= 4) {
        fallbackDpi = static_cast<unsigned short>(std::stoi(argv[3]));
    }
    if (argc >= 5) {
        threshold = static_cast<uint8_t>(std::stoi(argv[4]));
    }

    std::cout << "Probe Image:     " << path_probe << "\n";
    std::cout << "Candidate Image: " << path_candidate << "\n";
    std::cout << "Fallback DPI:    " << fallbackDpi << "\n";
    std::cout << "Decision Thresh: " << static_cast<int>(threshold) << "\n\n";

    // 1. Process Probe
    std::cout << "[1] Validating & Preprocessing Probe Image...\n";
    auto res_probe = extractTemplateValidated(path_probe, fallbackDpi);
    std::cout << "  Operations applied (" << res_probe.log_ops.size() << "):\n";
    for (const auto& op : res_probe.log_ops) {
        std::cout << "    - " << op << "\n";
    }
    if (!res_probe.success) {
        std::cout << "\n---------------------------------------------------------------------\n";
        std::cout << " RESULT: ERROR (Probe Validation/Extraction Failed)\n";
        std::cout << " Status:  " << res_probe.error_message << "\n";
        std::cout << " Decision: INPUT_UNSUPPORTED / EXTRACTION_FAILED (NOT a 'NO' non-match)\n";
        std::cout << "---------------------------------------------------------------------\n";
        return 1;
    }
    std::cout << "  Probe Minutiae Extracted: " << res_probe.minutiae_count << " (Template: " << res_probe.template_data.size() << " bytes)\n\n";

    // 2. Process Candidate
    std::cout << "[2] Validating & Preprocessing Candidate Image...\n";
    auto res_cand = extractTemplateValidated(path_candidate, fallbackDpi);
    std::cout << "  Operations applied (" << res_cand.log_ops.size() << "):\n";
    for (const auto& op : res_cand.log_ops) {
        std::cout << "    - " << op << "\n";
    }
    if (!res_cand.success) {
        std::cout << "\n---------------------------------------------------------------------\n";
        std::cout << " RESULT: ERROR (Candidate Validation/Extraction Failed)\n";
        std::cout << " Status:  " << res_cand.error_message << "\n";
        std::cout << " Decision: INPUT_UNSUPPORTED / EXTRACTION_FAILED (NOT a 'NO' non-match)\n";
        std::cout << "---------------------------------------------------------------------\n";
        return 1;
    }
    std::cout << "  Candidate Minutiae Extracted: " << res_cand.minutiae_count << " (Template: " << res_cand.template_data.size() << " bytes)\n\n";

    // 3. OpenAFIS Template Parsing
    std::cout << "[3] Parsing Templates with OpenAFIS...\n";
    OpenAFIS::TemplateISO19794_2_2005<std::string, OpenAFIS::Fingerprint> t_probe("probe");
    if (!t_probe.load(res_probe.template_data.data(), res_probe.template_data.size()) || t_probe.fingerprints().empty()) {
        std::cerr << "OpenAFIS failed to ingest probe template!\n";
        return 2;
    }
    OpenAFIS::TemplateISO19794_2_2005<std::string, OpenAFIS::Fingerprint> t_cand("cand");
    if (!t_cand.load(res_cand.template_data.data(), res_cand.template_data.size()) || t_cand.fingerprints().empty()) {
        std::cerr << "OpenAFIS failed to ingest candidate template!\n";
        return 2;
    }

    // 4. Match
    std::cout << "[4] Executing Geometric Consensus Match...\n";
    OpenAFIS::MatchSimilarity matcher;
    uint8_t score = 0;
    matcher.compute(score, t_probe.fingerprints()[0], t_cand.fingerprints()[0]);

    const bool isMatch = (score >= threshold);

    std::cout << "\n=====================================================================\n";
    std::cout << " 1:1 BIOMETRIC VERIFICATION RESULT:\n";
    std::cout << "  Match Score      : " << static_cast<int>(score) << " / 100\n";
    std::cout << "  Decision Cutoff  : " << static_cast<int>(threshold) << "\n";
    std::cout << "  Verified Decision: " << (isMatch ? "YES (MATCH / GENUINE)" : "NO (NON-MATCH / IMPOSTOR)") << "\n";
    std::cout << "=====================================================================\n";

    return 0;
}
