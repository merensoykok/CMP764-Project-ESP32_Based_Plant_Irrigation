import sqlite3
from datetime import datetime, timezone

from config import DATA_DIR, DB_PATH

_CREATE_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    received_at_utc TEXT NOT NULL,
    temp REAL,
    humidity REAL,
    light REAL,
    soil REAL,
    device_ms INTEGER,
    scenario INTEGER,
    decision TEXT,
    decision_source TEXT,
    edge_inference_ms REAL,
    cloud_inference_ms REAL
);
"""


def init_db() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute(_CREATE_TABLE_SQL)
        cols = {row[1] for row in conn.execute("PRAGMA table_info(readings)").fetchall()}
        migrations = {
            "source_ip": "ALTER TABLE readings ADD COLUMN source_ip TEXT",
            "scenario": "ALTER TABLE readings ADD COLUMN scenario INTEGER",
            "decision": "ALTER TABLE readings ADD COLUMN decision TEXT",
            "decision_source": "ALTER TABLE readings ADD COLUMN decision_source TEXT",
            "edge_inference_ms": "ALTER TABLE readings ADD COLUMN edge_inference_ms REAL",
            "cloud_inference_ms": "ALTER TABLE readings ADD COLUMN cloud_inference_ms REAL",
        }
        for col, sql in migrations.items():
            if col not in cols:
                conn.execute(sql)
        conn.commit()


def insert_reading(
    temp: float | None,
    humidity: float | None,
    light: float | None,
    soil: float | None,
    device_ms: int | None = None,
    source_ip: str | None = None,
    scenario: int | None = None,
    decision: str | None = None,
    decision_source: str | None = None,
    edge_inference_ms: float | None = None,
    cloud_inference_ms: float | None = None,
) -> int:
    received_at_utc = datetime.now(timezone.utc).isoformat()
    with sqlite3.connect(DB_PATH) as conn:
        cursor = conn.execute(
            """
            INSERT INTO readings (
                received_at_utc, temp, humidity, light, soil, device_ms, source_ip,
                scenario, decision, decision_source, edge_inference_ms, cloud_inference_ms
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                received_at_utc,
                temp,
                humidity,
                light,
                soil,
                device_ms,
                source_ip,
                scenario,
                decision,
                decision_source,
                edge_inference_ms,
                cloud_inference_ms,
            ),
        )
        conn.commit()
        return int(cursor.lastrowid)


def get_last_source_ip() -> str | None:
    with sqlite3.connect(DB_PATH) as conn:
        row = conn.execute(
            """
            SELECT source_ip FROM readings
            WHERE source_ip IS NOT NULL AND source_ip != ''
            ORDER BY id DESC
            LIMIT 1
            """
        ).fetchone()
    return row[0] if row else None


def recent_readings(limit: int = 50) -> list[dict]:
    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            """
            SELECT id, received_at_utc, temp, humidity, light, soil, device_ms, source_ip,
                   scenario, decision, decision_source, edge_inference_ms, cloud_inference_ms
            FROM readings
            ORDER BY id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()
    return [dict(row) for row in rows]


def readings_chronological(limit: int = 60) -> list[dict]:
    rows = recent_readings(limit)
    rows.reverse()
    return rows
