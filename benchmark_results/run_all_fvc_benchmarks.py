import os
import sys
import time
import subprocess
import json
import math

CLI_PATH = r"build\bin\Release\fingerprint_cli.exe"
DATA_ROOT = r"D:\Career\KIPSMA\FingerprintMatcher-Data"

DATABASES = [
    ("FVC2002", "DB2_B", os.path.join(DATA_ROOT, "FVC2002", "DB2_B")),
    ("FVC2002", "DB3_B", os.path.join(DATA_ROOT, "FVC2002", "DB3_B")),
    ("FVC2002", "DB4_B", os.path.join(DATA_ROOT, "FVC2002", "DB4_B")),
    ("FVC2004", "DB1_B", os.path.join(DATA_ROOT, "FVC2004", "DB1_B")),
    ("FVC2004", "DB2_B", os.path.join(DATA_ROOT, "FVC2004", "DB2_B")),
    ("FVC2004", "DB3_B", os.path.join(DATA_ROOT, "FVC2004", "DB3_B")),
    ("FVC2004", "DB4_B", os.path.join(DATA_ROOT, "FVC2004", "DB4_B")),
]

STRATEGIES = ["baseline", "local"]

def compute_metrics(json_path):
    with open(json_path, 'r') as f:
        data = json.load(f)
    
    pairs = data['pairs']
    gen_scores = [p['score'] for p in pairs if p['isGenuine']]
    imp_scores = [p['score'] for p in pairs if not p['isGenuine']]
    
    gen_scores.sort()
    imp_scores.sort()
    
    num_gen = len(gen_scores)
    num_imp = len(imp_scores)
    
    def mean_std(arr):
        if not arr: return 0.0, 0.0
        m = sum(arr) / len(arr)
        v = sum((x - m)**2 for x in arr) / len(arr)
        return m, math.sqrt(v)
    
    gen_mean, gen_std = mean_std(gen_scores)
    imp_mean, imp_std = mean_std(imp_scores)
    
    # ROC-AUC
    concordant = 0
    tied = 0
    for g in gen_scores:
        for im in imp_scores:
            if g > im:
                concordant += 1
            elif g == im:
                tied += 1
    auc = (concordant + 0.5 * tied) / (num_gen * num_imp) if (num_gen * num_imp) > 0 else 0.0
    
    # EER
    all_scores = sorted(list(set(gen_scores + imp_scores)))
    thresholds = [-1.0] + all_scores + [all_scores[-1] + 1.0] if all_scores else [0.0]
    
    best_eer_diff = 1e9
    eer = 0.0
    eer_thresh = 0.0
    eer_fmr = 0.0
    eer_fnmr = 0.0
    
    for t in thresholds:
        false_accepts = sum(1 for s in imp_scores if s >= t)
        false_rejects = sum(1 for s in gen_scores if s < t)
        fmr = false_accepts / num_imp if num_imp > 0 else 0.0
        fnmr = false_rejects / num_gen if num_gen > 0 else 0.0
        diff = abs(fmr - fnmr)
        if diff < best_eer_diff:
            best_eer_diff = diff
            eer = (fmr + fnmr) / 2.0
            eer_thresh = t
            eer_fmr = fmr
            eer_fnmr = fnmr
            
    return {
        "num_gen": num_gen,
        "num_imp": num_imp,
        "gen_mean": gen_mean,
        "gen_std": gen_std,
        "imp_mean": imp_mean,
        "imp_std": imp_std,
        "auc": auc,
        "eer": eer * 100.0,
        "eer_thresh": eer_thresh,
        "eer_fmr": eer_fmr * 100.0,
        "eer_fnmr": eer_fnmr * 100.0
    }

def main():
    results = {}
    
    for fvc_year, db_name, db_path in DATABASES:
        key = f"{fvc_year}_{db_name}"
        results[key] = {}
        print(f"\n==================== Benchmarking {key} ====================")
        print(f"Path: {db_path}")
        
        for strat in STRATEGIES:
            out_json = os.path.join("benchmark_results", f"{fvc_year.lower()}_{db_name.lower()}_{strat}.json")
            print(f"--- Strategy: {strat} -> {out_json} ---")
            
            # Check if file exists already (e.g. DB2_B was already run)
            if os.path.exists(out_json):
                print(f"Report already exists: {out_json}. Re-computing metrics...")
                m = compute_metrics(out_json)
                results[key][strat] = {
                    "output_file": out_json,
                    "runtime_sec": None,
                    "metrics": m,
                    "status": "EXISTING_OK"
                }
                print(f"  AUC: {m['auc']:.4f}, EER: {m['eer']:.2f}% (gen={m['num_gen']}, imp={m['num_imp']})")
                continue
            
            cmd = [
                CLI_PATH,
                "benchmark",
                db_path,
                out_json,
                "--strategy", strat
            ]
            
            t0 = time.time()
            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
                elapsed = time.time() - t0
                print(f"Execution succeeded in {elapsed:.2f} s")
                
                if os.path.exists(out_json):
                    m = compute_metrics(out_json)
                    results[key][strat] = {
                        "output_file": out_json,
                        "runtime_sec": round(elapsed, 2),
                        "metrics": m,
                        "status": "OK"
                    }
                    print(f"  AUC: {m['auc']:.4f}, EER: {m['eer']:.2f}% (gen={m['num_gen']}, imp={m['num_imp']})")
                else:
                    results[key][strat] = {
                        "output_file": out_json,
                        "runtime_sec": round(elapsed, 2),
                        "status": "MISSING_OUTPUT"
                    }
                    print("  ERROR: output JSON not generated!")
            except subprocess.CalledProcessError as e:
                elapsed = time.time() - t0
                print(f"Execution failed after {elapsed:.2f} s with return code {e.returncode}")
                print(f"Stderr: {e.stderr}")
                results[key][strat] = {
                    "output_file": out_json,
                    "runtime_sec": round(elapsed, 2),
                    "status": f"FAILED_CODE_{e.returncode}",
                    "error": e.stderr
                }
            except Exception as e:
                elapsed = time.time() - t0
                print(f"Execution exception: {e}")
                results[key][strat] = {
                    "output_file": out_json,
                    "runtime_sec": round(elapsed, 2),
                    "status": "EXCEPTION",
                    "error": str(e)
                }

    summary_file = os.path.join("benchmark_results", "cross_database_eval_summary.json")
    with open(summary_file, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"\nAll benchmark runs completed. Summary saved to {summary_file}")

if __name__ == '__main__':
    main()

