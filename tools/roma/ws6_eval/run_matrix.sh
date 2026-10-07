#!/bin/bash
# usage: run_matrix.sh --bin BIN --work DIR --ds DS --dsh DSH --matches DIR --dsh-matches DIR [--seed 0]
# The WS-6 arm matrix on two datasets: DS (full anchors) and DSH (half of the sparse points withheld, see make_half.py).
# Outputs DATASET/sparse/0-f-<arm>; logs <work>/matrix_<ds|dsh>_<arm>.log; one line per arm in <work>/matrix.done; <work>/matrix.all at the end.
# --matches / --dsh-matches are the native or dumped RoMa matches for each dataset's exported views.
BIN= WORK= DS= DSH= M= MH= SEED=0
while [ $# -gt 0 ]; do
  case $1 in
    --bin) BIN=$2; shift 2;; --work) WORK=$2; shift 2;; --ds) DS=$2; shift 2;; --dsh) DSH=$2; shift 2;;
    --matches) M=$2; shift 2;; --dsh-matches) MH=$2; shift 2;; --seed) SEED=$2; shift 2;;
    -h|--help) sed -n '2,5p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2;;
    *) echo "error: unknown argument $1" >&2; exit 2;;
  esac
done
[ -x "$BIN" ] || { echo "error: binary not found or not executable: '$BIN' (set with --bin)" >&2; exit 2; }
[ -d "$WORK" ] || { echo "error: work dir not found: '$WORK' (set with --work)" >&2; exit 2; }
for pair in "DS:$DS:--ds" "DSH:$DSH:--dsh"; do IFS=: read -r n p f <<< "$pair"; [ -d "$p/sparse" ] || { echo "error: dataset has no sparse/ dir: '$p' (set with $f)" >&2; exit 2; }; done
[ -d "$M" ]  || { echo "error: matches dir not found: '$M' (set with --matches)" >&2; exit 2; }
[ -d "$MH" ] || { echo "error: matches dir not found: '$MH' (set with --dsh-matches)" >&2; exit 2; }
run() { # label dataset arm args...
  lbl=$1; ds=$2; arm=$3; shift 3
  /usr/bin/time -l "$BIN" densify "$ds" --holdout-every 8 --seed "$SEED" --out "sparse/0-f-$arm" --overwrite "$@" > "$WORK/matrix_${lbl}_$arm.log" 2>&1
  echo "$lbl $arm rc=$?" >> "$WORK/matrix.done"
}
run ds  "$DS"  roma_nocap --source roma --matches "$M" --matches-per-ref 10212 --max-points off
run ds  "$DS"  moge_nocap --source moge --max-points off
run ds  "$DS"  hyb_cap1M  --source hybrid --matches "$M" --max-points 1000000
run ds  "$DS"  hyb_nocap  --source hybrid --matches "$M" --matches-per-ref 10212 --max-points off
run ds  "$DS"  hyb_auto   --source hybrid --matches "$M"
run ds  "$DS"  roma_cap   --source roma --matches "$M"
run dsh "$DSH" moge_cap   --source moge
run dsh "$DSH" moge_nocap --source moge --max-points off
run dsh "$DSH" hyb_cap1M  --source hybrid --matches "$MH" --max-points 1000000
run dsh "$DSH" hyb_nocap  --source hybrid --matches "$MH" --matches-per-ref 10212 --max-points off
run dsh "$DSH" hyb_auto   --source hybrid --matches "$MH"
echo ALL > "$WORK/matrix.all"
