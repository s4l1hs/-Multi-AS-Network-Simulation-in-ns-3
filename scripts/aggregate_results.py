#!/usr/bin/env python3
"""
aggregate_results.py — Aggregate MultiAS simulation CSVs and produce
comparison tables + plots.

Setup (one-time, in project root):
    python3 -m venv .venv
    source .venv/bin/activate
    pip install pandas matplotlib

Usage:
    python3 scripts/aggregate_results.py --resultsDir /path/to/results/20241201_120000

Outputs (written to <resultsDir>/analysis/):
    summary_table.csv
    summary_table.md
    delay_vs_nodes_by_distribution.png
    throughput_vs_nodes_by_distribution.png
    packetloss_vs_nodes_by_distribution.png
    convergence_vs_nodes_by_distribution.png
"""

import argparse
import glob
import os
import sys

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")   # non-interactive PNG backend — no display required
import matplotlib.pyplot as plt


# ── Constants ──────────────────────────────────────────────────────────────────

EXPECTED_SCENARIOS = [
    (20,  "balanced"),
    (20,  "unbalanced"),
    (50,  "balanced"),
    (50,  "unbalanced"),
    (100, "balanced"),
    (100, "unbalanced"),
]

NODE_COUNTS = [20, 50, 100]

# (internal column name, y-axis label, output filename)
METRICS = [
    ("meanDelayMs",    "Mean End-to-End Delay (ms)",   "delay_vs_nodes_by_distribution.png"),
    ("throughputMbps", "Mean Throughput (Mbps)",        "throughput_vs_nodes_by_distribution.png"),
    ("packetLossPct",  "Packet Loss (%)",               "packetloss_vs_nodes_by_distribution.png"),
    ("convergenceSec", "BGP Convergence Time (s)",      "convergence_vs_nodes_by_distribution.png"),
]

# Expected CSV columns produced by MetricsCollector::DumpCsv
REQUIRED_COLS = {
    "scenarioId", "runId", "nodeCount", "distribution",
    "meanDelayMs", "throughputMbps", "packetLossPct", "convergenceSec",
}

DIST_STYLE = {
    "balanced":   dict(color="#1f77b4", marker="o", linestyle="-",  label="Balanced"),
    "unbalanced": dict(color="#ff7f0e", marker="s", linestyle="--", label="Unbalanced"),
}


# ── Data loading ───────────────────────────────────────────────────────────────

def load_data(results_dir: str) -> pd.DataFrame:
    """
    Glob all per-flow metrics_*.csv files (exclude *_summary.csv).
    Returns a single concatenated DataFrame.
    """
    pattern = os.path.join(results_dir, "metrics_*.csv")
    candidates = glob.glob(pattern)
    csv_files = [f for f in candidates if not f.endswith("_summary.csv")]

    if not csv_files:
        sys.exit(
            f"ERROR: No per-flow metrics_*.csv files found in:\n  {results_dir}\n"
            "Run run_all_scenarios.sh first, or check --resultsDir."
        )

    frames = []
    skipped = 0
    for path in sorted(csv_files):
        try:
            df = pd.read_csv(path, dtype={"scenarioId": str, "distribution": str})
            missing = REQUIRED_COLS - set(df.columns)
            if missing:
                print(f"  SKIP {os.path.basename(path)}: missing columns {missing}",
                      file=sys.stderr)
                skipped += 1
                continue
            if df.empty:
                print(f"  SKIP {os.path.basename(path)}: empty file", file=sys.stderr)
                skipped += 1
                continue
            frames.append(df)
        except Exception as exc:
            print(f"  SKIP {os.path.basename(path)}: {exc}", file=sys.stderr)
            skipped += 1

    if not frames:
        sys.exit("ERROR: All CSV files failed to load — nothing to analyse.")

    print(f"Loaded {len(frames)} CSV file(s)"
          + (f", skipped {skipped}" if skipped else ""))

    combined = pd.concat(frames, ignore_index=True)
    print(f"Total flow rows: {len(combined):,}")

    # convergenceSec == -1 means failure was disabled → treat as missing
    combined["convergenceSec"] = combined["convergenceSec"].where(
        combined["convergenceSec"] >= 0, other=np.nan
    )

    # Ensure numeric types
    for col in ("nodeCount", "runId"):
        combined[col] = pd.to_numeric(combined[col], errors="coerce")

    return combined


# ── Aggregation ────────────────────────────────────────────────────────────────

def aggregate(df: pd.DataFrame) -> pd.DataFrame:
    """
    Two-level aggregation:
      1. Per-run mean — average metric values across all flows in one run.
      2. Cross-run stats — mean ± std across the repeated runs of each scenario.

    Returns a flat DataFrame with columns:
      nodeCount, distribution,
      <metric>_mean, <metric>_std   for each metric in METRICS.
    """
    metric_cols = [m[0] for m in METRICS]

    # Level 1: collapse flows → one row per (scenario, run)
    per_run = (
        df.groupby(["nodeCount", "distribution", "runId"])[metric_cols]
        .mean()
        .reset_index()
    )

    # Level 2: collapse runs → mean + std per scenario
    agg = (
        per_run.groupby(["nodeCount", "distribution"])[metric_cols]
        .agg(["mean", "std"])
    )
    # Flatten multi-level columns: (metric, stat) → metric_stat
    agg.columns = [f"{col}_{stat}" for col, stat in agg.columns]
    agg = agg.reset_index()

    # std is NaN when there is only one run → replace with 0 for display
    std_cols = [c for c in agg.columns if c.endswith("_std")]
    agg[std_cols] = agg[std_cols].fillna(0.0)

    return agg


# ── CSV output ─────────────────────────────────────────────────────────────────

def write_csv(summary: pd.DataFrame, out_dir: str) -> None:
    path = os.path.join(out_dir, "summary_table.csv")
    summary.to_csv(path, index=False, float_format="%.4f")
    print(f"  {path}")


# ── Markdown table ─────────────────────────────────────────────────────────────

def write_markdown(summary: pd.DataFrame, out_dir: str) -> None:
    def fmt(row, col):
        mean = row.get(f"{col}_mean", np.nan)
        std  = row.get(f"{col}_std",  np.nan)
        if pd.isna(mean):
            return "N/A"
        if pd.isna(std) or std == 0.0:
            return f"{mean:.3f}"
        return f"{mean:.3f} ± {std:.3f}"

    header = (
        "| Nodes | Distribution | "
        "Delay (ms) | Throughput (Mbps) | Loss (%) | Convergence (s) |"
    )
    sep = (
        "|------:|:-------------|"
        "-----------:|------------------:|---------:|----------------:|"
    )

    rows = [header, sep]
    for _, row in summary.sort_values(["nodeCount", "distribution"]).iterrows():
        rows.append(
            f"| {int(row['nodeCount']):5d} "
            f"| {row['distribution']:<12s} "
            f"| {fmt(row, 'meanDelayMs'):>10s} "
            f"| {fmt(row, 'throughputMbps'):>17s} "
            f"| {fmt(row, 'packetLossPct'):>8s} "
            f"| {fmt(row, 'convergenceSec'):>15s} |"
        )

    path = os.path.join(out_dir, "summary_table.md")
    with open(path, "w") as fh:
        fh.write("# MultiAS Network Simulation — Scenario Summary\n\n")
        fh.write("\n".join(rows))
        fh.write("\n\n")
        fh.write("_Values: mean ± std across repeated runs. "
                 "Convergence N/A = failure injection disabled._\n")
    print(f"  {path}")


# ── Plots ──────────────────────────────────────────────────────────────────────

def plot_metric(summary: pd.DataFrame,
                metric_col: str,
                ylabel: str,
                filename: str,
                out_dir: str) -> None:
    mean_col = f"{metric_col}_mean"
    std_col  = f"{metric_col}_std"

    fig, ax = plt.subplots(figsize=(7, 4.5))
    plotted = 0

    for dist, style in DIST_STYLE.items():
        subset = (
            summary[summary["distribution"] == dist]
            .sort_values("nodeCount")
        )
        if subset.empty:
            continue

        xs = subset["nodeCount"].tolist()
        ys = subset[mean_col].tolist()
        es = subset[std_col].tolist()

        # Skip series if every value is missing (e.g. convergence in baseline runs)
        if all(pd.isna(y) for y in ys):
            continue

        # Replace NaN in yerr with 0 so errorbar() does not crash
        es_clean = [0.0 if pd.isna(e) else e for e in es]

        ax.errorbar(
            xs, ys, yerr=es_clean,
            label=style["label"],
            color=style["color"],
            marker=style["marker"],
            linestyle=style["linestyle"],
            linewidth=2,
            markersize=7,
            capsize=5,
            capthick=1.5,
            elinewidth=1.2,
        )
        plotted += 1

    if plotted == 0:
        print(f"  SKIP plot {filename}: no data available")
        plt.close(fig)
        return

    ax.set_xlabel("Number of Nodes", fontsize=12)
    ax.set_ylabel(ylabel, fontsize=12)
    ax.set_xticks(NODE_COUNTS)
    ax.set_xticklabels([str(n) for n in NODE_COUNTS])
    ax.legend(fontsize=11, loc="best")
    ax.grid(axis="y", linestyle="--", alpha=0.45)
    ax.set_title(f"{ylabel}\nvs. Node Count by Distribution", fontsize=12)
    fig.tight_layout()

    path = os.path.join(out_dir, filename)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  {path}")


# ── Validation ─────────────────────────────────────────────────────────────────

def validate(summary: pd.DataFrame) -> bool:
    all_ok = True

    # 1. All 6 expected scenarios present
    found = {
        (int(r["nodeCount"]), str(r["distribution"]))
        for _, r in summary.iterrows()
    }
    missing = [(n, d) for n, d in EXPECTED_SCENARIOS if (n, d) not in found]
    if missing:
        print(f"  FAIL — missing scenarios: {missing}", file=sys.stderr)
        all_ok = False
    else:
        print("  PASS — all 6 expected scenarios present")

    # 2. No NaN in delay / throughput / loss means (convergence may be NaN)
    key_mean_cols = ["meanDelayMs_mean", "throughputMbps_mean", "packetLossPct_mean"]
    for col in key_mean_cols:
        if col not in summary.columns:
            print(f"  FAIL — column {col} missing from summary", file=sys.stderr)
            all_ok = False
            continue
        n_nan = int(summary[col].isna().sum())
        if n_nan:
            print(f"  FAIL — {n_nan} NaN value(s) in {col}", file=sys.stderr)
            all_ok = False
        else:
            print(f"  PASS — {col}: no NaN")

    return all_ok


# ── Entry point ────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--resultsDir", required=True, metavar="DIR",
        help="Timestamped results directory created by run_all_scenarios.sh",
    )
    return p.parse_args()


def main() -> None:
    args = parse_args()
    results_dir = os.path.realpath(args.resultsDir)
    if not os.path.isdir(results_dir):
        sys.exit(f"ERROR: --resultsDir does not exist: {results_dir}")

    out_dir = os.path.join(results_dir, "analysis")
    os.makedirs(out_dir, exist_ok=True)

    print(f"\nResults dir : {results_dir}")
    print(f"Analysis dir: {out_dir}\n")

    # ── Load ──────────────────────────────────────────────────────────────
    df = load_data(results_dir)

    # ── Aggregate ─────────────────────────────────────────────────────────
    summary = aggregate(df)
    print(f"\nScenarios found ({len(summary)}):")
    for _, row in summary.sort_values(["nodeCount", "distribution"]).iterrows():
        n_runs = int(
            df[(df["nodeCount"] == row["nodeCount"]) &
               (df["distribution"] == row["distribution"])]["runId"]
            .nunique()
        )
        print(f"  nodes={int(row['nodeCount']):3d}  dist={row['distribution']:<10s}  runs={n_runs}")

    # ── Write outputs ──────────────────────────────────────────────────────
    print("\nWriting outputs:")
    write_csv(summary, out_dir)
    write_markdown(summary, out_dir)
    for metric_col, ylabel, filename in METRICS:
        plot_metric(summary, metric_col, ylabel, filename, out_dir)

    # ── Validate ───────────────────────────────────────────────────────────
    print("\nValidation:")
    ok = validate(summary)

    print(f"\n{'All checks passed.' if ok else 'Some checks FAILED — see warnings above.'}")
    print(f"Output directory: {out_dir}")


if __name__ == "__main__":
    main()
