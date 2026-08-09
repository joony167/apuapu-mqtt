---
name: apuapu-mqtt
description: 어푸어푸 팀 Mosquitto 브로커에 붙어 XIAO ESP32C6 보드의 LED를 제어하고 A0 센서값을 읽는다. 팀 보드 목록 확인, LED on/off, 센서 모니터링, 대시보드 열기, 브로커 접속 문제 디버깅에 사용.
---

# apuapu-mqtt

어푸어푸 팀의 XIAO ESP32C6 보드들이 팀 전용 Mosquitto 브로커 한 대에 모인다.
이 스킬은 `mosquitto_pub` / `mosquitto_sub` 명령을 `skill.sh` 뒤에 감싼 것이다.

교실 공용 브로커(`classroom/…`)와는 **별개**다. 우리는 `apuapu/nodes/…` 토픽만
쓰므로 다른 팀과 절대 섞이지 않는다.

## 브로커

| | |
|---|---|
| 호스트 | `192.168.0.31` (현준 노트북) |
| MQTT (TCP) | `1883` — 보드, CLI, 데스크톱 클라이언트 |
| MQTT over WebSockets | `9001` — 브라우저(대시보드) |
| 인증 | 익명, 아이디/비밀번호 없음 |

브로커 노트북과 **같은 WiFi**에 있어야 한다. 사설 IP라 교실 밖에서는 닿지 않는다.

공유기가 IP를 바꾸면 파일을 고치지 말고 환경변수로 덮어쓴다:

```bash
export MQTT_HOST=192.168.0.31
export MQTT_PORT=1883
```

## 내 보드 이름

한 번 저장해두면 이후 모든 명령이 그 이름을 기본값으로 쓴다:

```bash
./skill.sh name hyunjoon   # 저장
./skill.sh name            # 저장된 값 확인
./skill.sh led on          # 이름 안 붙여도 됨
```

`~/.apuapu-mqtt` 에 저장된다. 환경변수 `MQTT_DEVICE`가 저장값보다 우선한다.

이 이름은 보드 `arduino_secrets.h`의 `DEVICE_NAME`과 **같아야** 한다. 그게
토픽 접두사를 결정하기 때문이다. 두 보드가 같은 이름을 쓰면 같은 토픽을 두고
싸우면서 서로를 브로커에서 계속 밀어낸다. 팀 안에서 유일한 이름을 고를 것.

## 토픽

각 보드는 이름(`DEVICE_NAME`, 비워두면 `c6-` + MAC 뒤 3바이트)으로 자기
영역을 갖는다. 보드는 부팅할 때 시리얼 모니터에 자기 이름을 출력한다.

| 토픽 | 방향 | 페이로드 |
|---|---|---|
| `apuapu/nodes/<id>/led/set` | 나 → 보드 | `on`, `off`, `toggle` |
| `apuapu/nodes/<id>/led/state` | 보드 → 나 | `on`, `off` (retained) |
| `apuapu/nodes/<id>/sensor/a0` | 보드 → 나 | `{"raw":2048,"mv":1650}` 2초마다 |
| `apuapu/nodes/<id>/status` | 보드 → 나 | `online`, `offline` (retained, 유언) |

`status`와 `led/state`는 retained라, 새로 구독해도 다음 변화를 기다리지 않고
현재 상태를 즉시 받는다.

## 사용법

```bash
./skill.sh name hyunjoon     # 내 보드 이름 저장 (최초 1회)
./skill.sh check             # 브로커에 닿는가?
./skill.sh devices           # 지금 온라인인 보드
./skill.sh led on            # 내 보드 LED 켜기
./skill.sh led off
./skill.sh led yeeun on      # 다른 보드를 이름으로 지정
./skill.sh sensor            # 내 보드 A0 값 스트리밍
./skill.sh watch             # 내 보드의 모든 메시지
./skill.sh dashboard         # 브라우저로 대시보드 열기
```

## 준비물

`skill.sh`는 mosquitto 커맨드라인 클라이언트를 쓴다:

- **Windows** — <https://mosquitto.org/download/> 설치 후 `C:\Program Files\mosquitto`
  를 PATH에 추가. `skill.sh`는 Git Bash에서 실행.
  (winget도 가능: `winget install --id EclipseFoundation.Mosquitto -e`)
- **macOS** — `brew install mosquitto`
- **Linux** — `sudo apt install mosquitto-clients`

`skill.sh`가 기본 Windows 설치 경로를 자동으로 찾으므로 PATH 설정은 없어도 대개 된다.

## 대시보드

`web/dashboard.html`이 팀의 모든 노드를 한 화면에 보여준다 — 온/오프라인 상태,
실시간 A0 값, 최근 60초 그래프, LED 버튼. 서버 없이 파일을 그대로 열면 된다.

```bash
./skill.sh dashboard
```

다른 브로커를 보려면 `dashboard.html?host=192.168.0.50` 처럼 넘긴다. 화면 위쪽
입력칸에서 바꿔도 되고, 그 값은 브라우저에 저장된다.

브라우저는 raw MQTT TCP 소켓을 열 수 없어서 1883으로는 접속할 수 없다. 9001
WebSocket listener가 그래서 존재한다.

## 브라우저에서 직접 쓰기

```js
const client = mqtt.connect('ws://192.168.0.31:9001')
client.subscribe('apuapu/nodes/+/sensor/a0')
client.publish('apuapu/nodes/hyunjoon/led/set', 'on')
```

## 보드 펌웨어

`firmware/ApuapuNode/` 를 Arduino IDE나 arduino-cli로 올린다.

1. `arduino_secrets.example.h` → `arduino_secrets.h` 로 복사
2. WiFi 비밀번호, `MQTT_HOST`, 본인 `DEVICE_NAME` 채우기
3. 보드 매니저에서 ESP32 코어 설치, 라이브러리에서 **PubSubClient** 설치
4. 보드를 `XIAO_ESP32C6`로 선택하고 업로드

```bash
arduino-cli lib install PubSubClient
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 firmware/ApuapuNode
arduino-cli upload -p COM3 --fqbn esp32:esp32:XIAO_ESP32C6 firmware/ApuapuNode
```

## 브로커 담당자용

브로커를 돌리는 사람만 하면 된다 (팀당 한 명).

```powershell
# 관리자 권한 PowerShell
powershell -ExecutionPolicy Bypass -File .\broker\apply-broker-config.ps1
```

Mosquitto 2.x는 listener를 선언하지 않으면 localhost에만 바인딩한다. 이 스크립트가
1883/9001 listener를 열고, 방화벽 규칙을 추가하고, 서비스를 재시작한 뒤 팀원에게
알려줄 IP를 출력한다.

## 문제 해결

| 증상 | 원인 |
|---|---|
| `check` 실패, connection refused | 브로커가 안 돌고 있거나 아직 localhost에만 묶여 있음 — 담당자가 `broker/apply-broker-config.ps1` 실행 필요 |
| `check` 실패, timeout | WiFi가 다르거나, 브로커 노트북 방화벽이 포트를 막고 있음 |
| `check`는 되는데 `devices`가 비어 있음 | 켜진 보드가 없거나, 보드가 브로커에 못 닿음 |
| 갑자기 전부 끊김 | 공유기가 브로커 노트북에 새 IP를 줌 — `ipconfig`로 확인하고 `export MQTT_HOST=새주소` |
| 보드가 `offline`로 표시 | 전원이나 WiFi가 끊김, 유언(LWT)이 발동한 것 |
| LED 명령은 나가는데 아무 반응 없음 | 보드 이름이 틀림 — `./skill.sh devices`로 확인 |
| `mosquitto_sub not found` | 클라이언트 미설치. `skill.sh`가 윈도우 기본 경로는 알아서 찾는다 |
| 보드가 안 뜨고 시리얼에 점만 계속 | WiFi SSID는 대소문자를 구분한다 (`ICEE`, `icee` 아님). 그리고 보드는 2.4GHz 전용이라 5GHz 전용 SSID에는 절대 못 붙는다 |
| 명령이 몇 초씩 밀리거나 씹힘 | 옛날 펌웨어. WiFi 모뎀 슬립이 하향 패킷을 붙잡는다 — 현재 스케치는 `WiFi.setSleep(false)`를 호출한다 |
| 대시보드가 계속 "연결 안 됨" | 9001 포트 문제. 브라우저는 1883을 못 쓴다. 담당자가 websockets listener를 열었는지 확인 |
