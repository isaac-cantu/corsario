#!/bin/bash
# Convert a directory of CORSIKA DAT* files to CSV in parallel, one file per
# SLURM array task. Parallelizing across *files* (not across sub-blocks of
# one file) is deliberate: corsario's per-file parse is already O(file size)
# and single-pass, so for a batch of many files the bottleneck is shared
# filesystem I/O, not per-file CPU time.
#
# Usage:
#   1. Edit DATA_DIR / OUT_DIR / the #SBATCH --array range below to match
#      your number of files (ls "$DATA_DIR"/DAT* | wc -l).
#   2. sbatch scripts/slurm_convert_array.sh
#
#SBATCH --job-name=corsario_convert
#SBATCH --array=0-99            # <-- set to (number_of_files - 1)
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:30:00
#SBATCH --output=logs/corsario_convert_%A_%a.out

set -euo pipefail

DATA_DIR="/path/to/corsika/output"     # <-- contains DATnnnnnn files
OUT_DIR="/path/to/processed"           # <-- CSVs are written here
VENV_ACTIVATE="/path/to/.venv/bin/activate"   # <-- created via: pip install ".[hdf5]"

mkdir -p "$OUT_DIR" logs
source "$VENV_ACTIVATE"

mapfile -t FILES < <(find "$DATA_DIR" -maxdepth 1 -type f -name 'DAT*' | sort)
FILE="${FILES[$SLURM_ARRAY_TASK_ID]}"
NAME="$(basename "$FILE")"

echo "[$SLURM_ARRAY_TASK_ID] converting $FILE"
corsario-inspect "$FILE" || echo "  (inspect reported an issue, continuing with --tolerant)"

corsario-convert "$FILE" "$OUT_DIR/${NAME}.particles.csv" --tolerant
corsario-convert "$FILE" "$OUT_DIR/${NAME}.shower.csv" --kind showers --tolerant

echo "[$SLURM_ARRAY_TASK_ID] done"
