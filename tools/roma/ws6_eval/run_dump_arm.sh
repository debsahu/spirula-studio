#!/bin/bash
# usage: run_dump_arm.sh --bin BIN --ds DATASET --matches DIR --work DIR [--seeds "0 1 2"]
# Densify DATASET from precomputed (torch dump) matches, once per seed, into DATASET/sparse/0-roma-dump-s<seed>.
# Logs go to <work>/dump_s<seed>.log; <work>/dump_arm.done is written at the end.
BIN= DS= MATCHES= WORK= SEEDS="0 1 2"
while [ $# -gt 0 ]; do
  case $1 in
    --bin) BIN=$2; shift 2;; --ds) DS=$2; shift 2;; --matches) MATCHES=$2; shift 2;;
    --work) WORK=$2; shift 2;; --seeds) SEEDS=$2; shift 2;;
    -h|--help) sed -n '2,4p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2;;
    *) echo "error: unknown argument $1" >&2; exit 2;;
  esac
done
[ -x "$BIN" ]      || { echo "error: binary not found or not executable: '$BIN' (set with --bin)" >&2; exit 2; }
[ -d "$DS/sparse" ] || { echo "error: dataset has no sparse/ dir: '$DS' (set with --ds)" >&2; exit 2; }
[ -d "$MATCHES" ]  || { echo "error: matches dir not found: '$MATCHES' (set with --matches)" >&2; exit 2; }
[ -d "$WORK" ]     || { echo "error: work dir not found: '$WORK' (set with --work)" >&2; exit 2; }
for seed in $SEEDS; do
  /usr/bin/time -l "$BIN" densify "$DS" --holdout-every 8 --matches "$MATCHES" --seed "$seed" --out "sparse/0-roma-dump-s$seed" --overwrite > "$WORK/dump_s$seed.log" 2>&1
done
echo DONE > "$WORK/dump_arm.done"
