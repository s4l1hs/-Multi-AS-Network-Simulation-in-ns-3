#!/usr/bin/env bash
# run_all_scenarios.sh — Orchestrate all 6 MultiAS scenarios × RUN_COUNT runs each.
#
# Usage:
#   bash scripts/run_all_scenarios.sh [OPTIONS]
#
# Options:
#   --parallel N      Run N simulations in parallel via xargs -P (default: 1)
#   --runs N          Number of repeated trials per scenario (default: 3)
#   --simTime S       Simulation duration in seconds (default: 60)
#   --failureTime S   Link failure injection time in seconds (default: 20)
#   --noFailure       Disable failure injection for all runs (baseline mode)
#   --seed BASE       Base RNG seed; run R uses seed BASE+R (default: 1)
#   --ns3Home PATH    Override NS3_HOME
#   --resultsDir DIR  Override results directory (default: auto-timestamped)
#
# Examples:
#   bash scripts/run_all_scenarios.sh
#   bash scripts/run_all_scenarios.sh --parallel 4 --runs 5
#   bash scripts/run_all_scenarios.sh --noFailure --simTime 120
#   NS3_HOME=~/ns-3.42 bash scripts/run_all_scenarios.sh --parallel 6

set -euo pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────
PARALLEL=1
RUN_COUNT=3
SIM_TIME=60
FAILURE_TIME=20
NO_FAILURE=0
SEED_BASE=1
NS3_HOME_ARG=""
RESULTS_DIR_ARG=""

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --parallel)    PARALLEL="${2:?--parallel requires a value}";    shift 2 ;;
        --runs)        RUN_COUNT="${2:?--runs requires a value}";       shift 2 ;;
        --simTime)     SIM_TIME="${2:?--simTime requires a value}";     shift 2 ;;
        --failureTime) FAILURE_TIME="${2:?--failureTime requires a value}"; shift 2 ;;
        --noFailure)   NO_FAILURE=1;                                    shift   ;;
        --seed)        SEED_BASE="${2:?--seed requires a value}";       shift 2 ;;
        --ns3Home)     NS3_HOME_ARG="${2:?--ns3Home requires a value}"; shift 2 ;;
        --resultsDir)  RESULTS_DIR_ARG="${2:?--resultsDir requires a value}"; shift 2 ;;
        --help|-h)
            sed -n '2,20p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) echo "ERROR: Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ── NS3_HOME resolution ───────────────────────────────────────────────────────
NS3_HOME="${NS3_HOME_ARG:-${NS3_HOME:-}}"

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
    echo "  Set NS3_HOME=/path/to/ns-3.X or pass --ns3Home PATH." >&2
    exit 1
fi

# ── Pre-flight: verify the binary is built ────────────────────────────────────
echo "Pre-flight: checking multi_as_sim build …"
(cd "$NS3_HOME" && ./ns3 build multi_as_sim) || {
    echo "ERROR: Build failed — fix compilation errors before running." >&2
    exit 1
}
echo "Build OK."

# ── Results directory ─────────────────────────────────────────────────────────
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RESULTS_DIR="${RESULTS_DIR_ARG:-$NS3_HOME/scratch/multi_as/results/$TIMESTAMP}"
mkdir -p "$RESULTS_DIR/logs"

echo ""
echo "Results will be saved to: $RESULTS_DIR"

# ── Scenario matrix ───────────────────────────────────────────────────────────
SCENARIOS=(
    "20 balanced"
    "20 unbalanced"
    "50 balanced"
    "50 unbalanced"
    "100 balanced"
    "100 unbalanced"
)

# Build the flat job list: "N D R" triples
ALL_JOBS=()
for SCENARIO in "${SCENARIOS[@]}"; do
    read -r N D <<< "$SCENARIO"
    for (( R=1; R<=RUN_COUNT; R++ )); do
        ALL_JOBS+=("$N $D $R")
    done
done

TOTAL="${#ALL_JOBS[@]}"

printf '\n%s\n' "$(printf '=%.0s' {1..70})"
printf 'Scenarios : %d  ×  runs=%d  =  %d total jobs\n' \
    "${#SCENARIOS[@]}" "$RUN_COUNT" "$TOTAL"
printf 'Parallel  : %d\n' "$PARALLEL"
printf 'simTime   : %ss  failureTime: %ss  noFailure: %s\n' \
    "$SIM_TIME" "$FAILURE_TIME" "$NO_FAILURE"
printf 'seed base : %d  (run R uses seed %d+R)\n' "$SEED_BASE" "$SEED_BASE"
printf '%s\n\n' "$(printf '=%.0s' {1..70})"

# ── Per-job runner (exported for xargs -P) ────────────────────────────────────
# Each invocation receives a single "N D R" token from stdin (via xargs -L 1).
# Writes:
#   $RESULTS_DIR/logs/${N}_${D}_run${R}.log
#   $RESULTS_DIR/logs/${N}_${D}_run${R}.timing   (elapsed seconds or "FAILED N")
#   $RESULTS_DIR/metrics_${N}_${D}_run${R}.csv   (written by the simulation)

_run_sim() {
    local TOKEN="$1"
    read -r N D R <<< "$TOKEN"

    local SCENARIO_ID="${N}_${D}"
    local SEED=$(( SEED_BASE + R ))
    local LOG_FILE="$RESULTS_DIR/logs/${SCENARIO_ID}_run${R}.log"
    local TIMING_FILE="$RESULTS_DIR/logs/${SCENARIO_ID}_run${R}.timing"

    local CMD="multi_as_sim"
    CMD+=" --nodes=$N"
    CMD+=" --dist=$D"
    CMD+=" --scenarioId=$SCENARIO_ID"
    CMD+=" --runId=$R"
    CMD+=" --outDir=$RESULTS_DIR"
    CMD+=" --simTime=$SIM_TIME"
    CMD+=" --failureTime=$FAILURE_TIME"
    CMD+=" --seed=$SEED"
    [[ "$NO_FAILURE" == "1" ]] && CMD+=" --noFailure=1"

    printf '[START] %s  run=%d  seed=%d\n' "$SCENARIO_ID" "$R" "$SEED"

    local T0
    T0=$(date +%s)

    local EXIT_CODE=0
    (cd "$NS3_HOME" && ./ns3 run "$CMD") >"$LOG_FILE" 2>&1 || EXIT_CODE=$?

    local T1
    T1=$(date +%s)
    local ELAPSED=$(( T1 - T0 ))

    if [[ $EXIT_CODE -ne 0 ]]; then
        printf '[FAIL]  %s  run=%d  elapsed=%ds  (exit %d)\n' \
            "$SCENARIO_ID" "$R" "$ELAPSED" "$EXIT_CODE"
        echo "FAILED $EXIT_CODE" > "$TIMING_FILE"
        return 0   # don't abort the whole batch
    fi

    echo "$ELAPSED" > "$TIMING_FILE"

    # Verify CSV output (skip header line to count data rows)
    local CSV_FILE="$RESULTS_DIR/metrics_${SCENARIO_ID}_run${R}.csv"
    local FLOW_ROWS=0
    if [[ -f "$CSV_FILE" ]]; then
        FLOW_ROWS=$(tail -n +2 "$CSV_FILE" 2>/dev/null | wc -l | tr -d ' ' || echo 0)
        printf '[DONE]  %s  run=%d  elapsed=%ds  flows=%d\n' \
            "$SCENARIO_ID" "$R" "$ELAPSED" "$FLOW_ROWS"
    else
        printf '[WARN]  %s  run=%d  elapsed=%ds  CSV missing!\n' \
            "$SCENARIO_ID" "$R" "$ELAPSED"
    fi
}

# Export everything the sub-shell needs
export -f _run_sim
export NS3_HOME RESULTS_DIR SIM_TIME FAILURE_TIME NO_FAILURE SEED_BASE

# ── Execute jobs ──────────────────────────────────────────────────────────────
WALL_T0=$(date +%s)

if [[ "$PARALLEL" -le 1 ]]; then
    # Sequential — simple, predictable, easy to debug
    for JOB in "${ALL_JOBS[@]}"; do
        _run_sim "$JOB"
    done
else
    # Parallel — xargs spawns up to PARALLEL child bash processes
    printf '%s\n' "${ALL_JOBS[@]}" \
        | xargs -P "$PARALLEL" -L 1 bash -c '_run_sim "$@"' _
fi

WALL_T1=$(date +%s)
WALL_ELAPSED=$(( WALL_T1 - WALL_T0 ))

# ── Build SUMMARY.md ──────────────────────────────────────────────────────────
SUMMARY="$RESULTS_DIR/SUMMARY.md"

{
    printf '# MultiAS Simulation Summary\n\n'
    printf '**Timestamp**: %s  \n' "$(date '+%Y-%m-%d %H:%M:%S')"
    printf '**NS3_HOME**: `%s`  \n' "$NS3_HOME"
    printf '**Results**: `%s`  \n\n' "$RESULTS_DIR"

    printf '## Parameters\n\n'
    printf '| Parameter    | Value |\n'
    printf '|:-------------|:------|\n'
    printf '| Scenarios    | %d    |\n' "${#SCENARIOS[@]}"
    printf '| Runs each    | %d    |\n' "$RUN_COUNT"
    printf '| Total jobs   | %d    |\n' "$TOTAL"
    printf '| Parallel     | %d    |\n' "$PARALLEL"
    printf '| simTime      | %ss   |\n' "$SIM_TIME"
    printf '| failureTime  | %ss   |\n' "$FAILURE_TIME"
    printf '| noFailure    | %s    |\n' "$NO_FAILURE"
    printf '| seed base    | %d    |\n' "$SEED_BASE"
    printf '| Wall time    | %ds   |\n\n' "$WALL_ELAPSED"

    printf '## Run Results\n\n'
    printf '| Scenario | Run | Seed | Elapsed | Status | Flow rows |\n'
    printf '|:---------|:----|-----:|--------:|:-------|----------:|\n'

    PASS=0; FAIL=0
    for JOB in "${ALL_JOBS[@]}"; do
        read -r N D R <<< "$JOB"
        SCENARIO_ID="${N}_${D}"
        SEED=$(( SEED_BASE + R ))
        TIMING_FILE="$RESULTS_DIR/logs/${SCENARIO_ID}_run${R}.timing"
        CSV_FILE="$RESULTS_DIR/metrics_${SCENARIO_ID}_run${R}.csv"

        local_ELAPSED="–"
        local_STATUS="MISSING"
        local_FLOWS="–"

        if [[ -f "$TIMING_FILE" ]]; then
            local_TIMING="$(cat "$TIMING_FILE")"
            if [[ "$local_TIMING" == FAILED* ]]; then
                local_STATUS="**FAILED**"
                local_ELAPSED="–"
                (( FAIL++ )) || true
            else
                local_ELAPSED="${local_TIMING}s"
                local_STATUS="OK"
                (( PASS++ )) || true
            fi
        else
            (( FAIL++ )) || true
        fi

        if [[ -f "$CSV_FILE" ]]; then
            local_FLOWS=$(tail -n +2 "$CSV_FILE" 2>/dev/null | wc -l | tr -d ' ' || echo "0")
        fi

        printf '| %-16s | %d | %4d | %7s | %-8s | %9s |\n' \
            "$SCENARIO_ID" "$R" "$SEED" "$local_ELAPSED" \
            "$local_STATUS" "$local_FLOWS"
    done

    printf '\n## Totals\n\n'
    printf '- **Passed**: %d / %d\n' "$PASS" "$TOTAL"
    printf '- **Failed**: %d / %d\n' "$FAIL" "$TOTAL"
    printf '- **Wall clock**: %ds\n\n' "$WALL_ELAPSED"

    printf '## Output Files\n\n'
    printf '```\n'
    ls -1 "$RESULTS_DIR" 2>/dev/null | head -60 || true
    printf '```\n'
} > "$SUMMARY"

# ── Final report ──────────────────────────────────────────────────────────────
printf '\n%s\n' "$(printf '=%.0s' {1..70})"
printf 'All %d jobs completed in %ds\n' "$TOTAL" "$WALL_ELAPSED"
printf 'Summary  : %s\n' "$SUMMARY"
printf 'Results  : %s\n' "$RESULTS_DIR"
printf '%s\n' "$(printf '=%.0s' {1..70})"

# Exit non-zero if any job failed
FAIL_COUNT=0
for JOB in "${ALL_JOBS[@]}"; do
    read -r N D R <<< "$JOB"
    SCENARIO_ID="${N}_${D}"
    TIMING_FILE="$RESULTS_DIR/logs/${SCENARIO_ID}_run${R}.timing"
    if [[ ! -f "$TIMING_FILE" ]] || grep -q '^FAILED' "$TIMING_FILE" 2>/dev/null; then
        (( FAIL_COUNT++ )) || true
    fi
done

if [[ $FAIL_COUNT -gt 0 ]]; then
    echo "WARNING: $FAIL_COUNT job(s) failed — check logs in $RESULTS_DIR/logs/" >&2
    exit 1
fi

exit 0
