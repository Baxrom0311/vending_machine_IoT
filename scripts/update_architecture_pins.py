#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
from typing import Final

START_MARKER: Final[str] = "<!-- PINS_TABLE_START -->"
END_MARKER: Final[str] = "<!-- PINS_TABLE_END -->"


def _parse_defines(path: Path) -> dict[str, str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    defines: dict[str, str] = {}

    i = 0
    while i < len(lines):
        line = lines[i].rstrip()
        if line.lstrip().startswith("#define"):
            full = line
            while full.rstrip().endswith("\\") and i + 1 < len(lines):
                full = full.rstrip()
                full = full[:-1].rstrip() + " " + lines[i + 1].strip()
                i += 1

            no_comment = full.split("//", 1)[0].split("/*", 1)[0].strip()
            parts = no_comment.split()
            if len(parts) >= 3 and parts[0] == "#define":
                name = parts[1]
                value = " ".join(parts[2:]).strip()
                defines[name] = value
        i += 1

    return defines


def _try_parse_int(value: str) -> int | None:
    v = value.strip()
    if v.startswith("(") and v.endswith(")"):
        v = v[1:-1].strip()
    try:
        return int(v, 0)
    except ValueError:
        return None


def _gpio(defines: dict[str, str], macro: str) -> str:
    raw = defines.get(macro)
    if raw is None:
        return "N/A"
    parsed = _try_parse_int(raw)
    return str(parsed) if parsed is not None else raw


def _table(title: str, rows: list[tuple[str, str, str]], defines: dict[str, str]) -> str:
    out: list[str] = []
    out.append(f"### {title}")
    out.append("")
    out.append("| Component | Macro | GPIO | Notes |")
    out.append("| :--- | :--- | :--- | :--- |")
    for component, macro, notes in rows:
        out.append(f"| {component} | `{macro}` | {_gpio(defines, macro)} | {notes} |")
    out.append("")
    return "\n".join(out)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    arch_path = root / "docs" / "ARCHITECTURE.md"
    main_hw = root / "src_esp32_main" / "hardware.h"
    pay_hw = root / "src_esp32_payment" / "hardware.h"

    arch = arch_path.read_text(encoding="utf-8")
    start = arch.find(START_MARKER)
    end = arch.find(END_MARKER)
    if start == -1 or end == -1 or end <= start:
        raise SystemExit(f"Markers not found in {arch_path}")

    main_def = _parse_defines(main_hw)
    pay_def = _parse_defines(pay_hw)

    main_rows = [
        ("Relay (Valve)", "RELAY_PIN", "Output"),
        ("Flow Sensor", "FLOW_SENSOR_PIN", "Input (Interrupt)"),
        ("TDS Sensor", "TDS_PIN", "Analog"),
        ("LCD (I2C) SDA", "I2C_SDA_PIN", "I2C"),
        ("LCD (I2C) SCL", "I2C_SCL_PIN", "I2C"),
        ("Start Button", "START_BUTTON_PIN", "Input (PullUp)"),
        ("Pause Button", "PAUSE_BUTTON_PIN", "Input (PullUp)"),
        ("UART RX (from Payment)", "UART_RX_PIN", "Serial2 RX"),
        ("UART TX (to Payment)", "UART_TX_PIN", "Serial2 TX"),
    ]

    pay_rows = [
        ("Cash Acceptor Pulse", "CASH_PULSE_PIN", "Input (Interrupt)"),
        ("Status LED", "LED_PIN", "Output"),
        ("UART TX (to Main)", "UART_TX_PIN", "Serial2 TX"),
        ("UART RX (from Main)", "UART_RX_PIN", "Serial2 RX"),
    ]

    generated = (
        _table("ESP32 #2 — Main Controller", main_rows, main_def)
        + _table("ESP32 #1 — Payment Controller", pay_rows, pay_def)
    ).rstrip()

    before = arch[: start + len(START_MARKER)]
    after = arch[end:]
    new_arch = before + "\n" + generated + "\n" + after

    if new_arch != arch:
        arch_path.write_text(new_arch, encoding="utf-8")
        print(f"Updated pins section in {arch_path}")
    else:
        print("No changes needed")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

