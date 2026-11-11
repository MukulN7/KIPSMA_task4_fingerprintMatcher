import os
import sys
import json
import math
import subprocess
import re
from pathlib import Path

def main():
    repo_root = Path(r"D:\Career\KIPSMA\kipsma_fingerprint_matcher")
    dataset_dir = Path(r"D:\Career\KIPSMA\FingerprintMatcher-Data\FVC2002\DB1_B")
    cli_exe = repo_root / "build" / "bin" / "Release" / "fingerprint_cli.exe"
    output_dir = repo_root / "benchmark_results"
    output_dir.mkdir(parents=True, exist_ok=True)

    print("=================================================================")
    print(" KIPSMA Fingerprint Matcher Benchmark & Metrics Validator")
    print("=================================================================")
    print(f"Dataset path: {dataset_dir}")
    print(f"CLI executable: {cli_exe}")

    if not cli_exe.exists():
        print(f"Error: CLI executable not found at {cli_exe}")
        sys.exit(1)

    fvc_pattern = re.compile(r"^(\d+)_(\d+)\.(tif|tiff)$", re.IGNORECASE)
    images = []
    for f in dataset_dir.glob("*.tif"):
        m = fvc_pattern.match(f.name)
        if m:
            finger_id = int(m.group(1))
            impression_id = int(m.group(2))
            images.append({
                "path": str(f),
                "filename": f.name,
                "finger": finger_id,
                "impression": impression_id
            })

    images.sort(key=lambda x: (x["finger"], x["impression"]))
    print(f"Discovered {len(images)} images in DB1_B.")

    finger_groups = {}
    for img in images:
        finger_groups.setdefault(img["finger"], []).append(img)

    fingers = sorted(finger_groups.keys())
    print(f"Fingers present ({len(fingers)}): {fingers}")

    # Generate genuine pairs: distinct impressions of the same finger
    genuine_pairs = []
    for f in fingers:
        imgs = finger_groups[f]
        for i in range(len(imgs)):
            for j in range(i + 1, len(imgs)):
                genuine_pairs.append({
                    "probe": imgs[i],
                    "candidate": imgs[j],
                    "is_genuine": True
                })

    # Standard FVC Protocol Impostor pairs (first impression vs first impression across distinct fingers)
    # 10 fingers -> 10 * 9 / 2 = 45 pairs
    impostor_fvc_pairs = []
    for i in range(len(fingers)):
        for j in range(i + 1, len(fingers)):
            p1 = finger_groups[fingers[i]][0]
            p2 = finger_groups[fingers[j]][0]
            impostor_fvc_pairs.append({
                "probe": p1,
                "candidate": p2,
                "is_genuine": False
            })

    # Full Impostor Protocol (all distinct-finger cross-pairs): 80 * 72 / 2 = 2880 pairs
    impostor_full_pairs = []
    for i in range(len(fingers)):
        for j in range(i + 1, len(fingers)):
            imgs1 = finger_groups[fingers[i]]
            imgs2 = finger_groups[fingers[j]]
            for p1 in imgs1:
                for p2 in imgs2:
                    impostor_full_pairs.append({
                        "probe": p1,
                        "candidate": p2,
                        "is_genuine": False
                    })

    print(f"Genuine pairs count: {len(genuine_pairs)}")
    print(f"FVC Standard Impostor pairs count (1st impressions): {len(impostor_fvc_pairs)}")
    print(f"Full Distinct-Finger Impostor pairs count: {len(impostor_full_pairs)}")

    # Run pairwise evaluation using CLI match command to get full, detailed pair-level results
    # To be extremely fast, we can extract templates or call match
    # Let's check how fast match is: 80 extractions, then 280 + 45 = 325 matches or 3160 matches.
    # The existing C++ CLI has built-in extraction and caching in memory for the 'benchmark' command!
    # Let's see if we can run python calling match or check the CLI benchmark results.

if __name__ == "__main__":
    main()

