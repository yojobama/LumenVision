#!/bin/bash
# Sends .rknn variants to the board and runs speed (rknn_perf, core 0, 300 runs) and accuracy (rknn_eval over eval.raw) for each, then scores
# the detections in WSL. Run from WSL: rknn_board_run.sh OUT_DIR DET_DIR NAME...
# Appends one JSON line per variant to DET_DIR/results.jsonl. PI_SSH is a command that runs its arguments on the board with stdin passed through
# (e.g. a wrapper script around ssh or plink); large files go through it, so prefer it over scp from a WSL path.
set -u
out=$1; det=$2; shift 2
pi() { "${PI_SSH:?set PI_SSH to a command that runs its arguments on the board}" "$@"; }
here=$(dirname "$(readlink -f "$0")")
mkdir -p "$det"
for name in "$@"; do
  [ -f "$out/$name.rknn" ] || { echo "{\"variant\": \"$name\", \"error\": \"no model\"}"; continue; }
  pi "cat > /home/lumen/rknn-audit/variant.rknn" < "$out/$name.rknn"
  perf=$(pi "/home/lumen/tools/rknn_perf /home/lumen/rknn-audit/variant.rknn 300 0 set 2>&1 | tail -1")
  pi "cd /home/lumen/rknn-audit && ./rknn_eval variant.rknn eval.raw 4500 det.txt 2>&1 | tail -1 && cat det.txt" > "$det/$name.full"
  tr -d '\r' < "$det/$name.full" | tail -n +2 > "$det/$name.txt"
  summary=$(head -1 "$det/$name.full" | tr -d '\r')
  perf=$(echo "$perf" | tr -d '\r')
  rm -f "$det/$name.full"
  score=$(~/rknn-env/bin/python -I "$here/rknn_eval_score.py" score "$det/$name.txt" ~/rknn-work/prep ~/rknn-work/coco 2>/dev/null | tail -1)
  wall=$(echo "$perf" | sed -n 's/.*wall_ms=\([0-9.]*\).*/\1/p'); npu=$(echo "$perf" | sed -n 's/.*npu_run_ms=\([0-9.]*\).*/\1/p')
  line="{\"variant\": \"$name\", \"bytes\": $(stat -c %s "$out/$name.rknn"), \"wall_ms\": ${wall:-null}, \"npu_ms\": ${npu:-null}, \"eval\": \"$summary\", \"score\": ${score:-null}}"
  echo "$line" | tee -a "$det/results.jsonl"
done
