import json
import re

with open(r'D:\Career\KIPSMA\kipsma_fingerprint_matcher\benchmark_results\fvc_standard_raw_results.json', 'r') as f:
    text = f.read()

text = re.sub(r'"dataset": ".*?"', '"dataset": "DB1_B"', text)
d = json.loads(text)

gen_scores = [p['score'] for p in d['pairs'] if p['isGenuine']]
imp_scores = [p['score'] for p in d['pairs'] if not p['isGenuine']]

gen_zero = sum(1 for s in gen_scores if s == 0)
imp_zero = sum(1 for s in imp_scores if s == 0)

print(f"Genuine count: {len(gen_scores)}, zeros: {gen_zero} ({gen_zero/len(gen_scores)*100:.1f}%), min: {min(gen_scores):.4f}, max: {max(gen_scores):.4f}")
print(f"Impostor count: {len(imp_scores)}, zeros: {imp_zero} ({imp_zero/len(imp_scores)*100:.1f}%), min: {min(imp_scores):.4f}, max: {max(imp_scores):.4f}")

bestEer = 100.0
bestEerTh = 0.0
for tInt in range(201):
    t = tInt * 0.5
    falseMatches = sum(1 for s in imp_scores if s >= t)
    fmr = 100.0 * falseMatches / len(imp_scores)
    falseNonMatches = sum(1 for s in gen_scores if s < t)
    fnmr = 100.0 * falseNonMatches / len(gen_scores)
    eerDiff = abs(fmr - fnmr)
    cond = eerDiff < abs(bestEer - (fmr + fnmr)*0.5)
    if tInt <= 6:
        print(f"t={t:.1f}: FMR={fmr:.2f}%, FNMR={fnmr:.2f}%, eerDiff={eerDiff:.2f}, rhs={abs(bestEer - (fmr+fnmr)*0.5):.2f}, cond={cond}")
    if cond:
        bestEer = (fmr + fnmr)*0.5
        bestEerTh = t

print(f"Resulting CLI bestEer={bestEer}, threshold={bestEerTh}")

