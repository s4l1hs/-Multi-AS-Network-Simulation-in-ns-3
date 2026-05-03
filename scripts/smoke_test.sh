#!/usr/bin/env bash
# smoke_test.sh — Pre-submission end-to-end smoke test for multi_as_sim.
#
# Runs 7 ordered steps; collects all failures before reporting so every
# broken step is visible in a single run. Exit 0 iff all steps pass.
#
# Usage:
#   bash scripts/smoke_test.sh [--ns3Home PATH]
#
# Optional env var: NS3_HOME

set -uo pipefail   # NOT -e: all steps must run to collect full failure list

# ── ANSI colours ──────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PYTHON_SCRIPT="$REPO_ROOT/scripts/aggregate_results.py"

SMOKE_DIR="/tmp/multi_as_smoke"   # fixed path, wiped each run
LOG_DIR="$SMOKE_DIR/logs"

# ── Argument parsing ──────────────────────────────────────────────────────────
NS3_HOME_ARG=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ns3Home) NS3_HOME_ARG="${2:?--ns3Home requires a value}"; shift 2 ;;
        --help|-h) grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ── NS3_HOME resolution ───────────────────────────────────────────────────────
NS3_HOME="${NS3_HOME_ARG:-${NS3_HOME:-}}"
if [[ -z "$NS3_HOME" ]]; then
    for _cand in \
        "$HOME/ns3-workspace/ns-allinone-3.42/ns-3.42" \
        "$HOME/ns3-workspace/ns-allinone-3.43/ns-3.43" \
        "$HOME/ns-3.42"; do
        if [[ -x "$_cand/ns3" ]]; then NS3_HOME="$_cand"; echo "Auto-detected NS3_HOME=$NS3_HOME"; break; fi
    done
fi
if [[ -z "$NS3_HOME" || ! -x "$NS3_HOME/ns3" ]]; then
    echo -e "${RED}ERROR${NC}: ns3 binary not found. Set NS3_HOME or use --ns3Home PATH." >&2
    exit 1
fi

# ── Workspace setup ───────────────────────────────────────────────────────────
rm -rf "$SMOKE_DIR"
mkdir -p "$LOG_DIR"

# ── Step-tracking helpers ─────────────────────────────────────────────────────
FAILED_STEPS=()   # accumulates "Step N — description" strings

_hdr()  {
    local n="$1" msg="$2"
    printf '\n%s\n' "$(printf '─%.0s' {1..60})"
    echo -e "${BOLD}${CYAN}Step $n:${NC}${BOLD} $msg${NC}"
}
_pass() { local n="$1" msg="$2"; echo -e "  ${GREEN}[PASS]${NC} $msg"; }
_fail() { local n="$1" msg="$2"; echo -e "  ${RED}[FAIL]${NC} $msg"; FAILED_STEPS+=("Step $n — $msg"); }
_info() { echo -e "  ${YELLOW}····${NC}  $1"; }
_skip() { local n="$1" msg="$2"; echo -e "  ${YELLOW}[SKIP]${NC} $msg"; }

# ── Banner ────────────────────────────────────────────────────────────────────
WALL_T0=$(date +%s)
echo ""
echo -e "${BOLD}Multi-AS Smoke Test${NC}  (ns-3: $NS3_HOME)"
echo -e "Smoke dir : $SMOKE_DIR"
echo -e "Logs      : $LOG_DIR"

# ═══════════════════════════════════════════════════════════════════════════════
# Step 1 — Build
# ═══════════════════════════════════════════════════════════════════════════════
_hdr 1 "Build multi_as_sim"
if (cd "$NS3_HOME" && ./ns3 build multi_as_sim) \
        >"$LOG_DIR/1_build.log" 2>&1; then
    _pass 1 "Build successful"
else
    _fail 1 "Compilation failed — see $LOG_DIR/1_build.log"
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Step 2 — Run small failure scenario (20 nodes, balanced, 15 s)
# failureTime=5 keeps failure + convergence well within simTime=15.
# ═══════════════════════════════════════════════════════════════════════════════
_hdr 2 "Run small scenario (20 nodes, balanced, simTime=15s, failureTime=5s)"
SID_SMALL="smoke_20_balanced"
CMD_SMALL="multi_as_sim"
CMD_SMALL+=" --nodes=20 --dist=balanced"
CMD_SMALL+=" --scenarioId=$SID_SMALL --runId=1"
CMD_SMALL+=" --simTime=15 --failureTime=5 --seed=42"
CMD_SMALL+=" --outDir=$SMOKE_DIR"

_info "Command: ./ns3 run \"$CMD_SMALL\""
if (cd "$NS3_HOME" && ./ns3 run "$CMD_SMALL") \
        >"$LOG_DIR/2_run_small.log" 2>&1; then
    _pass 2 "Simulation completed (exit 0)"
else
    _fail 2 "Simulation returned non-zero — see $LOG_DIR/2_run_small.log"
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Step 3 — Verify output files exist and are non-empty
# ═══════════════════════════════════════════════════════════════════════════════
_hdr 3 "Verify output files (CSV + NetAnim XML)"

CSV_SMALL="$SMOKE_DIR/metrics_${SID_SMALL}_run1.csv"
XML_SMALL="$SMOKE_DIR/anim_${SID_SMALL}_run1.xml"
STEP3_OK=true

# CSV check
if [[ ! -f "$CSV_SMALL" ]]; then
    _info "${RED}✗${NC} CSV not found: $CSV_SMALL"
    STEP3_OK=false
elif [[ ! -s "$CSV_SMALL" ]]; then
    _info "${RED}✗${NC} CSV is empty: $CSV_SMALL"
    STEP3_OK=false
else
    NROWS=$(tail -n +2 "$CSV_SMALL" 2>/dev/null | wc -l | tr -d ' ')
    _info "${GREEN}✓${NC} CSV: $NROWS flow row(s)  $(basename "$CSV_SMALL")"
fi

# NetAnim XML check
if [[ ! -f "$XML_SMALL" ]]; then
    _info "${RED}✗${NC} NetAnim XML not found: $XML_SMALL"
    STEP3_OK=false
elif [[ ! -s "$XML_SMALL" ]]; then
    _info "${RED}✗${NC} NetAnim XML is empty: $XML_SMALL"
    STEP3_OK=false
else
    XSIZE=$(wc -c <"$XML_SMALL" | tr -d ' ')
    _info "${GREEN}✓${NC} NetAnim XML: ${XSIZE} bytes  $(basename "$XML_SMALL")"
fi

$STEP3_OK \
    && _pass 3 "All expected output files present and non-empty" \
    || _fail 3 "One or more output files missing or empty"

# ═══════════════════════════════════════════════════════════════════════════════
# Step 4 — Validate CSV header schema
# ═══════════════════════════════════════════════════════════════════════════════
_hdr 4 "Validate CSV header schema"
EXPECTED_HDR="scenarioId,runId,nodeCount,distribution,flowId,srcAs,dstAs,meanDelayMs,throughputMbps,packetLossPct,convergenceSec,simTimeSec"

if [[ ! -f "$CSV_SMALL" ]]; then
    _fail 4 "CSV missing — cannot verify header (Step 3 failed)"
else
    ACTUAL_HDR=$(head -1 "$CSV_SMALL" 2>/dev/null || true)
    if [[ "$ACTUAL_HDR" == "$EXPECTED_HDR" ]]; then
        _pass 4 "CSV header matches expected schema"
        _info "  $ACTUAL_HDR"
    else
        _fail 4 "CSV header mismatch"
        _info "Expected: $EXPECTED_HDR"
        _info "Got:      $ACTUAL_HDR"
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Step 5 — Well-formed XML check via xmllint
# /usr/bin/xmllint ships with macOS (libxml2); skip gracefully if absent.
# ═══════════════════════════════════════════════════════════════════════════════
_hdr 5 "Validate NetAnim XML is well-formed (xmllint)"
if ! command -v xmllint &>/dev/null; then
    _skip 5 "xmllint not found — skipping (install: brew install libxml2)"
elif [[ ! -f "$XML_SMALL" ]]; then
    _fail 5 "NetAnim XML missing — cannot validate (Step 3 failed)"
else
    if xmllint --noout "$XML_SMALL" 2>"$LOG_DIR/5_xmllint.log"; then
        _pass 5 "NetAnim XML is well-formed"
    else
        _fail 5 "NetAnim XML is malformed — see $LOG_DIR/5_xmllint.log"
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Step 6 — Large-scenario crash test (100 nodes, unbalanced, 10 s, no failure)
# noFailure=1 keeps simTime=10 viable (no convergence window needed).
# ═══════════════════════════════════════════════════════════════════════════════
_hdr 6 "Crash test: 100-node unbalanced scenario (simTime=10s, --noFailure)"
SID_LARGE="smoke_100_unbalanced"
CMD_LARGE="multi_as_sim"
CMD_LARGE+=" --nodes=100 --dist=unbalanced"
CMD_LARGE+=" --scenarioId=$SID_LARGE --runId=1"
CMD_LARGE+=" --simTime=10 --noFailure=1 --seed=42"
CMD_LARGE+=" --outDir=$SMOKE_DIR"

_info "Command: ./ns3 run \"$CMD_LARGE\""
if (cd "$NS3_HOME" && ./ns3 run "$CMD_LARGE") \
        >"$LOG_DIR/6_run_large.log" 2>&1; then
    _pass 6 "Large scenario completed without crash"
else
    _fail 6 "Large scenario crashed — see $LOG_DIR/6_run_large.log"
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Step 7 — Aggregate script: run and verify summary_table.md is produced
# ═══════════════════════════════════════════════════════════════════════════════
_hdr 7 "Run aggregate_results.py and verify summary_table.md"

# Locate Python with required packages
PYTHON=""
VENV_PY="$REPO_ROOT/.venv/bin/python3"
if [[ -x "$VENV_PY" ]] && "$VENV_PY" -c "import pandas, matplotlib, numpy" 2>/dev/null; then
    PYTHON="$VENV_PY"
    _info "Using venv: $PYTHON"
elif python3 -c "import pandas, matplotlib, numpy" 2>/dev/null; then
    PYTHON="python3"
    _info "Using system python3: $(which python3)"
fi

if [[ -z "$PYTHON" ]]; then
    _fail 7 "Python + pandas/matplotlib/numpy not found — run: bash scripts/setup_analysis_env.sh"
elif [[ ! -f "$PYTHON_SCRIPT" ]]; then
    _fail 7 "Script not found: $PYTHON_SCRIPT"
else
    if "$PYTHON" "$PYTHON_SCRIPT" --resultsDir "$SMOKE_DIR" \
            >"$LOG_DIR/7_aggregate.log" 2>&1; then
        SUMMARY_MD="$SMOKE_DIR/analysis/summary_table.md"
        if [[ -f "$SUMMARY_MD" && -s "$SUMMARY_MD" ]]; then
            # Count data rows in the markdown table (lines starting with '|' that aren't header/sep)
            DATA_ROWS=$(grep -c '^|[^-]' "$SUMMARY_MD" 2>/dev/null || echo 0)
            _pass 7 "summary_table.md produced ($DATA_ROWS scenario row(s) in table)"
            # Show the actual table for visual confirmation
            grep '^|' "$SUMMARY_MD" | head -10 | while IFS= read -r line; do
                _info "  $line"
            done
        else
            _fail 7 "aggregate_results.py exited 0 but summary_table.md missing or empty"
        fi
    else
        _fail 7 "aggregate_results.py failed — see $LOG_DIR/7_aggregate.log"
    fi
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Final report
# ═══════════════════════════════════════════════════════════════════════════════
WALL_T1=$(date +%s)
ELAPSED=$((WALL_T1 - WALL_T0))

printf '\n%s\n' "$(printf '═%.0s' {1..60})"

if [[ ${#FAILED_STEPS[@]} -eq 0 ]]; then
    echo -e "${BOLD}${GREEN}  ✓  ALL SMOKE TESTS PASSED  (${ELAPSED}s)${NC}"
    echo -e "     Smoke outputs : $SMOKE_DIR"
    echo -e "     Logs          : $LOG_DIR"
    exit 0
else
    echo -e "${BOLD}${RED}  ✗  SMOKE TESTS FAILED — ${#FAILED_STEPS[@]} step(s) did not pass (${ELAPSED}s)${NC}"
    echo ""
    for entry in "${FAILED_STEPS[@]}"; do
        echo -e "  ${RED}✗${NC}  $entry"
    done
    echo ""
    echo -e "  Logs: $LOG_DIR"
    echo -e "  Tip:  cat $LOG_DIR/<step>_*.log  for details"
    exit 1
fi
