# Fingerprint Dataset Inventory & Evaluation Protocol Specification

## 1. Executive Summary

This document specifies the dataset inventory, identity mapping rules, evaluation protocol designs, licensing constraints, and tooling compatibility for all benchmark datasets available under `D:\Career\KIPSMA\FingerprintMatcher-Data`.

The datasets under evaluation are:
1. **FVC2002** (Databases DB1_B, DB2_B, DB3_B, DB4_B)
2. **FVC2004** (Databases DB1_B, DB2_B, DB3_B, DB4_B)
3. **SOCOFing** (Sokoto Coventry Fingerprint Dataset: Real & Altered subsets)

---

## 2. Dataset Inventory

### 2.1 File Counts and Sensor Specifications

| Dataset | Subset / Database | Sensor / Technology | Resolution (DPI) | Dimensions (W x H) | Bit Depth / Format | Image Count | Fingers | Impressions / Finger |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **FVC2002** | `DB1_B` | Optical (Identix TouchView II) | 500 dpi | 388 × 374 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **FVC2002** | `DB2_B` | Optical (Biometrika FX2000) | 569 dpi | 296 × 560 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **FVC2002** | `DB3_B` | Capacitive (Precise 100 SC) | 500 dpi | 300 × 300 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **FVC2002** | `DB4_B` | Synthetic (SFinGe v2.51) | ~500 dpi | 288 × 384 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **FVC2004** | `DB1_B` | Optical (CrossMatch V300) | 500 dpi | 640 × 480 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **FVC2004** | `DB2_B` | Optical (DigitalPersona U.are.U 4000) | 512 dpi | 328 × 364 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **FVC2004** | `DB3_B` | Thermal Sweep (Atmel FingerChip) | 512 dpi | 300 × 480 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **FVC2004** | `DB4_B` | Synthetic (SFinGe v3.0) | ~500 dpi | 288 × 384 | 8-bit Grayscale TIFF | 80 | 10 | 8 |
| **SOCOFing** | `Real/` | DigitalPersona 4500 (Clean) | ~200 dpi (est.) | 96 × 103 | 32-bit BGRA DIB BMP | 6,000 | 6,000 | 1 |
| **SOCOFing** | `Altered-Easy/` | Synthetically Altered | ~200 dpi (est.) | 96 × 103 | 32-bit BGRA DIB BMP | 17,931 | ~6,000 | 1–3 (Obl, CR, Zcut) |
| **SOCOFing** | `Altered-Medium/` | Synthetically Altered | ~200 dpi (est.) | 96 × 103 | 32-bit BGRA DIB BMP | 17,067 | ~6,000 | 1–3 (Obl, CR, Zcut) |
| **SOCOFing** | `Altered-Hard/` | Synthetically Altered | ~200 dpi (est.) | 96 × 103 | 32-bit BGRA DIB BMP | 14,272 | ~6,000 | 1–3 (Obl, CR, Zcut) |

**Totals**:
- **FVC2002 Total**: 320 images across 4 databases.
- **FVC2004 Total**: 320 images across 4 databases.
- **SOCOFing Total**: 55,270 unique images (6,000 Real + 49,270 Altered).
  - *Note on SOCOFing filesystem structure*: The root `D:\Career\KIPSMA\FingerprintMatcher-Data\SOCOFing\archive\SOCOFing` contains an identical recursive nested subdirectory `SOCOFing\`. All file hashes and structure match 100%. Evaluation scripts must ignore the nested duplicate directory to avoid redundant or distorted evaluations.

---

## 3. Finger Identity Establishment Rules

### 3.1 FVC2002 and FVC2004
- **Filename pattern**: `(\d+)_(\d+)\.tif`
  - Group 1: Finger ID (`101` to `110`, representing 10 distinct fingers).
  - Group 2: Impression ID (`1` to `8`, representing 8 separate acquisitions).
- **Identity rule**:
  - **Same Finger (Genuine)**: `FingerID(A) == FingerID(B)`.
  - **Different Finger (Impostor)**: `FingerID(A) != FingerID(B)`.

### 3.2 SOCOFing
- **Filename pattern (Real)**: `^(\d+)__([MF])_(Left|Right)_(thumb|index|middle|ring|little)_finger\.BMP$`
  - `SubjectID`: Integer `1` to `600` (600 distinct subjects).
  - `Gender`: `M` (Male) or `F` (Female).
  - `Hand`: `Left` or `Right`.
  - `Finger`: `thumb`, `index`, `middle`, `ring`, or `little`.
- **Filename pattern (Altered)**: `^(\d+)__([MF])_(Left|Right)_(thumb|index|middle|ring|little)_finger_(CR|Obl|Zcut)\.BMP$`
  - Corruption types:
    - `CR`: Central Rotation.
    - `Obl`: Obliteration (scratching / erasing ridge patterns).
    - `Zcut`: Z-cut distortion / surgical scarring.
- **Identity rule**:
  - A unique biological finger is identified by the tuple: `(SubjectID, Hand, Finger)`.
  - There are $600 \times 2 \times 5 = 6,000$ unique fingers in `Real/`.
  - `Gender` is an attribute of `SubjectID` and is fully correlated.
  - **Same Finger (Genuine)**: `(SubjectA, HandA, FingerA) == (SubjectB, HandB, FingerB)`.
  - **Different Finger (Impostor)**: `(SubjectA, HandA, FingerA) != (SubjectB, HandB, FingerB)`.

---

## 4. Benchmark Protocols and Expected Pair Counts

### 4.1 FVC2002 Protocols (Evaluated per DB: DB1_B, DB2_B, DB3_B, DB4_B)
Each DB has 10 fingers $\times$ 8 impressions = 80 images.

1. **Genuine Comparisons**:
   - Each impression compared against every other impression of the same finger:
     $$\binom{8}{2} \times 10 = 28 \times 10 = 280\text{ genuine pairs per DB}$$
   - Total across all 4 databases: $4 \times 280 = 1,120\text{ pairs}$.

2. **Standard FVC Impostor Protocol (Official FVC Protocol)**:
   - First impression of each finger compared against the first impression of all other fingers:
     $$\binom{10}{2} = 45\text{ impostor pairs per DB}$$
   - Total across all 4 databases: $4 \times 45 = 180\text{ pairs}$.

3. **Full Cross-Finger Impostor Protocol**:
   - All pairwise combinations across different fingers (excluding symmetric and self pairs):
     $$\frac{80 \times (80 - 8)}{2} = \frac{80 \times 72}{2} = 2,880\text{ impostor pairs per DB}$$
   - Total across all 4 databases: $4 \times 2,880 = 11,520\text{ pairs}$.

### 4.2 FVC2004 Protocols (Evaluated per DB: DB1_B, DB2_B, DB3_B, DB4_B)
FVC2004 shares the exact same 10 fingers $\times$ 8 impressions structure as FVC2002:
- **Genuine Pairs**: 280 pairs per DB ($1,120$ total across DB1–DB4).
- **Standard Impostor Pairs**: 45 pairs per DB ($180$ total across DB1–DB4).
- **Full Cross-Finger Impostor Pairs**: 2,880 pairs per DB ($11,520$ total across DB1–DB4).

### 4.3 SOCOFing Protocols
Because `Real/` has only **1 impression per finger**, genuine pairs **cannot** be generated using `Real/` alone. Evaluation requires cross-matching `Real` (enrolled reference) against `Altered` (probe).

#### Protocol Options:

1. **Full Verification Protocol (Real vs. Altered)**:
   - Reference set: `Real` (6,000 images).
   - Probe set: `Altered` (Difficulty split: Easy: 17,931; Medium: 17,067; Hard: 14,272).
   - Genuine pairs: For each altered probe, pair with its matching real reference `(Subject, Hand, Finger)`:
     - Easy: 17,931 genuine pairs.
     - Medium: 17,067 genuine pairs.
     - Hard: 14,272 genuine pairs.
   - Exhaustive impostor pairs: $17,931 \times 5,999 \approx 1.07 \times 10^8$ pairs. This is computationally prohibitive for unaccelerated CPU matching.

2. **Standardized Stratified Subset Protocol (Recommended for CI/Benchmarking)**:
   - To make computation tractable while preserving statistical validity:
   - Take the first **100 subjects** (Subject IDs 1 to 100), covering 1,000 unique fingers.
   - **Genuine pairs**:
     - Real vs. Altered-Easy: ~2,990 pairs.
     - Real vs. Altered-Medium: ~2,840 pairs.
     - Real vs. Altered-Hard: ~2,380 pairs.
   - **Impostor pairs**:
     - Same finger type across different subjects (e.g. Subject $i$ Left Index vs Subject $j$ Left Index, $i \ne j$), or cross-finger matching yielding a fixed balanced sample of 10,000–50,000 impostor pairs.

---

## 5. Dataset-Specific Complications & Licensing

### 5.1 Technical Complications

1. **Resolution & Physical Scale**:
   - FVC2002 and FVC2004 are acquired at 500–569 DPI with typical ridge frequencies of 8–10 pixels per period.
   - SOCOFing images are $96 \times 103$ pixels at approximately 200 DPI. Ridge spacing is much narrower (3–5 pixels per ridge). Filtering kernels (such as Gabor filters in `Enhancer` tuned for 500 DPI) and spatial distance thresholds (e.g., minutiae distance tolerance `maxDistance = 15.0`) may fail or require scale adaptation.
2. **Channel Format**:
   - FVC images are 8-bit single-channel grayscale TIFFs.
   - SOCOFing images are 32-bit BGRA DIB BMPs (`BITMAPV5HEADER`). Ingestion must convert BGRA to single-channel 8-bit grayscale before feeding the pipeline.
3. **Severe Synthetic Alterations**:
   - `Altered-Hard` in SOCOFing contains heavy Z-cuts and full obliterations covering >50% of the fingerprint area. Extractor minutiae counts may drop drastically or produce false minutiae along synthetic cut borders.

### 5.2 Licensing & Usage Terms

- **FVC2002 & FVC2004**:
  - Copyright: Biometric System Laboratory (University of Bologna), Pattern Recognition and Image Processing Laboratory (Michigan State University), and Biometric Test Center (San Jose State University).
  - Permitted Use: Academic research, non-commercial benchmarking, and internal algorithmic evaluation.
  - Restrictions: Commercial redistribution of raw fingerprint images is prohibited.
- **SOCOFing**:
  - Authors: Sokoto Coventry Fingerprint Dataset (Shehu et al., 2018).
  - License: Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
  - Restrictions: Permitted strictly for academic and non-commercial research; must not be embedded or distributed within commercial product binaries.

---

## 6. CLI and Tooling Compatibility Assessment

### 6.1 Current CLI Implementation (`src/main.cpp`)
- **Regex filter**:
  ```cpp
  std::regex fvcRegex(R"(^(\d+)_(\d+)\.(tif|tiff|bmp|png|jpg)$)", std::regex::icase);
  ```
  - This regex works seamlessly for **all 8 FVC databases** (`FVC2002/DB1_B`–`DB4_B`, `FVC2004/DB1_B`–`DB4_B`).
  - It **fails completely** on SOCOFing filenames (e.g. `100__M_Left_index_finger.BMP`), ignoring every file.
- **Single-Directory Grouping**:
  - The CLI assumes all impressions of all fingers reside in a single directory and parses integer finger IDs to build `std::map<int, std::vector<size_t>> fingerGroups`.
  - Works for FVC's within-database verification.
  - Cannot evaluate cross-directory reference-vs-probe setups (such as SOCOFing `Real/` vs `Altered-Easy/`).

### 6.2 Tooling Action Plan
1. **FVC2002 & FVC2004**:
   - Zero C++ code modifications needed. The current CLI can be executed directly against any of the 8 databases:
     ```bash
     kipsma_matcher benchmark --dataset "D:\Career\KIPSMA\FingerprintMatcher-Data\FVC2004\DB1_B" --strategy baseline --output results.json
     ```
2. **SOCOFing**:
   - Recommended non-invasive approach: Create a dedicated Python evaluation script (`benchmark_results/run_socofing_eval.py`) or extend the CLI with a `--dataset-type socofing` mode and `--probe-dir` option to handle cross-directory pairing, BGRA-to-grayscale decoding, and filename parsing without touching core matcher algorithms.

---

## 7. Recommended Next Steps

1. **Step 1 (Immediate & Zero-Risk)**: Execute cross-database benchmark evaluation on the remaining 7 FVC databases (`FVC2002 DB2_B–DB4_B` and `FVC2004 DB1_B–DB4_B`) using the existing CLI without modifying any matcher code.
2. **Step 2**: Analyze baseline vs experimental performance across all FVC sensors (capacitive, thermal sweep, optical, synthetic).
3. **Step 3**: Introduce scale-adaptation or dedicated protocol script for SOCOFing evaluation.

