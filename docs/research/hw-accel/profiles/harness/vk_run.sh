#!/bin/bash
# usage: vk_run.sh LABELPREFIX OUTFILE [WORKLOADS...]   (default: V W B, two rounds)
cd /tmp
P=$1; OUT=$2; shift 2
LIST="${@:-V W B}"
rm -f $OUT
for round in 1 2; do
  for w in $LIST; do
    python3 /tmp/ab_env.py $w $P-$w >> $OUT 2>> /tmp/vk_err.txt
  done
done
echo DONE >> $OUT
