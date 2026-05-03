#!/usr/bin/env bash
# setup_analysis_env.sh — Create a Python virtual environment with analysis deps.
#
# Run once from the project root:
#   bash scripts/setup_analysis_env.sh
#
# After setup, activate with:
#   source .venv/bin/activate
#
# Then run the aggregation script:
#   python3 scripts/aggregate_results.py --resultsDir /path/to/results/TIMESTAMP

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV_DIR="$REPO_ROOT/.venv"
REQ_FILE="$REPO_ROOT/requirements-analysis.txt"

echo "Project root : $REPO_ROOT"
echo "Venv dir     : $VENV_DIR"

# ── Python version check ───────────────────────────────────────────────────────
if ! command -v python3 &>/dev/null; then
    echo "ERROR: python3 not found. Install via Homebrew: brew install python@3.11" >&2
    exit 1
fi

PY_VER=$(python3 -c "import sys; print('.'.join(map(str,sys.version_info[:2])))")
echo "Python       : $(command -v python3)  ($PY_VER)"

# ── Create venv if missing ─────────────────────────────────────────────────────
if [[ ! -d "$VENV_DIR" ]]; then
    echo "Creating virtual environment…"
    python3 -m venv "$VENV_DIR"
fi

# ── Upgrade pip and install deps ───────────────────────────────────────────────
echo "Installing/upgrading dependencies…"
"$VENV_DIR/bin/pip" install --upgrade pip --quiet
"$VENV_DIR/bin/pip" install --upgrade -r "$REQ_FILE"

echo ""
echo "Done. To activate:"
echo "  source .venv/bin/activate"
echo ""
echo "To run the analysis:"
echo "  python3 scripts/aggregate_results.py --resultsDir <RESULTS_DIR>"
