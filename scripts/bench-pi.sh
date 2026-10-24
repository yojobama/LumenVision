#!/usr/bin/env bash
# Measures the running service for a fixed time and writes one JSON report: every node's stage timings (GET /api/profile, reset first),
# plus 1 Hz samples of the CPU cluster frequencies, the NPU/GPU/DDR devfreq load and the SoC temperatures.
#
# Run it on the board with the graph you want to measure already running:
#   scripts/bench-pi.sh [seconds=60] [label=baseline] [output.json]
# then copy the JSON next to docs/research/hw-accel/baselines/ to compare before and after a change.
set -euo pipefail

DURATION="${1:-60}"
LABEL="${2:-baseline}"
OUT="${3:-bench-${LABEL}-$(date +%Y%m%d-%H%M%S).json}"
API="${LUMEN_API:-http://127.0.0.1:5800/api}"

devfreq_load() { # prints "<load percent>" for a devfreq node, 0 when unreadable
    local f="/sys/class/devfreq/$1/load"
    [[ -r "$f" ]] && cut -d@ -f1 "$f" || echo 0
}
cpu_khz() { cat "/sys/devices/system/cpu/cpu$1/cpufreq/scaling_cur_freq" 2>/dev/null || echo 0; }

SAMPLES="$(mktemp)"
trap 'rm -f "$SAMPLES"' EXIT

curl -fsS -X POST "$API/profile/reset" >/dev/null
echo "measuring for ${DURATION}s (label: ${LABEL})" >&2

for ((i = 0; i < DURATION; i++)); do
    temps=""
    for z in /sys/class/thermal/thermal_zone*; do temps+="$(cat "$z/temp" 2>/dev/null || echo 0),"; done
    echo "$(cpu_khz 0) $(cpu_khz 4) $(cpu_khz 6) $(devfreq_load fdab0000.npu) $(devfreq_load fb000000.gpu) $(devfreq_load dmc) ${temps%,}" >>"$SAMPLES"
    sleep 1
done

PROFILE_FILE="$(mktemp)"
trap 'rm -f "$SAMPLES" "$PROFILE_FILE"' EXIT
curl -fsS "$API/profile" >"$PROFILE_FILE"
python3 - "$LABEL" "$DURATION" "$SAMPLES" "$OUT" "$PROFILE_FILE" <<'PY'
import json, sys
label, duration, samples_path, out, profile_path = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
profile = json.load(open(profile_path))
rows = [line.split() for line in open(samples_path) if line.strip()]
def col(i): return [float(r[i]) for r in rows]
def mean(v): return sum(v) / len(v) if v else 0.0
zones = [[float(t) / 1000 for t in r[6].split(',')] for r in rows]
report = {
    'label': label,
    'seconds': duration,
    'cpuMHz': {'little': mean(col(0)) / 1000, 'bigCluster0': mean(col(1)) / 1000, 'bigCluster1': mean(col(2)) / 1000},
    'loadPercent': {'npu': mean(col(3)), 'gpu': mean(col(4)), 'ddr': mean(col(5))},
    'maxTempC': [max(z[i] for z in zones) for i in range(len(zones[0]))] if zones else [],
    'nodes': profile,
}
with open(out, 'w') as f: json.dump(report, f, indent=2)
print(out)
for node in profile:
    print(f"{node['Kind']:6} {node['Name']}")
    for s in node['Stages']:
        print(f"    {s['Stage']:16} mean {s['MeanMs']:7.2f} ms  p95 {s['P95Ms']:7.2f} ms  n={s['Count']}")
PY
