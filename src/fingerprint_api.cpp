#include "kipsma_fingerprint/fingerprint_api.h"
#include "kipsma_fingerprint/enhancer.h"
#include "kipsma_fingerprint/extractor.h"
#include "kipsma_fingerprint/matcher.h"

namespace kipsma::fingerprint {

const char* GetVersion() {
    return "0.3.0-matcher";
}

struct FingerprintEngine::Impl {
    ImageEnhancer     enhancer;
    MinutiaeExtractor extractor;
    MinutiaeMatcher   matcher;
};

FingerprintEngine::FingerprintEngine()
    : impl_(std::make_unique<Impl>()) {}

FingerprintEngine::~FingerprintEngine() = default;

FingerprintEngine::FingerprintEngine(FingerprintEngine&&) noexcept = default;
FingerprintEngine& FingerprintEngine::operator=(FingerprintEngine&&) noexcept = default;

FingerprintTemplate FingerprintEngine::extractTemplate(const cv::Mat& image) {
    FingerprintTemplate t;
    if (image.empty()) return t;

    t.width = static_cast<uint32_t>(image.cols);
    t.height = static_cast<uint32_t>(image.rows);
    t.dpi = 500.0f;
    t.quality = 0.0f;

    const auto enhRes = impl_->enhancer.process(image);
    if (!enhRes.success || !enhRes.isUsable) {
        return t;
    }

    t.quality = enhRes.qualityScore;
    const auto extRes = impl_->extractor.extract(enhRes);
    if (extRes.success) {
        t.minutiae = extRes.minutiae;
    }

    return t;
}

MatchResult FingerprintEngine::match(const FingerprintTemplate& probe, const FingerprintTemplate& candidate) {
    return impl_->matcher.match(probe, candidate);
}

void FingerprintEngine::setMatcherParams(const MatcherParams& params) {
    impl_->matcher.setParams(params);
}

const MatcherParams& FingerprintEngine::getMatcherParams() const {
    return impl_->matcher.params();
}

} // namespace kipsma::fingerprint

#include "kipsma_fingerprint/preprocessor.h"
#include "FJFX.h"
#include "OpenAFIS.h"
#include "TemplateISO19794_2_2005.h"
#include "Match.h"

namespace kipsma::fingerprint {

VerificationResult FingerprintEngine::verifyFiles(const std::string& probePath,
                                                 const std::string& candidatePath,
                                                 unsigned short fallbackDpi,
                                                 float threshold) {
    VerificationResult res;
    res.threshold = threshold;

    PreprocessingConfig cfg;
    cfg.configuredDpi = fallbackDpi;
    cfg.autoPolarity = true;
    cfg.autoContrast = true;
    cfg.cropBlankBorders = true;
    cfg.minStdDev = 5.0f;

    ImagePreprocessor prep(cfg);

    // 1. Process probe image
    const auto prepProbe = prep.processFile(probePath);
    res.probeOperations = prepProbe.appliedOperations;
    if (prepProbe.status != PreprocessingStatus::SUCCESS) {
        if (prepProbe.status == PreprocessingStatus::IMAGE_QUALITY_INSUFFICIENT) {
            res.decision = VerificationDecision::ERROR_EXTRACTION_FAILED;
        } else {
            res.decision = VerificationDecision::ERROR_UNSUPPORTED_INPUT;
        }
        res.isMatch = false;
        res.score = 0.0f;
        res.message = "Probe image validation failed: " + prepProbe.errorMessage;
        return res;
    }

    // 2. Process candidate image
    const auto prepCand = prep.processFile(candidatePath);
    res.candOperations = prepCand.appliedOperations;
    if (prepCand.status != PreprocessingStatus::SUCCESS) {
        if (prepCand.status == PreprocessingStatus::IMAGE_QUALITY_INSUFFICIENT) {
            res.decision = VerificationDecision::ERROR_EXTRACTION_FAILED;
        } else {
            res.decision = VerificationDecision::ERROR_UNSUPPORTED_INPUT;
        }
        res.isMatch = false;
        res.score = 0.0f;
        res.message = "Candidate image validation failed: " + prepCand.errorMessage;
        return res;
    }

    // 3. Extract ISO 19794-2:2005 minutiae from probe via FingerJetFX
    std::vector<uint8_t> probeFmd(FJFX_FMD_BUFFER_SIZE, 0);
    unsigned int probeFmdSize = static_cast<unsigned int>(probeFmd.size());
    int rcProbe = fjfx_create_fmd_from_raw(
        prepProbe.processedImage.data,
        prepProbe.detectedDpi,
        static_cast<unsigned short>(prepProbe.processedImage.rows),
        static_cast<unsigned short>(prepProbe.processedImage.cols),
        FJFX_FMD_ISO_19794_2_2005,
        probeFmd.data(),
        &probeFmdSize
    );
    if (rcProbe != FJFX_SUCCESS) {
        res.decision = VerificationDecision::ERROR_EXTRACTION_FAILED;
        res.isMatch = false;
        res.score = 0.0f;
        res.message = "Probe feature extraction failed (FingerJetFX code " + std::to_string(rcProbe) + ")";
        return res;
    }
    probeFmd.resize(probeFmdSize);

    // 4. Extract ISO 19794-2:2005 minutiae from candidate via FingerJetFX
    std::vector<uint8_t> candFmd(FJFX_FMD_BUFFER_SIZE, 0);
    unsigned int candFmdSize = static_cast<unsigned int>(candFmd.size());
    int rcCand = fjfx_create_fmd_from_raw(
        prepCand.processedImage.data,
        prepCand.detectedDpi,
        static_cast<unsigned short>(prepCand.processedImage.rows),
        static_cast<unsigned short>(prepCand.processedImage.cols),
        FJFX_FMD_ISO_19794_2_2005,
        candFmd.data(),
        &candFmdSize
    );
    if (rcCand != FJFX_SUCCESS) {
        res.decision = VerificationDecision::ERROR_EXTRACTION_FAILED;
        res.isMatch = false;
        res.score = 0.0f;
        res.message = "Candidate feature extraction failed (FingerJetFX code " + std::to_string(rcCand) + ")";
        return res;
    }
    candFmd.resize(candFmdSize);

    // 5. Ingest into OpenAFIS
    OpenAFIS::TemplateISO19794_2_2005<std::string, OpenAFIS::Fingerprint> tProbe("probe");
    if (!tProbe.load(probeFmd.data(), probeFmd.size()) || tProbe.fingerprints().empty()) {
        res.decision = VerificationDecision::ERROR_MATCHING_FAILED;
        res.isMatch = false;
        res.score = 0.0f;
        res.message = "OpenAFIS failed to parse probe template";
        return res;
    }

    OpenAFIS::TemplateISO19794_2_2005<std::string, OpenAFIS::Fingerprint> tCand("cand");
    if (!tCand.load(candFmd.data(), candFmd.size()) || tCand.fingerprints().empty()) {
        res.decision = VerificationDecision::ERROR_MATCHING_FAILED;
        res.isMatch = false;
        res.score = 0.0f;
        res.message = "OpenAFIS failed to parse candidate template";
        return res;
    }

    // 6. Match similarity via OpenAFIS
    OpenAFIS::MatchSimilarity matcher;
    uint8_t rawScore = 0;
    matcher.compute(rawScore, tProbe.fingerprints()[0], tCand.fingerprints()[0]);

    res.score = static_cast<float>(rawScore);
    res.isMatch = (res.score >= threshold);
    res.decision = res.isMatch ? VerificationDecision::MATCH_YES : VerificationDecision::NON_MATCH_NO;
    res.message = res.isMatch ? "Genuine match verified" : "Impostor non-match verified";

    return res;
}

} // namespace kipsma::fingerprint


