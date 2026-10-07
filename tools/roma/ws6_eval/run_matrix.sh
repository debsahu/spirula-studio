#!/bin/bash
S=/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/ws6
B=$S/bin/spirula_6bc4bb28
cd $S
run() { # dataset arm matchesdir? args...
  ds=$1; arm=$2; shift 2
  /usr/bin/time -l $B densify $ds --holdout-every 8 --seed 0 --out sparse/0-f-$arm --overwrite "$@" > matrix_${ds}_$arm.log 2>&1
  echo "$ds $arm rc=$?" >> matrix.done
}
M=$S/../ws4/basement_export/matches
run ds roma_nocap --source roma --matches $M --matches-per-ref 10212 --max-points off
run ds moge_nocap --source moge --max-points off
run ds hyb_cap1M --source hybrid --matches $M --max-points 1000000
run ds hyb_nocap --source hybrid --matches $M --matches-per-ref 10212 --max-points off
run ds hyb_auto --source hybrid --matches $M
run ds roma_cap --source roma --matches $M
run dsh moge_cap --source moge
run dsh moge_nocap --source moge --max-points off
run dsh hyb_cap1M --source hybrid --matches $S/dsh_matches --max-points 1000000
run dsh hyb_nocap --source hybrid --matches $S/dsh_matches --matches-per-ref 10212 --max-points off
run dsh hyb_auto --source hybrid --matches $S/dsh_matches
echo ALL > matrix.all
