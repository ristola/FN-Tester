import csv
import numpy as np

path = "/Users/rts/Development/CYD-4.3-FN-Tester/FN_OUTPUT_Tester_Handoff/captures/digital.csv"

rows = []
with open(path, newline="") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        rows.append((float(row[0]), int(row[1]), int(row[2])))

SR_US = 2.0
WIN_START = 0.2
WIN_END = 1.0  # 800ms window - big enough to see out past 50ms with room to spare

n = int((WIN_END - WIN_START) * 1e6 / SR_US)
sig = np.zeros(n, dtype=np.int8)

for i in range(len(rows) - 1):
    t, lvl, ch1 = rows[i]
    t_next = rows[i + 1][0]
    if t_next < WIN_START:
        continue
    if t > WIN_END:
        break
    start_i = max(0, int((t - WIN_START) * 1e6 / SR_US))
    end_i = min(n, int((t_next - WIN_START) * 1e6 / SR_US))
    if start_i < n and end_i > start_i:
        sig[start_i:end_i] = lvl

sig_f = sig.astype(np.float64) - sig.mean()
f = np.fft.rfft(sig_f, n=2 * n)
acf = np.fft.irfft(f * np.conj(f))[:n]
acf /= acf[0]

def acf_at_us(lag_us):
    i = int(round(lag_us / SR_US))
    return acf[i] if 0 <= i < n else float("nan")

# Direct comparison of specific candidate periods
candidates_us = [206, 412, 1650, 16670, 16670*2, 16670*3, 50000, 50006, 100000, 33340]
print("Direct ACF comparison at candidate lags:")
for c in candidates_us:
    print(f"  lag={c:8.1f}us  acf={acf_at_us(c):.4f}")

# Real local-maxima peak picking over the full range 500us..400ms
lo = int(500 / SR_US)
hi = int(400000 / SR_US)
region = acf[lo:hi]
peaks = []
for i in range(1, len(region) - 1):
    if region[i] > region[i - 1] and region[i] > region[i + 1] and region[i] > 0.3:
        peaks.append((i, region[i]))
peaks.sort(key=lambda p: -p[1])
print(f"\nTop 25 local-maxima peaks (acf > 0.3) in 500us-400ms range:")
for i, val in peaks[:25]:
    lag_us = (i + lo) * SR_US
    print(f"  lag={lag_us:10.1f}us ({lag_us/1000:.4f}ms)  acf={val:.4f}")
