# KIPSMA Fingerprint Matcher

An industrial-grade, self-contained native C++17 biometric fingerprint verification library, command-line interface (CLI), and evaluation harness for Windows (MSVC x64).

Developed for **Kips Microanalysis**.

---

## 1. Overview
Automated fingerprint verification is essential for identity confirmation and secure access control. High-reliability biometric matching requires overcoming severe optical distortions, skin dryness/moisture variations, non-linear elastic deformations, and varying sensor physical resolutions.

**KIPSMA Fingerprint Matcher** combines standards-compliant minutiae extraction (ISO/IEC 19794-2:2005) with Delaunay triangulation and local topological similarity scoring. It provides deterministic, robust 1:1 verification returning unambiguous `YES` / `NO` match decisions and granular confidence telemetry.

---

## 2. Problem Statement
Practical biometric deployment encounters diverse friction-ridge imaging challenges:
- High variability in sensor technology (optical prism, capacitive array, thermal sweep, synthetic generation).
- Partial fingerprints with fewer than 15 valid minutiae points.
- Rotational and translational offsets between impressions.
- Low dynamic range, inverted ridge polarities, and noisy background borders.
- Stringent latency requirements (< 30 ms per 1:1 comparison).

---

## 3. Objectives
- **Automated Ingress Preprocessing:** Ingest BMP, PNG, JPEG, and TIFF images; normalize multi-channel/alpha buffers to contiguous 8-bit grayscale; detect inverted polarity; safely crop blank non-ridge borders.
- **Standards-Compliant Extraction:** Extract minutiae locations $(x, y)$, orientations $\theta$, and types (bifurcation vs. ending) conforming to ISO/IEC 19794-2:2005.
- **Delaunay Triplet Matching:** Form invariant spatial triangles connecting neighboring minutiae to achieve rotation- and translation-invariant similarity scoring.
- **Extensively Benchmarked Accuracy:** Validated across all 8 standard databases of FVC2002 and FVC2004 with a mean ROC-AUC of **0.9413** and Equal Error Rates (EER) of **2.32% – 8.95%**.

---

## 4. System Architecture

```text
[Fingerprint Image A]                    [Fingerprint Image B]
          │                                        │
          ▼                                        ▼
[Image Preprocessor]                     [Image Preprocessor]
├─ Format Ingress (TIFF/BMP/PNG/JPEG)    ├─ Format Ingress (TIFF/BMP/PNG/JPEG)
├─ 8-bit Grayscale Normalization         ├─ 8-bit Grayscale Normalization
├─ Dynamic Range & Polarity Check        ├─ Dynamic Range & Polarity Check
└─ Safe Border Auto-Cropping             └─ Safe Border Auto-Cropping
          │                                        │
          ▼                                        ▼
[Gabor / Ridge Enhancer]                 [Gabor / Ridge Enhancer]
├─ Directional Field Estimation          ├─ Directional Field Estimation
├─ Ridge Frequency Analysis              ├─ Ridge Frequency Analysis
└─ Binarization & Thinning (Skeleton)    └─ Binarization & Thinning (Skeleton)
          │                                        │
          ▼                                        ▼
[Minutiae Extractor]                     [Minutiae Extractor]
(ISO/IEC 19794-2:2005 Features)          (ISO/IEC 19794-2:2005 Features)
          │                                        │
          └───────────────────┬────────────────────┘
                              ▼
                 [Delaunay Triplet Matcher]
                 ├─ Triangle Edge & Angle Similarity
                 ├─ Global Geometric Alignment (RANSAC)
                 └─ Root Mean Square Minutiae Distance
                              │
                              ▼
                 [Decision & Scoring Engine]
                 ├─ Similarity Score (0.0 - 1.0)
                 ├─ Deterministic Threshold ($\ge 0.35$)
                 └─ Output: YES / NO + Diagnostics
```

---

## 5. Technical Approach & Mathematical Model

1. **Minutiae Representation:**
   Each extracted minutia point $m_i$ is defined by:
   $$m_i = (x_i, y_i, \theta_i, \tau_i)$$
   where $(x_i, y_i)$ are Cartesian spatial coordinates, $\theta_i \in [0, 2\pi)$ is ridge tangent orientation, and $\tau_i \in \{\text{Ending}, \text{Bifurcation}\}$ is topological type.
2. **Local Minutiae Triplets (Delaunay Triangulation):**
   Three neighboring minutiae $(m_a, m_b, m_c)$ form a triangle characterized by edge lengths $(L_{ab}, L_{bc}, L_{ca})$ and internal angles $(\alpha, \beta, \gamma)$.
   Because Euclidean edge lengths and internal angles are strictly invariant to 2D rigid rotation and translation, matching triplets across impressions enables rapid correspondence establishment without combinatorial search.
3. **Similarity Metric:**
   For a candidate match between triplet $T_1$ and triplet $T_2$:
   $$\Delta L = \max(|L_{1,ab} - L_{2,ab}|, |L_{1,bc} - L_{2,bc}|, |L_{1,ca} - L_{2,ca}|)$$
   $$\Delta \alpha = \max(|\alpha_1 - \alpha_2|, |\beta_1 - \beta_2|, |\gamma_1 - \gamma_2|)$$
   Triplets are matched if $\Delta L \le 8\text{ px}$ and $\Delta \alpha \le 15^\circ$.
4. **Global Alignment & Scoring:**
   Candidate pairs establish rigid transformation $[\mathbf{R} \mid \mathbf{t}]$. Minutiae pairs falling within spatial radius $r_0 \le 12\text{ px}$ and angular tolerance $\Delta \theta \le 20^\circ$ contribute to the total matching score $S$:
   $$S = \frac{2 \cdot N_{\text{matched}}}{N_1 + N_2}$$

---

## 6. Technologies and Dependencies
- **Language:** Native C++17
- **Computer Vision:** OpenCV 3.4.11 (`core`, `imgproc`, `imgcodecs`)
- **Build System:** CMake 3.16+ with CTest
- **Platform:** Windows 10 / Windows 11 (MSVC x64)
- **External Components:**
  - `FingerJetFX OSE`: DigitalPersona ISO/IEC 19794-2 feature extraction engine
  - `OpenAFIS`: BSD-2-Clause Delaunay triangulation matcher

---

## 7. Repository Structure

```text
KIPSMA_task4_fingerprintMatcher/
├── README.md                      # Comprehensive project documentation
├── .gitignore                     # Git ignore rules for MSVC, datasets
├── CMakeLists.txt                 # Primary build configuration (Library, CLI, Tests)
├── LICENSE                        # Apache 2.0 License
├── THIRD_PARTY_NOTICES.md         # Open-source attributions and licenses
├── ARCHITECTURE.md                # In-depth system design & module contracts
├── DATASET_INVENTORY.md           # Biometric dataset inventory and sensor details
├── binarized.png                  # Sample ridge enhancement diagnostic
├── skeleton.png                   # Sample morphological thinning diagnostic
├── minutiae_overlay.png           # Sample extracted minutiae visualization
├── include/kipsma_fingerprint/    # Public C++ API headers
│   ├── fingerprint_api.h          # Main C++ verification API
│   ├── export.h                   # Dynamic link library macros
│   ├── types.h                    # ImageBuffer, Minutia, MatchResult
│   ├── preprocessor.h             # Input normalization and border cropping
│   ├── enhancer.h                 # Gabor filtering and skeletonization
│   ├── extractor.h                # Minutiae feature extraction
│   ├── matcher.h                  # Geometric alignment and scoring
│   └── triplet.h                  # Delaunay triangle topology
├── src/                           # Core implementation files
│   ├── fingerprint_api.cpp
│   ├── preprocessor.cpp
│   ├── enhancer.cpp
│   ├── extractor.cpp
│   ├── matcher.cpp
│   └── triplet.cpp
├── cli/                           # Command-line interface
│   └── main.cpp
├── tests/                         # Automated unit & integration test suite
│   ├── smoke_test.cpp
│   ├── enhancer_test.cpp
│   ├── extractor_test.cpp
│   ├── matcher_test.cpp
│   └── triplet_test.cpp
├── benchmark_results/             # Evaluation reports and verification metrics
│   ├── FINAL_VALIDATION_REPORT.md
│   ├── FINAL_FINGERJET_OPENAFIS_REPORT.md
│   └── calculate_detailed_metrics.py
└── versions/
    └── poc_fingerjet_openafis/    # Exploratory POC benchmarking external engines
        ├── CMakeLists.txt
        ├── main.cpp
        ├── benchmark_runner.cpp
        └── README.md
```

---

## 8. Build Instructions

```powershell
# Open terminal in project root
# Configure CMake with Visual Studio MSVC x64
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" `
    -S . -B build -G "Visual Studio 18 2026" -A x64

# Build library, CLI, and all test executables
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" `
    --build build --config Release
```

---

## 9. Testing Instructions

Run CTest from the build directory:
```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" `
    --test-dir build -C Release --output-on-failure
```

### Verified Test Targets (5/5 Passing)
1. **`smoke_test`**: Ingress loading, basic extraction, and matching API lifecycle.
2. **`enhancer_test`**: Dynamic range normalization, Gabor filtering, and binarization.
3. **`extractor_test`**: Minutiae coordinate localization, orientation angle, and boundary filtering.
4. **`matcher_test`**: Decision threshold logic, genuine vs. impostor score separation.
5. **`triplet_test`**: Delaunay triangle generation and rotation-invariant edge matching.

---

## 10. CLI Execution

```powershell
# Compare two fingerprint impressions:
.\build\bin\Release\fingerprint_cli.exe "<probe_image.tif>" "<candidate_image.tif>"

# Example Output:
# [RESULT] Match: YES (Score: 0.742, Minutiae Matched: 28, Threshold: 0.35)
```

---

## 11. Maintainer Information
- **Author:** Mukul Napa ([GitHub: MukulN7](https://github.com/MukulN7))
- **Organization:** Kips Microanalysis
