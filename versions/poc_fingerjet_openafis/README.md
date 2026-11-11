# Exploratory POC: FingerJetFX OSE & OpenAFIS Comparative Evaluation

## Overview
This directory preserves the initial research proof-of-concept (POC) developed to benchmark external biometric libraries (DigitalPersona FingerJetFX OSE and OpenAFIS) against standard fingerprint competition datasets before engineering the self-contained native C++17 `kipsma_fingerprint_matcher` pipeline.

## Contents
- `main.cpp`: Standalone executable evaluating extraction latency and Delaunay triplet comparisons using dynamic linkage against external `FJFX.dll` and `openafis.lib`.
- `benchmark_runner.cpp`: Automated harness executing 1:1 genuine and impostor matching across FVC2002 and FVC2004 databases.
- `CMakeLists.txt`: Build configuration linking local reference builds of FingerJetFX and OpenAFIS.

## Findings & Evolution
- The POC proved the feasibility of combining ISO/IEC 19794-2:2005 minutiae records with local triplet matching.
- Latency benchmarks demonstrated ~19 ms extraction and ~60 µs comparison times.
- These findings guided the design of the production `kipsma_fingerprint` architecture, which encapsulates preprocessing, ridge enhancement, and matching into a single deterministic C++17 library.
