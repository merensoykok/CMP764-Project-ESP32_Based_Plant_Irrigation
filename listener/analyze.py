"""Print edge vs cloud comparison tables from collected metric_events."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

from config import DATA_DIR, SCENARIOS
from metrics_store import init_metrics_table, scenario_summary
from storage import init_db


def _fmt(value, digits: int = 2) -> str:
    if value is None:
        return "—"
    return f"{float(value):.{digits}f}"


def print_summary() -> None:
    rows = scenario_summary()
    if not rows:
        print("No metric_events yet. Run experiments with esp32.ino scenarios 1–4.")
        return

    print("\nEdge vs Cloud — scenario comparison")
    print("=" * 88)
    header = (
        f"{'Scn':<4} {'Mode':<34} {'N':>5} {'Edge ms':>9} {'Cloud ms':>9} "
        f"{'RTT ms':>8} {'WiFi ms':>9} {'Bytes':>8} {'E2E ms':>8}"
    )
    print(header)
    print("-" * len(header))

    for row in rows:
        scn = int(row["scenario"])
        label = SCENARIOS.get(scn, f"scenario_{scn}")
        print(
            f"{scn:<4} {label:<34} {row['samples']:>5} "
            f"{_fmt(row['avg_edge_ms']):>9} {_fmt(row['avg_cloud_ms']):>9} "
            f"{_fmt(row['avg_http_rtt_ms']):>8} {_fmt(row['avg_wifi_on_ms'], 0):>9} "
            f"{_fmt(row['avg_bytes_sent'], 0):>8} {_fmt(row['avg_e2e_ms']):>8}"
        )

    print("\nInterpretation hints:")
    print("  • Scenarios 1 & 3 use cloud inference; 2 & 4 use on-device rules (tiny ML baseline).")
    print("  • Scenarios 3 & 4 should show lower avg WiFi-on time than 1 & 2.")
    print("  • Edge scenarios should show near-zero cloud_inference_ms.")


def export_csv(path: Path) -> None:
    rows = scenario_summary()
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "scenario",
                "label",
                "samples",
                "avg_edge_ms",
                "avg_cloud_ms",
                "avg_http_rtt_ms",
                "avg_wifi_on_ms",
                "avg_bytes_sent",
                "avg_e2e_ms",
            ],
        )
        writer.writeheader()
        for row in rows:
            scn = int(row["scenario"])
            writer.writerow(
                {
                    "scenario": scn,
                    "label": SCENARIOS.get(scn, ""),
                    **{k: row.get(k) for k in writer.fieldnames if k not in ("scenario", "label")},
                }
            )
    print(f"Exported: {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare edge vs cloud IoT experiment metrics.")
    parser.add_argument(
        "--export",
        type=Path,
        default=DATA_DIR / "scenario_comparison.csv",
        help="Optional CSV export path",
    )
    parser.add_argument("--no-export", action="store_true", help="Skip CSV export")
    args = parser.parse_args()

    init_db()
    init_metrics_table()
    print_summary()
    if not args.no_export:
        export_csv(args.export)


if __name__ == "__main__":
    main()
