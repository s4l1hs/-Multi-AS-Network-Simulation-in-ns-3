#!/usr/bin/env bash
# run_one_scenario.sh — Run a single MultiAS simulation for debugging.
#
# Usage:
#   run_one_scenario.sh N DIST [RUN_ID [SIM_TIME [FAILURE_TIME [SEED]]]]
#
# Examples:
#   bash scripts/run_one_scenario.sh 20 balanced
#   bash scripts/run_one_scenario.sh 50 unbalanced 2 30 10
#   NS3_HOME=/path/to/ns-3.42 bash scripts/run_one_scenario.sh 100 balanced
#
# Optional env vars: NS3_HOME, RESULTS_DIR, NO_FAILURE (0|1)

set -euo pipefail

# ── Arguments ─────────────────────────────────────────────────────────────────
N="${1:?Usage: run_one_scenario.sh N DIST [RUN_ID [SIM_TIME [FAILURE_TIME [SEED]]]]}"
D="${2:?DIST (balanced|unbalanced) required}"
R="${3:-1}"
SIM_TIME="${4:-60}"
FAILURE_TIME="${5:-20}"
SEED="${6:-1}"
NO_FAILURE="${NO_FAILURE:-0}"

if [[ "$D" != "balanced" && "$D" != "unbalanced" ]]; then
    echo "ERROR: DIST must be 'balanced' or 'unbalanced', got: '$D'" >&2
    exit 1
fi

# ── NS3_HOME detection ────────────────────────────────────────────────────────
NS3_HOME="${NS3_HOME:-}"
if [[ -z "$NS3_HOME" ]]; then
    for _cand in \
        "$HOME/ns3-workspace/ns-allinone-3.42/ns-3.42" \
        "$HOME/ns3-workspace/ns-allinone-3.43/ns-3.43" \
        "$HOME/ns-3.42"; do
        if [[ -x "$_cand/ns3" ]]; then
            NS3_HOME="$_cand"
            echo "Auto-detected NS3_HOME=$NS3_HOME"
            break
        fi
    done
fi

if [[ -z "$NS3_HOME" || ! -x "$NS3_HOME/ns3" ]]; then
    echo "ERROR: NS3_HOME not set or ns3 binary not found." >&2
    echo "  Set NS3_HOME=/path/to/ns-3.X or run the setup script first." >&2
    exit 1
fi

# ── Output directory ──────────────────────────────────────────────────────────
RESULTS_DIR="${RESULTS_DIR:-$NS3_HOME/scratch/multi_as/results/debug}"
mkdir -p "$RESULTS_DIR/logs"

SCENARIO_ID="${N}_${D}"
LOG_FILE="$RESULTS_DIR/logs/${SCENARIO_ID}_run${R}.log"

# ── Build command ─────────────────────────────────────────────────────────────
CMD="multi_as_sim"
CMD+=" --nodes=$N"
CMD+=" --dist=$D"
CMD+=" --scenarioId=$SCENARIO_ID"
CMD+=" --runId=$R"
CMD+=" --outDir=$RESULTS_DIR"
CMD+=" --simTime=$SIM_TIME"
CMD+=" --failureTime=$FAILURE_TIME"
CMD+=" --seed=$SEED"
[[ "$NO_FAILURE" == "1" ]] && CMD+=" --noFailure=1"

printf '\n%s\n' "$(printf '=%.0s' {1..60})"
printf 'Scenario : %s  run=%d\n' "$SCENARIO_ID" "$R"
printf 'Command  : ./ns3 run "%s"\n' "$CMD"
printf 'Log      : %s\n' "$LOG_FILE"
printf 'NS3_HOME : %s\n' "$NS3_HOME"
printf '%s\n\n' "$(printf '=%.0s' {1..60})"

# ── Run ───────────────────────────────────────────────────────────────────────
T0=$(date +%s)

# Tee so output is visible AND saved.
(cd "$NS3_HOME" && ./ns3 run "$CMD") 2>&1 | tee "$LOG_FILE"

T1=$(date +%s)
ELAPSED=$((T1 - T0))

printf '\nCompleted in %ds\n' "$ELAPSED"

# ── Verify outputs ────────────────────────────────────────────────────────────
STATUS=0
CSV_FILE="$RESULTS_DIR/metrics_${SCENARIO_ID}_run${R}.csv"
if [[ -f "$CSV_FILE" ]]; then
    # Count data rows (skip header line)
    FLOW_ROWS=$(tail -n +2 "$CSV_FILE" 2>/dev/null | wc -l | tr -d ' ' || echo 0)
    printf 'CSV      : %s (%d flow rows)\n' "$CSV_FILE" "$FLOW_ROWS"
else
    printf 'WARNING  : CSV not found: %s\n' "$CSV_FILE" >&2
    STATUS=1
fi

ANIM_FILE="$RESULTS_DIR/anim_${SCENARIO_ID}_run${R}.xml"
if [[ -f "$ANIM_FILE" ]]; then
    printf 'NetAnim  : %s\n' "$ANIM_FILE"
else
    printf 'WARNING  : NetAnim XML not found: %s\n' "$ANIM_FILE" >&2
fi

exit $STATUS
