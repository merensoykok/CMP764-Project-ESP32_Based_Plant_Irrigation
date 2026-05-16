from datetime import datetime, timezone

from storage import get_last_source_ip

_esp32_ip: str | None = None
_last_seen_utc: str | None = None

ONLINE_THRESHOLD_SEC = 25


def register_contact(ip: str | None) -> None:
    global _esp32_ip, _last_seen_utc
    if not ip:
        return
    _esp32_ip = ip
    _last_seen_utc = datetime.now(timezone.utc).isoformat()


def load_last_known_ip() -> None:
    ip = get_last_source_ip()
    if ip:
        register_contact(ip)


def get_status() -> dict:
    online = False
    if _last_seen_utc:
        last = datetime.fromisoformat(_last_seen_utc)
        age = (datetime.now(timezone.utc) - last).total_seconds()
        online = age <= ONLINE_THRESHOLD_SEC

    url = f"http://{_esp32_ip}/" if _esp32_ip else None
    return {
        "ip": _esp32_ip,
        "url": url,
        "last_seen_utc": _last_seen_utc,
        "online": online,
    }
