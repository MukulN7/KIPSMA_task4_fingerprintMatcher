import json
import re

with open(r'D:\Career\KIPSMA\kipsma_fingerprint_matcher\benchmark_results\fvc_standard_raw_results.json', 'r') as f:
    text = f.read()

text = re.sub(r'"dataset": ".*?"', '"dataset": "DB1_B"', text)
d = json.loads(text)

gen_scores = [p['score'] for p in d['pairs'] if p['isGenuine']]
imp_scores = [p['score'] for p in d['pairs'] if not p['isGenuine']]

# Fine-grained threshold sweep
min_s = min(min(gen_scores), min(imp_scores))
max_s = max(max(gen_scores), max(imp_scores))

best_diff = 1e9
best_t = 0.0
best_fmr = 0.0
best_fnmr = 0.0

steps = 1000
for step in range(steps + 1):
    t = min_s + (max_s - min_s) * (step / steps)
    fmr = 100.0 * sum(1 for s in imp_scores if s >= t) / len(imp_scores)
    fnmr = 100.0 * sum(1 for s in gen_scores if s < t) / len(gen_scores)
    diff = abs(fmr - fnmr)
    if diff < best_diff:
        best_diff = diff
        best_t = t
        best_fmr = fmr
        best_fnmr = fnmr

print(f"Closest operating point: t = {best_t:.4f}")
print(f"  FMR  = {best_fmr:.2f}% ({sum(1 for s in imp_scores if s >= best_t)} / {len(imp_scores)})")
print(f"  FNMR = {best_fnmr:.2f}% ({sum(1 for s in gen_scores if s < best_t)} / {len(gen_scores)})")
print(f"  Average (EER point) = {(best_fmr + best_fnmr)/2:.2f}%")

