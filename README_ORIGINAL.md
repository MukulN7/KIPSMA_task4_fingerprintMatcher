# KIPSMA Fingerprint Matcher

An industrial-grade, self-contained native C++17 biometric fingerprint verification library, command-line interface (CLI), and evaluation harness for Windows (MSVC x64).

The system integrates **DigitalPersona FingerJetFX OSE** (minutiae feature extraction conforming to ISO/IEC 19794-2:2005) with **OpenAFIS** (minutiae triplet matching) and an OpenCV-based preprocessing pipeline. It provides deterministic, robust 1:1 biometric verification returning unambiguous `YES` / `NO` decisions or explicit error diagnoses.

---

## 1. Key Features & Architecture

- **Automatic Input Preprocessing:**
  - Ingress loading across TIFF, BMP, PNG, and JPEG formats.
  - Automatic conversion of 1-, 3-, and 4-channel images (including RGBA/BGRA) to 8-bit contiguous grayscale buffers.
  - Safe blank border removal without clipping active friction ridges.
  - Contrast and dynamic range validation; detection of inverted ridge polarity.
  - Strict physical dimension and DPI validation to prevent algorithmic distortion.
- **Standards-Compliant Feature Extraction:**
  - Extracts standard ISO/IEC 19794-2:2005 minutiae records via FingerJetFX OSE.
  - Robust handling of varied sensor geometries (optical, capacitive, thermal sweep).
- **Fast Triplet Minutiae Matching:**
  - Employs OpenAFIS Delaunay triangulation and local topology similarity scoring.
  - Ultra-low latency: ~19 ms extraction, ~60 µs comparison time.
- **Benchmark Proven:**
  - Mean ROC-AUC of **0.9413** across all 8 standard competition databases of FVC2002 and FVC2004.
  - Equal Error Rates (EER) of **2.32% – 8.95%** on optical, thermal sweep, and synthetic sensors.
  - Complete validation report available in [`benchmark_results/FINAL_VALIDATION_REPORT.md`](benchmark_results/FINAL_VALIDATION_REPORT.md).

---

## 2. Prerequisites & Toolchain

- **Operating System:** Windows 10 / 11 (64-bit)
- **Compiler:** Microsoft Visual C++ (MSVC) from Visual Studio 2019, 2022, or 2026 (C++17 standard support required)
- **Build System:** CMake 3.16 or newer
- **OpenCV:** OpenCV 3.4.11 (or 4.x) pre-built for Windows x64 (MSVC)
- **External Dependencies:**
  - `FingerJetFX OSE`: LGPLv3 fingerprint feature extractor (`FJFX.dll` / `FJFX.lib`)
  - `OpenAFIS`: BSD-2-Clause C++ minutiae matcher (`openafis.lib`)

---

## 3. Directory Layout

```text
kipsma_fingerprint_matcher/
├── CMakeLists.txt              # Primary CMake configuration
├── README.md                   # This documentation
├── LICENSE                     # Project license (Apache 2.0)
├── THIRD_PARTY_NOTICES.md      # Attribution and licenses for third-party libraries
├── ARCHITECTURE.md             # Technical architecture and design document
├── DATASET_INVENTORY.md        # Reference dataset specifications (FVC, SOCOFing)
├── .gitignore                  # Git ignore rules
├── include/kipsma_fingerprint/ # Public C++ header files
│   ├── fingerprint_api.h       # High-level FingerprintEngine API
│   ├── preprocessor.h          # Image preprocessing & quality gating
│   ├── types.h                 # VerificationDecision, MatchResult, ImageBuffer
│   ├── export.h                # DLL export/import definitions
│   ├── enhancer.h              # Gabor filtering and orientation fields
│   ├── extractor.h             # Minutiae extraction definitions
│   ├── matcher.h               # Minutiae matching definitions
│   └── triplet.h               # Delaunay triplet definitions
├── src/                        # Library implementation source files
│   ├── fingerprint_api.cpp
│   ├── preprocessor.cpp
│   ├── enhancer.cpp
│   ├── extractor.cpp
│   ├── matcher.cpp
│   └── triplet.cpp
├── cli/                        # Standalone CLI source
│   └── main.cpp
├── tests/                      # Automated CTest test suite
│   ├── smoke_test.cpp
│   ├── enhancer_test.cpp
│   ├── extractor_test.cpp
│   ├── matcher_test.cpp
│   └── triplet_test.cpp
└── benchmark_results/          # Validation reports & benchmark evaluation scripts
    └── FINAL_VALIDATION_REPORT.md
```

---

## 4. Building the Project

### Step 1: Prepare External Dependencies

#### A. OpenCV 3.4.11 x64
Install or extract OpenCV to a directory of your choice, for example `C:\tools\opencv3411`. Ensure the following subdirectories exist:
- `include/` (containing `opencv2/opencv.hpp`)
- `lib/` (containing `opencv_core3411.lib`, `opencv_imgproc3411.lib`, `opencv_imgcodecs3411.lib`)
- `bin/` (containing runtime DLLs)

#### B. FingerJetFX OSE
1. Clone or obtain FingerJetFX OSE:
   ```cmd
   git clone https://github.com/DigitalPersona/FingerJetFXOSE.git
   ```
2. Build the x64 Release configuration using CMake / MSVC. The build produces:
   - Header: `FingerJetFXOSE/libFJFX/include/FRFXLL.h`
   - Import Library: `FingerJetFXOSE/dist/win64/Release/FJFX.lib`
   - Runtime DLL: `FingerJetFXOSE/dist/win64/Release/FJFX.dll`

#### C. OpenAFIS
1. Clone or obtain OpenAFIS:
   ```cmd
   git clone https://github.com/neilharan/openafis.git
   ```
2. Build the x64 Release configuration with CMake / MSVC. The build produces:
   - Headers: `openafis/lib/`
   - Library: `openafis/build/lib/Release/openafis.lib`

---

### Step 2: Configure with CMake

You can configure `kipsma_fingerprint_matcher` by pointing CMake to your dependency locations using cache flags (`-D...`) or environment variables:

```powershell
# Using CMake flags (recommended):
cmake -B build -S . `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DOpenCV_DIR="C:/tools/opencv3411" `
  -DFINGERJET_ROOT="D:/Career/KIPSMA/FingerprintMatcher-References/FingerJetFXOSE/FingerJetFXOSE" `
  -DOPENAFIS_ROOT="D:/Career/KIPSMA/FingerprintMatcher-References/openafis" `
  -DDATASET_DIR="D:/Career/KIPSMA/FingerprintMatcher-Data"
```

Or configure using environment variables:
```powershell
$env:OpenCV_DIR = "C:\tools\opencv3411"
$env:FINGERJET_ROOT = "D:\Career\KIPSMA\FingerprintMatcher-References\FingerJetFXOSE\FingerJetFXOSE"
$env:OPENAFIS_ROOT = "D:\Career\KIPSMA\FingerprintMatcher-References\openafis"

cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
```

---

### Step 3: Compile

Compile the project in Release mode using CMake:

```powershell
cmake --build build --config Release
```

Upon compilation:
- The shared library `kipsma_fingerprint.dll` and CLI executable `fingerprint_cli.exe` are placed into `build\bin\Release\`.
- **Automatic Runtime Deployment:** CMake automatically copies `FJFX.dll` and the required OpenCV DLLs into `build\bin\Release\`. No manual PATH modification or manual copying of DLLs is required.

---

### Step 4: Run Tests

Execute the automated test suite using CTest:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

---

## 5. Command-Line Interface (CLI) Usage

The standalone executable `fingerprint_cli.exe` is located in `build\bin\Release\`.

### Synopsis
```text
fingerprint_cli.exe verify <probe_image> <candidate_image> [options]
fingerprint_cli.exe match  <probe_image> <candidate_image> [options]
fingerprint_cli.exe --help
```

### Options
- `--threshold <float>` : Decision cutoff threshold (default: `2.0`). Scores $\ge T$ result in `YES`, $< T$ result in `NO`.
- `--dpi <int>` : Fallback acquisition DPI if container metadata does not specify resolution (default: `500`).
- `--help` : Display help and usage information.

### Return Codes
- `0` : Verification completed successfully (either `YES` or `NO`).
- `1` : Command line argument parsing error or usage error.
- `2` : Verification error (file not found, corrupted image, unsupported dimensions, or feature extraction failure).

---

### Concrete CLI Examples

#### 1. Genuine Fingerprint Pair (Same finger)
```powershell
.\build\bin\Release\fingerprint_cli.exe verify "path\to\101_1.tif" "path\to\101_2.tif"
```
**Output:**
```text
=== 1:1 Biometric Fingerprint Verification ===
  Probe Image     : path\to\101_1.tif
  Candidate Image : path\to\101_2.tif
  Fallback DPI    : 500
  Decision Cutoff : 2
  Probe Preprocessing:
    - Safely cropped uninformative blank border: 250x368 px
  Candidate Preprocessing:
    - Safely cropped uninformative blank border: 243x284 px
  Status Message  : Genuine match verified
  Match Score     : 39 / 100
==============================================
VERIFICATION DECISION: YES
==============================================
```

#### 2. Impostor Fingerprint Pair (Different fingers)
```powershell
.\build\bin\Release\fingerprint_cli.exe verify "path\to\101_1.tif" "path\to\102_1.tif"
```
**Output:**
```text
=== 1:1 Biometric Fingerprint Verification ===
  Probe Image     : path\to\101_1.tif
  Candidate Image : path\to\102_1.tif
  Fallback DPI    : 500
  Decision Cutoff : 2
  Probe Preprocessing:
    - Safely cropped uninformative blank border: 250x368 px
  Candidate Preprocessing:
    - Safely cropped uninformative blank border: 270x374 px
  Status Message  : Impostor non-match verified
  Match Score     : 0 / 100
==============================================
VERIFICATION DECISION: NO
==============================================
```

#### 3. Undersized / Incompatible Image
```powershell
.\build\bin\Release\fingerprint_cli.exe verify "path\to\small_image.bmp" "path\to\101_1.tif"
```
**Output (Exit code 2):**
```text
=== 1:1 Biometric Fingerprint Verification ===
  Probe Image     : path\to\small_image.bmp
  Candidate Image : path\to\101_1.tif
  Fallback DPI    : 500
  Decision Cutoff : 2
  Status Message  : Probe image validation failed: Input unsupported: Physical width 0.192000 in outside required range [0.30, 1.62] in
  Match Result    : ERROR (Verification could not be performed)
==============================================
VERIFICATION DECISION: ERROR (Probe image validation failed: Input unsupported: Physical width 0.192000 in outside required range [0.30, 1.62] in)
==============================================
```

---

## 6. C++ API Integration

Include the main library header and link against `kipsma_fingerprint.lib`:

```cpp
#include <iostream>
#include "kipsma_fingerprint/fingerprint_api.h"

int main() {
    kipsma::fingerprint::FingerprintEngine engine;

    // Verify two image files with 500 DPI fallback and decision cutoff 2.0
    kipsma::fingerprint::VerificationResult result = engine.verifyFiles(
        "probe.tif",
        "candidate.tif",
        500,     /* fallback DPI */
        2.0f     /* threshold */
    );

    if (result.decision == kipsma::fingerprint::VerificationDecision::MATCH_YES) {
        std::cout << "Decision: YES (Score: " << result.score << ")\n";
    } else if (result.decision == kipsma::fingerprint::VerificationDecision::NON_MATCH_NO) {
        std::cout << "Decision: NO (Score: " << result.score << ")\n";
    } else {
        std::cerr << "Verification failed: " << result.message << "\n";
        return 1;
    }

    return 0;
}
```

---

## 7. Operational Specifications & Known Limitations

1. **Resolution Bounds:** Supported sensor DPI ranges from **300 to 1024 DPI** (standard 500 DPI recommended). Inputs outside this range are rejected with `ERROR_UNSUPPORTED_INPUT`.
2. **Physical Area Bounds:** FingerJetFX OSE requires a minimum physical active sensor area of **$0.30 \times 0.30$ inches** ($150 \times 150$ pixels at 500 DPI). Ultra-small mobile sensor impressions (such as SOCOFing $96 \times 103$ px at ~200 DPI) cannot provide sufficient ridge-frequency cycles and are safely rejected with explicit dimension errors.
3. **Contiguous Buffers:** Image buffers passed to the feature extractor are automatically validated and normalized to 8-bit contiguous grayscale buffers with continuous row stride.
4. **Third-Party Attribution:** Applications incorporating this software must comply with the licensing and notice terms detailed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

---

## 8. License

The `kipsma_fingerprint_matcher` framework and wrapper code are licensed under the **Apache License, Version 2.0**. See [`LICENSE`](LICENSE) for terms.

Third-party dependencies (FingerJetFX OSE, OpenAFIS, Delaunator-cpp, OpenCV) are subject to their respective licenses. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for full notices and disclaimers.

