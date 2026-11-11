import json
import math
import sys

def analyze(json_path):
    with open(json_path, 'r') as f:
        data = json.load(f)
    
    pairs = data['pairs']
    gen_scores = [p['score'] for p in pairs if p['isGenuine']]
    imp_scores = [p['score'] for p in pairs if not p['isGenuine']]
    
    gen_scores.sort()
    imp_scores.sort()
    
    def percentile(arr, p):
        if not arr: return 0.0
        k = (len(arr) - 1) * (p / 100.0)
        f = math.floor(k)
        c = math.ceil(k)
        if f == c:
            return arr[int(k)]
        d0 = arr[int(f)] * (c - k)
        d1 = arr[int(c)] * (k - f)
        return d0 + d1

    def mean_std(arr):
        if not arr: return 0.0, 0.0
        m = sum(arr) / len(arr)
        v = sum((x - m)**2 for x in arr) / len(arr)
        return m, math.sqrt(v)

    gen_mean, gen_std = mean_std(gen_scores)
    imp_mean, imp_std = mean_std(imp_scores)
    
    # Calculate ROC-AUC via Wilcoxon-Mann-Whitney statistic
    # Higher score = genuine.
    # WMW U-statistic: sum of I(gen > imp) + 0.5 * I(gen == imp) / (N_gen * N_imp)
    num_gen = len(gen_scores)
    num_imp = len(imp_scores)
    
    concordant = 0
    tied = 0
    for g in gen_scores:
        for im in imp_scores:
            if g > im:
                concordant += 1
            elif g == im:
                tied += 1
    auc = (concordant + 0.5 * tied) / (num_gen * num_imp)
    
    # ROC curve and EER
    # Score thresholds: all unique scores + bounds
    all_scores = sorted(list(set(gen_scores + imp_scores)))
    thresholds = [-1.0] + all_scores + [all_scores[-1] + 1.0]
    
    roc_points = []
    best_eer_diff = 1e9
    eer = 0.0
    eer_thresh = 0.0
    eer_fmr = 0.0
    eer_fnmr = 0.0
    
    # We define match if score >= threshold
    for t in thresholds:
        # FMR = fraction of impostors with score >= t
        # FNMR = fraction of genuines with score < t
        false_accepts = sum(1 for s in imp_scores if s >= t)
        false_rejects = sum(1 for s in gen_scores if s < t)
        
        fmr = false_accepts / num_imp
        fnmr = false_rejects / num_gen
        roc_points.append((fmr, fnmr, t))
        
        diff = abs(fmr - fnmr)
        if diff < best_eer_diff:
            best_eer_diff = diff
            eer = (fmr + fnmr) / 2.0
            eer_thresh = t
            eer_fmr = fmr
            eer_fnmr = fnmr
            
    # ZeroFMR: lowest threshold where FMR == 0, and corresponding FNMR
    # FMR100: lowest threshold where FMR <= 0.01 (1%), and corresponding FNMR
    # FMR1000: lowest threshold where FMR <= 0.001 (0.1%), and corresponding FNMR
    zero_fmr_fnmr = None
    fmr100_fnmr = None
    fmr1000_fnmr = None
    
    for fmr, fnmr, t in roc_points:
        if fmr == 0.0 and zero_fmr_fnmr is None:
            zero_fmr_fnmr = fnmr
        if fmr <= 0.01 and fmr100_fnmr is None:
            fmr100_fnmr = fnmr
        if fmr <= 0.001 and fmr1000_fnmr is None:
            fmr1000_fnmr = fnmr

    print(f"=== Analysis for {json_path} ===")
    print(f"Genuine Pairs: {num_gen}")
    print(f"  Min: {gen_scores[0]:.4f}, Max: {gen_scores[-1]:.4f}")
    print(f"  Mean: {gen_mean:.4f}, Std: {gen_std:.4f}")
    print(f"  P10: {percentile(gen_scores, 10):.4f}")
    print(f"  P25 (Q1): {percentile(gen_scores, 25):.4f}")
    print(f"  Median (P50): {percentile(gen_scores, 50):.4f}")
    print(f"  P75 (Q3): {percentile(gen_scores, 75):.4f}")
    print(f"  P90: {percentile(gen_scores, 90):.4f}")
    print(f"  P95: {percentile(gen_scores, 95):.4f}")
    print(f"  P99: {percentile(gen_scores, 99):.4f}")
    print(f"  Zero scores count: {sum(1 for s in gen_scores if s == 0.0)} ({sum(1 for s in gen_scores if s == 0.0)/num_gen*100:.2f}%)")
    
    print(f"\nImpostor Pairs: {num_imp}")
    print(f"  Min: {imp_scores[0]:.4f}, Max: {imp_scores[-1]:.4f}")
    print(f"  Mean: {imp_mean:.4f}, Std: {imp_std:.4f}")
    print(f"  P10: {percentile(imp_scores, 10):.4f}")
    print(f"  P25 (Q1): {percentile(imp_scores, 25):.4f}")
    print(f"  Median (P50): {percentile(imp_scores, 50):.4f}")
    print(f"  P75 (Q3): {percentile(imp_scores, 75):.4f}")
    print(f"  P90: {percentile(imp_scores, 90):.4f}")
    print(f"  P95: {percentile(imp_scores, 95):.4f}")
    print(f"  P99: {percentile(imp_scores, 99):.4f}")
    print(f"  Zero scores count: {sum(1 for s in imp_scores if s == 0.0)} ({sum(1 for s in imp_scores if s == 0.0)/num_imp*100:.2f}%)")

    print(f"\nROC & Biometric Metrics:")
    print(f"  ROC-AUC: {auc:.4f}")
    print(f"  EER: {eer*100:.2f}% (at threshold = {eer_thresh:.4f}, FMR = {eer_fmr*100:.2f}%, FNMR = {eer_fnmr*100:.2f}%)")
    print(f"  ZeroFMR (FNMR @ FMR = 0%): {zero_fmr_fnmr*100 if zero_fmr_fnmr is not None else 'N/A':.2f}%")
    print(f"  FMR100 (FNMR @ FMR <= 1%): {fmr100_fnmr*100 if fmr100_fnmr is not None else 'N/A':.2f}%")
    print(f"  FMR1000 (FNMR @ FMR <= 0.1%): {fmr1000_fnmr*100 if fmr1000_fnmr is not None else 'N/A':.2f}%")

if __name__ == '__main__':
    if len(sys.argv) > 1:
        for path in sys.argv[1:]:
            analyze(path)
            print("\n" + "="*60 + "\n")
    else:
        analyze('benchmark_results/corrected_benchmark_fvc_standard.json')
        print("\n" + "="*60 + "\n")
        analyze('benchmark_results/corrected_benchmark_full_impostors.json')

