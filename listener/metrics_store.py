import sqlite3
from datetime import datetime, timezone

from config import DB_PATH

_CREATE_METRICS_SQL = """
CREATE TABLE IF NOT EXISTS metric_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    reading_id INTEGER,
    received_at_utc TEXT NOT NULL,
    scenario INTEGER,
    decision TEXT,
    decision_source TEXT,
    edge_inference_ms REAL,
    cloud_inference_ms REAL,
    http_rtt_ms REAL,
    wifi_on_ms INTEGER,
    bytes_sent INTEGER,
    bytes_received INTEGER,
    end_to_end_ms REAL,
    FOREIGN KEY (reading_id) REFERENCES readings(id)
);
"""


def init_metrics_table() -> None:
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute(_CREATE_METRICS_SQL)
        conn.commit()


def insert_metric_event(
    reading_id: int | None,
    scenario: int | None,
    decision: str | None,
    decision_source: str | None,
    edge_inference_ms: float | None = None,
    cloud_inference_ms: float | None = None,
    http_rtt_ms: float | None = None,
    wifi_on_ms: int | None = None,
    bytes_sent: int | None = None,
    bytes_received: int | None = None,
    end_to_end_ms: float | None = None,
) -> int:
    received_at_utc = datetime.now(timezone.utc).isoformat()
    with sqlite3.connect(DB_PATH) as conn:
        cursor = conn.execute(
            """
            INSERT INTO metric_events (
                reading_id, received_at_utc, scenario, decision, decision_source,
                edge_inference_ms, cloud_inference_ms, http_rtt_ms, wifi_on_ms,
                bytes_sent, bytes_received, end_to_end_ms
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                reading_id,
                received_at_utc,
                scenario,
                decision,
                decision_source,
                edge_inference_ms,
                cloud_inference_ms,
                http_rtt_ms,
                wifi_on_ms,
                bytes_sent,
                bytes_received,
                end_to_end_ms,
            ),
        )
        conn.commit()
        return int(cursor.lastrowid)


def scenario_summary() -> list[dict]:
    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            """
            SELECT
                scenario,
                COUNT(*) AS samples,
                AVG(edge_inference_ms) AS avg_edge_ms,
                AVG(cloud_inference_ms) AS avg_cloud_ms,
                AVG(http_rtt_ms) AS avg_http_rtt_ms,
                AVG(wifi_on_ms) AS avg_wifi_on_ms,
                AVG(bytes_sent) AS avg_bytes_sent,
                AVG(end_to_end_ms) AS avg_e2e_ms
            FROM metric_events
            WHERE scenario IS NOT NULL
            GROUP BY scenario
            ORDER BY scenario
            """
        ).fetchall()
    return [dict(row) for row in rows]


def patch_metric_network(
    reading_id: int,
    http_rtt_ms: float | None,
    bytes_sent: int | None,
    bytes_received: int | None,
    end_to_end_ms: float | None,
) -> bool:
    with sqlite3.connect(DB_PATH) as conn:
        cursor = conn.execute(
            """
            UPDATE metric_events
            SET http_rtt_ms = COALESCE(?, http_rtt_ms),
                bytes_sent = COALESCE(?, bytes_sent),
                bytes_received = COALESCE(?, bytes_received),
                end_to_end_ms = COALESCE(?, end_to_end_ms)
            WHERE reading_id = ?
            """,
            (http_rtt_ms, bytes_sent, bytes_received, end_to_end_ms, reading_id),
        )
        conn.commit()
        return cursor.rowcount > 0


def recent_metrics(limit: int = 60) -> list[dict]:
    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            """
            SELECT id, reading_id, received_at_utc, scenario, decision, decision_source,
                   edge_inference_ms, cloud_inference_ms, http_rtt_ms, wifi_on_ms,
                   bytes_sent, bytes_received, end_to_end_ms
            FROM metric_events
            ORDER BY id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()
    result = [dict(row) for row in rows]
    result.reverse()
    return result
