#!/usr/bin/env bash
# apuapu-mqtt skill - 어푸어푸 팀 MQTT 브로커와 셸에서 대화한다.
#
# mosquitto 클라이언트(mosquitto_pub / mosquitto_sub)가 필요하다:
#   Windows  https://mosquitto.org/download/ 설치 (C:\Program Files\mosquitto)
#   macOS    brew install mosquitto
#   Linux    sudo apt install mosquitto-clients
#
# 브로커 주소가 바뀌면 파일을 고치지 말고 환경변수로 덮어쓴다:
#   export MQTT_HOST=192.168.0.31
#   export MQTT_PORT=1883

set -euo pipefail

MQTT_HOST="${MQTT_HOST:-192.168.0.31}"
MQTT_PORT="${MQTT_PORT:-1883}"
BASE="apuapu/nodes"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 내 보드 이름. 명령마다 다시 치지 않으려고 저장해둔다.
# `./skill.sh name <id>`로 저장하고, 환경변수 MQTT_DEVICE가 우선한다.
CONFIG_FILE="${HOME}/.apuapu-mqtt"

die() { echo "error: $*" >&2; exit 1; }

load_device() {
  if [ -n "${MQTT_DEVICE:-}" ]; then
    echo "$MQTT_DEVICE"
  elif [ -f "$CONFIG_FILE" ]; then
    sed -n 's/^device=//p' "$CONFIG_FILE" | head -1
  fi
}

resolve_device() {
  local id="${1:-}"
  [ -n "$id" ] || id="$(load_device)"
  [ -n "$id" ] || die "보드 이름이 없습니다. 직접 넘기거나 저장하세요: ./skill.sh name hyunjoon"
  echo "$id"
}

# Windows 설치본은 mosquitto를 PATH에 넣지 않고 Git Bash도 그대로 물려받는다.
# 포기하기 전에 흔한 설치 경로를 뒤져본다.
need_clients() {
  if command -v mosquitto_sub >/dev/null 2>&1 && command -v mosquitto_pub >/dev/null 2>&1; then
    return 0
  fi

  local dir
  for dir in \
    "/c/Program Files/mosquitto" \
    "/c/Program Files (x86)/mosquitto" \
    "$HOME/scoop/apps/mosquitto/current" \
    "/opt/homebrew/bin" \
    "/usr/local/bin"
  do
    if [ -x "$dir/mosquitto_sub" ] || [ -x "$dir/mosquitto_sub.exe" ]; then
      PATH="$PATH:$dir"
      export PATH
      return 0
    fi
  done

  die "mosquitto_sub / mosquitto_pub 를 찾을 수 없습니다.
  Windows  https://mosquitto.org/download/ (기본 설치 경로는 자동 탐지됨)
  macOS    brew install mosquitto
  Linux    sudo apt install mosquitto-clients"
}

usage() {
  local saved; saved="$(load_device)"
  cat <<EOF
apuapu-mqtt - 어푸어푸 팀 브로커 ${MQTT_HOST}:${MQTT_PORT}
내 보드: ${saved:-<미설정 - 실행: ./skill.sh name 내이름>}

  ./skill.sh name [ID]          내 보드 이름 저장 (인자 없으면 현재 값 표시)
  ./skill.sh check              브로커에 닿는지 확인
  ./skill.sh devices            지금 온라인인 보드 목록
  ./skill.sh watch [ID]         메시지 전부 보기 (ID 생략 시 내 보드)
  ./skill.sh led [ID] on|off|toggle
  ./skill.sh sensor [ID]        그 보드의 A0 값 스트리밍
  ./skill.sh dashboard          브라우저로 대시보드 열기
  ./skill.sh pub TOPIC PAYLOAD  아무 토픽에나 발행 (탈출구)

ID를 생략하면 저장된 이름을 씁니다. \`./skill.sh name hyunjoon\` 한 번 해두면
그 뒤로는 \`./skill.sh led on\` 만 치면 됩니다. 이 이름은 보드
arduino_secrets.h의 DEVICE_NAME과 같아야 합니다.

토픽 (어푸어푸 팀 전용 - 다른 팀과 겹치지 않음)
  ${BASE}/<id>/led/set      on | off | toggle      (내가 발행)
  ${BASE}/<id>/led/state    on | off               (retained)
  ${BASE}/<id>/sensor/a0    {"raw":2048,"mv":1650}
  ${BASE}/<id>/status       online | offline       (retained)
EOF
}

cmd_check() {
  need_clients
  local topic="${BASE}/_check/$$"
  echo "발행 시도: ${MQTT_HOST}:${MQTT_PORT} ..."
  if mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$topic" -m "hello" 2>/dev/null; then
    echo "OK - 브로커에 연결됨"
  else
    echo "실패 - ${MQTT_HOST}:${MQTT_PORT} 에 닿지 않습니다" >&2
    echo "  - 브로커 노트북과 같은 WiFi에 있나요?" >&2
    echo "  - 브로커 쪽 방화벽이 TCP ${MQTT_PORT}를 막고 있지 않나요?" >&2
    echo "  - 브로커 담당자의 IP가 바뀌었다면: export MQTT_HOST=새주소" >&2
    exit 1
  fi
}

cmd_devices() {
  need_clients
  echo "보고된 보드 (2초 대기, Ctrl-C로 중단):"
  # status는 retained라 구독하자마자 바로 들어온다.
  mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/+/status" -v -W 2 2>/dev/null |
    while read -r topic payload; do
      id="${topic#${BASE}/}"; id="${id%/status}"
      printf '  %-12s %s\n' "$id" "$payload"
    done || true
}

cmd_name() {
  local id="${1:-}"
  if [ -z "$id" ]; then
    local saved; saved="$(load_device)"
    if [ -n "$saved" ]; then
      echo "내 보드: ${saved}"
      [ -n "${MQTT_DEVICE:-}" ] && echo "(환경변수 MQTT_DEVICE 값)"
    else
      echo "저장된 이름이 없습니다 - 실행: ./skill.sh name 내이름"
    fi
    return
  fi

  # 토픽의 한 단계로 들어가므로 MQTT 와일드카드나 구분자가 있으면 안 된다.
  case "$id" in
    */*|*'#'*|*'+'*|*' '*) die "보드 이름에 / # + 공백이 들어가면 안 됩니다" ;;
  esac

  printf 'device=%s\n' "$id" > "$CONFIG_FILE"
  echo "저장됨: ${id}  (${CONFIG_FILE})"
  echo "보드 arduino_secrets.h의 DEVICE_NAME과 같아야 합니다"
}

cmd_watch() {
  need_clients
  local id="${1:-}"
  [ -n "$id" ] || id="$(load_device)"
  [ -n "$id" ] || id="+"          # 저장된 게 없으면 전체 감시
  echo "감시 중: ${BASE}/${id}/#  (Ctrl-C로 중단)"
  mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/${id}/#" -v
}

cmd_led() {
  need_clients
  local id state
  # `led on` (저장된 보드) 과 `led hyunjoon on` 둘 다 받는다.
  case "${1:-}" in
    on|off|toggle) id="$(resolve_device)"; state="$1" ;;
    *)             id="$(resolve_device "${1:-}")"; state="${2:-}" ;;
  esac
  [ -n "$state" ] || die "사용법: ./skill.sh led [ID] on|off|toggle"
  case "$state" in
    on|off|toggle) ;;
    *) die "상태는 on, off, toggle 중 하나여야 합니다" ;;
  esac
  mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/${id}/led/set" -m "$state"
  echo "'${state}' 를 ${id} 에게 보냄"
}

cmd_sensor() {
  need_clients
  local id; id="$(resolve_device "${1:-}")"
  echo "스트리밍: ${BASE}/${id}/sensor/a0  (Ctrl-C로 중단)"
  mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "${BASE}/${id}/sensor/a0"
}

cmd_dashboard() {
  local page="${SCRIPT_DIR}/web/dashboard.html"
  [ -f "$page" ] || die "대시보드 파일이 없습니다: $page"

  # Git Bash 경로(/c/...)는 브라우저가 모른다. cygpath -m 이 C:/... 형태를 준다.
  local fs_path="$page"
  if command -v cygpath >/dev/null 2>&1; then
    fs_path="$(cygpath -m "$page")"
  fi

  # 경로에 공백이 있으면 file: URL이 거기서 잘린다. 쿼리(?host=)를 윈도우
  # 파일 경로에 그대로 붙이면 Start-Process가 파일명으로 읽고 실패한다.
  # 그래서 항상 제대로 된 URL로 만들어 넘긴다.
  local encoded="${fs_path// /%20}"
  local url="file:///${encoded#/}?host=${MQTT_HOST}"
  echo "여는 중: $url"

  if command -v cygpath >/dev/null 2>&1; then
    powershell.exe -NoProfile -Command "Start-Process '$url'"
  elif command -v open >/dev/null 2>&1; then
    open "$url"
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$url"
  else
    echo "브라우저를 자동으로 못 열었습니다. 직접 여세요:"
    echo "  $page"
  fi
}

cmd_pub() {
  need_clients
  local topic="${1:-}" payload="${2:-}"
  [ -n "$topic" ] || die "사용법: ./skill.sh pub TOPIC PAYLOAD"
  mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$topic" -m "$payload"
  echo "발행됨: ${topic}"
}

case "${1:-help}" in
  name)      shift; cmd_name "$@" ;;
  check)     shift; cmd_check "$@" ;;
  devices)   shift; cmd_devices "$@" ;;
  watch)     shift; cmd_watch "$@" ;;
  led)       shift; cmd_led "$@" ;;
  sensor)    shift; cmd_sensor "$@" ;;
  dashboard) shift; cmd_dashboard "$@" ;;
  pub)       shift; cmd_pub "$@" ;;
  help|-h|--help) usage ;;
  *) usage; exit 1 ;;
esac
