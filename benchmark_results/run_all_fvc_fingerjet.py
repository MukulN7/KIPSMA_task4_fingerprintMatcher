import os
import subprocess
import json
import math
import time

RUNNER_EXE = r"D:\Career\KIPSMA\FingerprintMatcher-References\fingerjet_openafis_poc\build\Release\benchmark_runner.exe"
DATA_ROOT = r"D:\Career\KIPSMA\FingerprintMatcher-Data"
RESULTS_DIR = r"D:\Career\KIPSMA\kipsma_fingerprint_matcher\benchmark_results"

DATABASES = [
    # (Year, DB, DPI, Sensor Description)
    ("FVC2002", "DB1_B", 500, "Optical Identix TouchView II"),
    ("FVC2002", "DB2_B", 569, "Optical Biometrika FX2000"),
    ("FVC2002", "DB3_B", 500, "Capacitive Precise 100 SC"),
    ("FVC2002", "DB4_B", 500, "Synthetic SFinGe v2.51"),
    ("FVC2004", "DB1_B", 500, "Optical CrossMatch V300"),
    ("FVC2004", "DB2_B", 512, "Optical DigitalPersona U.are.U 4000"),
    ("FVC2004", "DB3_B", 512, "Thermal Sweep Atmel FingerChip"),
    ("FVC2004", "DB4_B", 500, "Synthetic SFinGe v3.0"),
]

def compute_metrics(json_path):
    with open(json_path, 'r') as f:
        d = json.load(f)
    
    pairs = d['pairs']
    gens = sorted([p['score'] for p in pairs if p['isGenuine']])
    imps = sorted([p['score'] for p in pairs if not p['isGenuine']])
    N_g = len(gens)
    N_i = len(imps)

    def mean_std(arr):
        if not arr: return 0.0, 0.0
        m = sum(arr) / len(arr)
        v = sum((x - m)**2 for x in arr) / len(arr)
        return m, math.sqrt(v)

    gen_mean, gen_std = mean_std(gens)
    imp_mean, imp_std = mean_std(imps)

    # Wilcoxon-Mann-Whitney AUC
    concordant = sum(1 for g in gens for im in imps if g > im)
    tied = sum(1 for g in gens for im in imps if g == im)
    auc = (concordant + 0.5 * tied) / (N_g * N_i) if (N_g * N_i) > 0 else 0.0

    # ROC / EER calculation
    all_scores = sorted(list(set(gens + imps)))
    thresholds = [-1.0] + all_scores + [all_scores[-1] + 1.0] if all_scores else [0.0]

    best_diff = 1e9
    eer = 0.0
    eer_thresh = 0.0
    eer_fmr = 0.0
    eer_fnmr = 0.0

    for t in thresholds:
        false_accepts = sum(1 for s in imps if s >= t)
        false_rejects = sum(1 for s in gens if s < t)
        fmr = false_accepts / N_i if N_i > 0 else 0.0
        fnmr = false_rejects / N_g if N_g > 0 else 0.0
        diff = abs(fmr - fnmr)
        if diff < best_diff:
            best_diff = diff
            eer = (fmr + fnmr) / 2.0
            eer_thresh = t
            eer_fmr = fmr
            eer_fnmr = fnmr

    # Threshold T = 2.0 fixed metrics (standard decision threshold)
    fmr_at_2 = sum(1 for s in imps if s >= 2) / N_i if N_i > 0 else 0.0
    fnmr_at_2 = sum(1 for s in gens if s < 2) / N_g if N_g > 0 else 0.0

    # ZeroFMR and FMR100
    zero_fmr_fnmr = None
    fmr100_fnmr = None
    for t in thresholds:
        fmr = sum(1 for s in imps if s >= t) / N_i if N_i > 0 else 0.0
        fnmr = sum(1 for s in gens if s < t) / N_g if N_g > 0 else 0.0
        if fmr == 0.0 and zero_fmr_fnmr is None:
            zero_fmr_fnmr = fnmr
        if fmr <= 0.01 and fmr100_fnmr is None:
            fmr100_fnmr = fnmr

    return {
        "num_gen": N_g,
        "num_imp": N_i,
        "extract_failures": d.get("extractFailures", 0),
        "parse_failures": d.get("parseFailures", 0),
        "comparison_failures": d.get("comparisonFailures", 0),
        "avg_extract_ms": d.get("avgExtractMs", 0.0),
        "avg_match_us": d.get("avgMatchUs", 0.0),
        "gen_mean": gen_mean,
        "gen_std": gen_std,
        "gen_min": min(gens) if gens else 0,
        "gen_max": max(gens) if gens else 0,
        "imp_mean": imp_mean,
        "imp_std": imp_std,
        "imp_min": min(imps) if imps else 0,
        "imp_max": max(imps) if imps else 0,
        "auc": auc,
        "eer": eer * 100.0,
        "eer_thresh": eer_thresh,
        "eer_fmr": eer_fmr * 100.0,
        "eer_fnmr": eer_fnmr * 100.0,
        "fmr_at_2": fmr_at_2 * 100.0,
        "fnmr_at_2": fnmr_at_2 * 100.0,
        "zero_fmr_fnmr": (zero_fmr_fnmr * 100.0) if zero_fmr_fnmr is not None else None,
        "fmr100_fnmr": (fmr100_fnmr * 100.0) if fmr100_fnmr is not None else None,
    }

def main():
    env = os.environ.copy()
    dll_paths = [
        r"D:\Career\KIPSMA\FingerprintMatcher-References\FingerJetFXOSE\FingerJetFXOSE\dist\win64\Release",
        r"C:\tools\opencv3411\x64\vc15\bin",
        r"C:\tools\opencv3411\bin"
    ]
    env["PATH"] = ";".join(dll_paths) + ";" + env.get("PATH", "")

    all_results = {}

    print("================================================================================")
    print(" Running FingerJetFX OSE + OpenAFIS on All 8 FVC2002 & FVC2004 Databases")
    print("================================================================================\n")

    for year, db, dpi, desc in DATABASES:
        key = f"{year}_{db}"
        db_path = os.path.join(DATA_ROOT, year, db)
        out_json = os.path.join(RESULTS_DIR, f"fingerjet_openafis_{year.lower()}_{db.lower()}.json")

        print(f"--- Running {key} ({desc}, DPI={dpi}) ---")
        cmd = [RUNNER_EXE, db_path.replace("\\", "/"), out_json.replace("\\", "/"), str(dpi)]
        
        t0 = time.time()
        res = subprocess.run(cmd, env=env, capture_output=True, text=True)
        elapsed = time.time() - t0

        if res.returncode != 0:
            print(f"FAILED (code {res.returncode}):\n{res.stderr}")
            continue

        print(f"Completed in {elapsed:.2f} s")
        m = compute_metrics(out_json)
        all_results[key] = {
            "year": year,
            "db": db,
            "dpi": dpi,
            "sensor": desc,
            "json_path": out_json,
            "elapsed_sec": round(elapsed, 2),
            "metrics": m
        }
        print(f"  AUC: {m['auc']:.4f} | EER: {m['eer']:.2f}% (T={m['eer_thresh']}, FMR={m['eer_fmr']:.2f}%, FNMR={m['eer_fnmr']:.2f}%)")
        print(f"  Gen: {m['gen_mean']:.2f}±{m['gen_std']:.2f} [{m['gen_min']}, {m['gen_max']}] | Imp: {m['imp_mean']:.2f}±{m['imp_std']:.2f} [{m['imp_min']}, {m['imp_max']}]")
        print(f"  Failures: extract={m['extract_failures']}, parse={m['parse_failures']}, match={m['comparison_failures']}\n")

    # Save summary json
    summary_path = os.path.join(RESULTS_DIR, "fingerjet_openafis_all_fvc_summary.json")
    with open(summary_path, 'w') as f:
        json.dump(all_results, f, indent=2)
    print(f"Saved complete summary to: {summary_path}")

if __name__ == '__main__':
    main()

