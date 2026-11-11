import json
import statistics

with open('benchmark_results/cross_database_eval_summary.json') as f:
    data = json.load(f)

# Fill DB2_B runtimes from our previous runs
data['FVC2002_DB2_B']['baseline']['runtime_sec'] = 6.77
data['FVC2002_DB2_B']['local']['runtime_sec'] = 8.23

with open('benchmark_results/cross_database_eval_summary.json', 'w') as f:
    json.dump(data, f, indent=2)

print("=== 7 NEW DATABASES BENCHMARK RESULTS ===")
base_aucs = []
local_aucs = []
base_eers = []
local_eers = []
base_times = []
local_times = []

for db, strats in data.items():
    b_m = strats['baseline']['metrics']
    l_m = strats['local']['metrics']
    b_t = strats['baseline']['runtime_sec']
    l_t = strats['local']['runtime_sec']
    
    base_aucs.append(b_m['auc'])
    local_aucs.append(l_m['auc'])
    base_eers.append(b_m['eer'])
    local_eers.append(l_m['eer'])
    base_times.append(b_t)
    local_times.append(l_t)
    
    auc_diff = l_m['auc'] - b_m['auc']
    eer_diff = l_m['eer'] - b_m['eer']
    print(f"{db:15s} | Baseline: AUC={b_m['auc']:.4f}, EER={b_m['eer']:.2f}%, T={b_t:.2f}s | Local: AUC={l_m['auc']:.4f}, EER={l_m['eer']:.2f}%, T={l_t:.2f}s | Delta: AUC={auc_diff:+.4f}, EER={eer_diff:+.2f}%")

print("\n=== SUMMARY STATISTICS ACROSS 7 DATABASES ===")
print(f"Baseline AUC:  Mean={statistics.mean(base_aucs):.4f}, Median={statistics.median(base_aucs):.4f}")
print(f"Local AUC:     Mean={statistics.mean(local_aucs):.4f}, Median={statistics.median(local_aucs):.4f}")
print(f"AUC Delta:     Mean={statistics.mean(local_aucs) - statistics.mean(base_aucs):+.4f}, Median={statistics.median(local_aucs) - statistics.median(base_aucs):+.4f}")
print(f"Baseline EER:  Mean={statistics.mean(base_eers):.2f}%, Median={statistics.median(base_eers):.2f}%")
print(f"Local EER:     Mean={statistics.mean(local_eers):.2f}%, Median={statistics.median(local_eers):.2f}%")
print(f"EER Delta:     Mean={statistics.mean(local_eers) - statistics.mean(base_eers):+.2f}%, Median={statistics.median(local_eers) - statistics.median(base_eers):+.2f}%")
print(f"Baseline Time: Mean={statistics.mean(base_times):.2f}s, Median={statistics.median(base_times):.2f}s")
print(f"Local Time:    Mean={statistics.mean(local_times):.2f}s, Median={statistics.median(local_times):.2f}s")

# Let's also include FVC2002 DB1_B to see the 8-database aggregate
# From earlier:
# FVC2002 DB1_B Baseline: AUC=0.4879, EER=53.10%
# FVC2002 DB1_B Local:    AUC=0.6387, EER=42.18%
all8_base_aucs = [0.4879] + base_aucs
all8_local_aucs = [0.6387] + local_aucs
all8_base_eers = [53.10] + base_eers
all8_local_eers = [42.18] + local_eers

print("\n=== SUMMARY STATISTICS ACROSS ALL 8 FVC DATABASES (INC. FVC2002 DB1_B) ===")
print(f"Baseline AUC:  Mean={statistics.mean(all8_base_aucs):.4f}, Median={statistics.median(all8_base_aucs):.4f}")
print(f"Local AUC:     Mean={statistics.mean(all8_local_aucs):.4f}, Median={statistics.median(all8_local_aucs):.4f}")
print(f"AUC Delta:     Mean={statistics.mean(all8_local_aucs) - statistics.mean(all8_base_aucs):+.4f}, Median={statistics.median(all8_local_aucs) - statistics.median(all8_base_aucs):+.4f}")
print(f"Baseline EER:  Mean={statistics.mean(all8_base_eers):.2f}%, Median={statistics.median(all8_base_eers):.2f}%")
print(f"Local EER:     Mean={statistics.mean(all8_local_eers):.2f}%, Median={statistics.median(all8_local_eers):.2f}%")
print(f"EER Delta:     Mean={statistics.mean(all8_local_eers) - statistics.mean(all8_base_eers):+.2f}%, Median={statistics.median(all8_local_eers) - statistics.median(all8_base_eers):+.2f}%")

