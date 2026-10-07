#!/bin/bash
S=/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/ws6
B=$S/bin/spirula_708a7699
for seed in 0 1 2; do
  /usr/bin/time -l $B densify $S/ds --holdout-every 8 --matches /private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/ws6/../ws4/basement_export/matches --seed $seed --out sparse/0-roma-dump-s$seed --overwrite > $S/dump_s$seed.log 2>&1
done
echo DONE > $S/dump_arm.done
