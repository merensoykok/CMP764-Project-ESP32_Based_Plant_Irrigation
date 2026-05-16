import re
import socket
import subprocess
import sys


def get_wifi_ssid() -> str | None:
    if sys.platform == "win32":
        return _ssid_windows()
    if sys.platform == "darwin":
        return _ssid_macos()
    return _ssid_linux()


def _ssid_windows() -> str | None:
    try:
        result = subprocess.run(
            ["netsh", "wlan", "show", "interfaces"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
            check=False,
        )
        for line in result.stdout.splitlines():
            stripped = line.strip()
            if stripped.startswith("SSID") and not stripped.startswith("BSSID"):
                parts = stripped.split(":", 1)
                if len(parts) == 2:
                    value = parts[1].strip()
                    if value:
                        return value
    except OSError:
        pass
    return None


def _ssid_macos() -> str | None:
    try:
        result = subprocess.run(
            ["/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport", "-I"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
            check=False,
        )
        match = re.search(r"^\s*SSID:\s*(.+)$", result.stdout, re.MULTILINE)
        if match:
            return match.group(1).strip()
    except OSError:
        pass
    return None


def _ssid_linux() -> str | None:
    try:
        result = subprocess.run(
            ["nmcli", "-t", "-f", "active,ssid", "dev", "wifi"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
            check=False,
        )
        for line in result.stdout.splitlines():
            if line.startswith("yes:"):
                ssid = line[4:].strip()
                if ssid:
                    return ssid
    except OSError:
        pass
    return None


def get_server_lan_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
