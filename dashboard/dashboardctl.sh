#!/usr/bin/env bash
#
# BME680 dashboard — background server control.
#
#   ./dashboardctl.sh start     start server detached (background)
#   ./dashboardctl.sh stop      stop the running server
#   ./dashboardctl.sh restart   stop then start
#   ./dashboardctl.sh status    is it running? on which port?
#   ./dashboardctl.sh logs      tail the server log
#
# Env overrides (also honoured by server.py):
#   BME680_PORT, BME680_BAUD, BME680_HTTP_HOST, BME680_HTTP_PORT
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="$HERE/.server.pid"
LOG_FILE="$HERE/.server.log"
HTTP_PORT="${BME680_HTTP_PORT:-8080}"

is_running() { [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; }

start() {
  if is_running; then
    echo "already running (pid $(cat "$PID_FILE")) → http://127.0.0.1:$HTTP_PORT"
    exit 0
  fi
  echo "starting BME680 dashboard…"
  # nohup + detach so it survives the terminal / this session
  nohup uv run --with pyserial python "$HERE/server.py" > "$LOG_FILE" 2>&1 &
  echo $! > "$PID_FILE"
  sleep 1
  if is_running; then
    echo "started (pid $(cat "$PID_FILE")) → http://127.0.0.1:$HTTP_PORT"
    echo "log: $LOG_FILE"
  else
    echo "FAILED to start — last log lines:"; tail -n 20 "$LOG_FILE" || true
    rm -f "$PID_FILE"; exit 1
  fi
}

stop() {
  if ! is_running; then
    echo "not running"; rm -f "$PID_FILE"; exit 0
  fi
  local pid; pid="$(cat "$PID_FILE")"
  echo "stopping (pid $pid)…"
  kill "$pid" 2>/dev/null || true
  for _ in $(seq 1 20); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
  kill -9 "$pid" 2>/dev/null || true
  rm -f "$PID_FILE"
  echo "stopped"
}

status() {
  if is_running; then
    echo "running (pid $(cat "$PID_FILE")) → http://127.0.0.1:$HTTP_PORT"
  else
    echo "stopped"
  fi
}

case "${1:-}" in
  start)   start ;;
  stop)    stop ;;
  restart) stop || true; start ;;
  status)  status ;;
  logs)    tail -n 40 -f "$LOG_FILE" ;;
  *) echo "usage: $0 {start|stop|restart|status|logs}"; exit 1 ;;
esac
