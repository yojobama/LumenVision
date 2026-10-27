#!/bin/bash
# Speed-only A/B of .rknn models on the board: rknn_perf (core 0, 500 runs) on each model in turn, ROUNDS times, alternating so drift hits all
# equally. Run from WSL: rknn_speed_ab.sh ROUNDS MODEL.rknn...   Prints one line per run (model, wall_ms, npu_run_ms).
# PI_SSH is a command that runs its arguments on the board with stdin passed through (see rknn_board_run.sh).
set -u
rounds=$1; shift
pi() { "${PI_SSH:?set PI_SSH to a command that runs its arguments on the board}" "$@"; }
pi "mkdir -p /home/lumen/rknn-audit/ab"
for m in "$@"; do pi "cat > /home/lumen/rknn-audit/ab/$(basename "$m")" < "$m"; done
names=""; for m in "$@"; do names="$names $(basename "$m")"; done
pi "cd /home/lumen/rknn-audit/ab && for r in \$(seq $rounds); do for n in $names; do /home/lumen/tools/rknn_perf \$n 500 0 set | tail -1; done; done" | tr -d '\r'
