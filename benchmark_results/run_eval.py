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

    print(f"Dataset path: {dataset_dir}")
    print(f"CLI executable: {cli_exe}")
    print(f"Output directory: {output_dir}")

    # Gather all images
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

    # Group by finger
    finger_groups = {}
    for img in images:
        finger_groups.setdefault(img["finger"], []).append(img)

    fingers = sorted(finger_groups.keys())
    print(f"Fingers present: {fingers}")

    # 1. Genuine pairs: all distinct impression pairs for each finger (8 * 7 / 2 = 28 per finger * 10 fingers = 280)
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

    # 2. Impostor pairs:
    # Standard FVC competition protocol: first impression vs first impression across distinct fingers (10 * 9 / 2 = 45)
    # Full cross-finger protocol (all distinct-finger cross-pairs): 80 * 72 / 2 = 2880 pairs
    # Let's generate both standard 45 and full 2880 to provide complete benchmark insight.
    impostor_fvc_protocol = []
    for i in range(len(fingers)):
        for j in range(i + 1, len(fingers)):
            p1 = finger_groups[fingers[i]][0]  # impression 1
            p2 = finger_groups[fingers[j]][0]  # impression 1
            impostor_fvc_protocol.append({
                "probe": p1,
                "candidate": p2,
                "is_genuine": False
            })

    all_impostor_pairs = []
    for i in range(len(fingers)):
        for j in range(i + 1, len(fingers)):
            imgs1 = finger_groups[fingers[i]]
            imgs2 = finger_groups[fingers[j]]
            for p1 in imgs1:
                for p2 in imgs2:
                    all_impostor_pairs.append({
                        "probe": p1,
                        "candidate": p2,
                        "is_genuine": False
                    })

    print(f"Genuine pairs: {len(genuine_pairs)}")
    print(f"Standard FVC Impostor pairs (1st impression vs 1st impression): {len(impostor_fvc_protocol)}")
    print(f"All Distinct-Finger Impostor pairs: {len(all_impostor_pairs)}")

    # We evaluate all 280 genuine pairs and all 2880 distinct-finger impostor pairs.
    # Total comparisons = 280 + 2880 = 3160 pairs.
    total_pairs = genuine_pairs + all_impostor_pairs

    # We can run the existing fingerprint_cli match command or run via a benchmark helper
    # Notice fingerprint_cli has benchmark command built in!
    # Let's run the CLI benchmark command directly first, and also evaluate pair-by-pair if needed.

if __name__ == "__main__":
    main()

