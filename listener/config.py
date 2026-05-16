from pathlib import Path

HOST = "0.0.0.0"
PORT = 8741

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
DB_PATH = DATA_DIR / "readings.db"

FETCH_PATH = "/fetch"

# Scenario labels (must match SCENARIO in esp32.ino)
SCENARIOS = {
    1: "Wi-Fi always on + cloud decision",
    2: "Wi-Fi always on + edge (tiny) decision",
    3: "Wi-Fi duty-cycle + cloud decision",
    4: "Wi-Fi duty-cycle + edge (tiny) decision",
}
