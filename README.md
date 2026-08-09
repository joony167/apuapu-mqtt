# 어푸어푸 MQTT

어푸어푸 팀 전용 MQTT 실습 세트. XIAO ESP32C6 보드들이 팀 브로커 한 대에 모여
서로의 LED를 제어하고 A0 센서값을 공유한다.

교실 공용 브로커와는 **별개**다. 우리는 `apuapu/nodes/…` 토픽만 쓰므로 다른 팀과
섞이지 않는다.

```
                 [팀원 노트북] ──┐
                                 │  ws://…:9001 (대시보드)
  [보드 hyunjoon] ──┐            ▼
  [보드 ○○○]     ──┼──1883──> [Mosquitto 브로커]
  [보드 △△△]     ──┘           현준 노트북 192.168.0.31
```

## 팀원용 설치 (3단계)

**1. 스킬 설치**

```bash
npx skills add joony167/apuapu-mqtt
```

**2. mosquitto 클라이언트 설치**

```powershell
winget install --id EclipseFoundation.Mosquitto -e
```

macOS는 `brew install mosquitto`, Linux는 `sudo apt install mosquitto-clients`.

**3. 내 보드 이름 저장하고 접속 확인**

```bash
cd .agents/skills/apuapu-mqtt
./skill.sh name 내이름
./skill.sh check
./skill.sh devices
./skill.sh dashboard
```

## 보드 펌웨어 올리기

1. `firmware/ApuapuNode/arduino_secrets.example.h` → `arduino_secrets.h` 복사
2. WiFi 비밀번호와 본인 `DEVICE_NAME` 채우기 (팀 안에서 유일해야 함)
3. 업로드

```bash
arduino-cli lib install PubSubClient
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 firmware/ApuapuNode
arduino-cli upload -p COM3 --fqbn esp32:esp32:XIAO_ESP32C6 firmware/ApuapuNode
```

> 한글 사용자명 계정에서 링크 에러가 나면 `--build-path C:\dev\ac_build` 처럼
> ASCII 경로를 지정한다.

## 토픽

| 토픽 | 방향 | 페이로드 |
|---|---|---|
| `apuapu/nodes/<id>/led/set` | 나 → 보드 | `on`, `off`, `toggle` |
| `apuapu/nodes/<id>/led/state` | 보드 → 나 | `on`, `off` (retained) |
| `apuapu/nodes/<id>/sensor/a0` | 보드 → 나 | `{"raw":2048,"mv":1650}` |
| `apuapu/nodes/<id>/status` | 보드 → 나 | `online`, `offline` (retained, 유언) |

## 브로커 담당자용

팀에서 한 명만 하면 된다.

```powershell
winget install --id EclipseFoundation.Mosquitto -e
# 관리자 권한 PowerShell에서:
powershell -ExecutionPolicy Bypass -File .\broker\apply-broker-config.ps1
```

Mosquitto 2.x는 listener를 선언하지 않으면 localhost에만 바인딩해서, 기본 설치
상태로는 팀원이 붙을 수 없다. 위 스크립트가 1883(TCP)과 9001(WebSocket) listener를
열고, 방화벽 규칙을 추가하고, 서비스를 재시작한 뒤 팀원에게 알려줄 IP를 출력한다.

**IP가 바뀌면** 팀원들은 파일을 고치지 말고 환경변수로 덮어쓴다:

```bash
export MQTT_HOST=새주소
```

보드는 `arduino_secrets.h`의 `MQTT_HOST`를 고쳐서 다시 업로드해야 한다.

## 구성

```
broker/     mosquitto.conf + 적용 스크립트 (담당자만)
firmware/   XIAO ESP32C6 아두이노 스케치
skills/apuapu-mqtt/
  skill.sh      CLI
  SKILL.md      에이전트/사람용 문서
  web/          대시보드 (dashboard.html + mqtt.min.js)
```

## 보안

브로커는 익명 접속을 허용한다. 같은 WiFi에 있는 사람은 누구나 발행·구독할 수
있다. 교실 LAN 실습용이며, 공용 네트워크나 인터넷에 노출하지 말 것.
