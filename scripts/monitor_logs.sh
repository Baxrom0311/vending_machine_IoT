#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${1:-esp32_payment}"
PORT="${2:-}"
BAUD="${3:-115200}"

if [[ -z "$PORT" ]]; then
  PORT="$(pio device list | awk '/\/dev\/cu\.usbserial/ {print $1; exit}')"
fi

if [[ -z "$PORT" ]]; then
  echo "USB serial port not found."
  echo "Usage: scripts/monitor_logs.sh [esp32_payment|esp32_main] [/dev/cu.usbserial-XXX] [baud]"
  exit 1
fi

mkdir -p logs
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="logs/${ENV_NAME}-${STAMP}.log"

echo "Monitoring ${ENV_NAME} on ${PORT} at ${BAUD} baud"
echo "Log file: ${LOG_FILE}"
echo "Stop with Ctrl+C"

pio device monitor -e "${ENV_NAME}" --port "${PORT}" --baud "${BAUD}" 2>&1 | tee -a "${LOG_FILE}"
