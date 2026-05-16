from time import perf_counter

from flask import Flask, jsonify, render_template, request

from config import FETCH_PATH, HOST, PORT, SCENARIOS
from device_state import get_status, load_last_known_ip, register_contact
from metrics_store import (
    init_metrics_table,
    insert_metric_event,
    patch_metric_network,
    recent_metrics,
    scenario_summary,
)
from plant_model import classify_with_timing
from storage import init_db, insert_reading, readings_chronological
from wifi_info import get_server_lan_ip, get_wifi_ssid

app = Flask(__name__)


def _client_ip() -> str | None:
    forwarded = request.headers.get("X-Forwarded-For", "").strip()
    if forwarded:
        return forwarded.split(",")[0].strip()
    return request.remote_addr


def _setup_context() -> dict:
    ssid = get_wifi_ssid()
    server_ip = get_server_lan_ip()
    server_url = f"http://{server_ip}:{PORT}{FETCH_PATH}"
    return {
        "wifi_ssid": ssid,
        "wifi_ssid_display": ssid if ssid else "— (not detected)",
        "server_ip": server_ip,
        "server_port": PORT,
        "server_url": server_url,
        "scenarios": SCENARIOS,
        "setup_warning": (
            f'Connect ESP32 to Wi-Fi network "{ssid}". '
            f'Set the Server URL in "esp32.ino" to {server_url} and upload the sketch. '
            f"Set SCENARIO (1–4) for each experiment run."
        )
        if ssid
        else (
            f"Wi-Fi SSID could not be detected. ESP32 and this computer must be on the same network. "
            f'Set the Server URL in "esp32.ino" to {server_url}. Set SCENARIO (1–4) per experiment.'
        ),
    }


def _to_float(value):
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _to_int(value):
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _uses_cloud_decision(scenario: int | None) -> bool:
    return scenario in (1, 3)


def _uses_edge_decision(scenario: int | None) -> bool:
    return scenario in (2, 4)


@app.route("/")
def index():
    return render_template("index.html", **_setup_context())


@app.route("/api/status")
def api_status():
    return jsonify(get_status())


@app.route("/api/readings")
def api_readings():
    limit = request.args.get("limit", default=60, type=int)
    limit = max(10, min(limit, 200))
    return jsonify({"readings": readings_chronological(limit)})


@app.route("/api/metrics")
def api_metrics():
    limit = request.args.get("limit", default=60, type=int)
    limit = max(10, min(limit, 200))
    return jsonify({"metrics": recent_metrics(limit)})


@app.route("/api/comparison")
def api_comparison():
    rows = scenario_summary()
    enriched = []
    for row in rows:
        scn = int(row["scenario"])
        enriched.append({**row, "label": SCENARIOS.get(scn, f"scenario_{scn}")})
    return jsonify({"scenarios": enriched})


@app.route(f"{FETCH_PATH}/metrics", methods=["POST"])
def fetch_metrics():
    if not request.is_json:
        return jsonify({"error": "Payload must be JSON."}), 400

    data = request.get_json(silent=True) or {}
    reading_id = _to_int(data.get("reading_id"))
    if not reading_id:
        return jsonify({"error": "reading_id required."}), 400

    updated = patch_metric_network(
        reading_id=reading_id,
        http_rtt_ms=_to_float(data.get("http_rtt_ms")),
        bytes_sent=_to_int(data.get("bytes_sent")),
        bytes_received=_to_int(data.get("bytes_received")),
        end_to_end_ms=_to_float(data.get("end_to_end_ms")),
    )
    if not updated:
        return jsonify({"error": "No metric row for reading_id."}), 404
    return jsonify({"message": "Network metrics updated.", "reading_id": reading_id}), 200


@app.route(FETCH_PATH, methods=["POST"])
def fetch():
    t_request = perf_counter()

    if not request.is_json:
        return jsonify({"error": "Payload must be JSON."}), 400

    client_ip = _client_ip()
    register_contact(client_ip)

    data = request.get_json(silent=True) or {}
    temp = _to_float(data.get("temp"))
    humidity = _to_float(data.get("hum"))
    light = _to_float(data.get("light"))
    soil = _to_float(data.get("soil"))
    device_ms = _to_int(data.get("device_ms"))
    scenario = _to_int(data.get("scenario"))

    edge_inference_ms = _to_float(data.get("edge_inference_ms"))
    edge_decision = data.get("edge_decision")
    http_rtt_ms = _to_float(data.get("http_rtt_ms"))
    wifi_on_ms = _to_int(data.get("wifi_on_ms"))
    bytes_sent = _to_int(data.get("bytes_sent"))
    bytes_received = _to_int(data.get("bytes_received"))
    end_to_end_ms = _to_float(data.get("end_to_end_ms"))

    cloud_inference_ms: float | None = None
    decision: str | None = None
    decision_source: str | None = None
    reason: str | None = None

    if _uses_cloud_decision(scenario):
        cloud_decision, cloud_inference_ms = classify_with_timing(temp, humidity, light, soil)
        decision = cloud_decision.label
        decision_source = "cloud"
        reason = cloud_decision.reason
    elif _uses_edge_decision(scenario):
        decision = str(edge_decision) if edge_decision else "unknown"
        decision_source = "edge"
    else:
        # Unknown scenario: still classify on cloud for logging
        cloud_decision, cloud_inference_ms = classify_with_timing(temp, humidity, light, soil)
        decision = cloud_decision.label
        decision_source = "cloud"
        reason = cloud_decision.reason

    row_id = insert_reading(
        temp=temp,
        humidity=humidity,
        light=light,
        soil=soil,
        device_ms=device_ms,
        source_ip=client_ip,
        scenario=scenario,
        decision=decision,
        decision_source=decision_source,
        edge_inference_ms=edge_inference_ms,
        cloud_inference_ms=cloud_inference_ms,
    )

    insert_metric_event(
        reading_id=row_id,
        scenario=scenario,
        decision=decision,
        decision_source=decision_source,
        edge_inference_ms=edge_inference_ms,
        cloud_inference_ms=cloud_inference_ms,
        http_rtt_ms=http_rtt_ms,
        wifi_on_ms=wifi_on_ms,
        bytes_sent=bytes_sent,
        bytes_received=bytes_received,
        end_to_end_ms=end_to_end_ms,
    )

    server_total_ms = (perf_counter() - t_request) * 1000.0

    return jsonify(
        {
            "message": "Reading received successfully.",
            "id": row_id,
            "decision": decision,
            "decision_source": decision_source,
            "reason": reason,
            "cloud_inference_ms": cloud_inference_ms,
            "server_total_ms": round(server_total_ms, 3),
            "scenario": scenario,
        }
    ), 200


if __name__ == "__main__":
    init_db()
    init_metrics_table()
    load_last_known_ip()
    app.run(host=HOST, port=PORT, debug=False)
