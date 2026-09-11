#!/bin/bash
# Local build/dev helper: creates a venv (if missing), installs corsario in
# editable mode with dev+hdf5 extras, and runs the test suite.
set -euo pipefail
cd "$(dirname "$0")/.."

VENV=".venv"
if [ ! -d "$VENV" ]; then
    python3 -m venv "$VENV"
fi
source "$VENV/bin/activate"

pip install --upgrade pip
pip install -e ".[dev,hdf5]"

pytest -v

echo
echo "Build OK. Activate with: source $VENV/bin/activate"
