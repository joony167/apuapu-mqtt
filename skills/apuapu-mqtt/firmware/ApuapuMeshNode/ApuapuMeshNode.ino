// 어푸어푸 팀 - painlessMesh 리프 노드 (XIAO ESP32C6)
//
// 이 보드는 공유기에 붙지 않는다. painlessMesh로 이웃 노드끼리만 통신하고,
// 메시지는 홉을 타고 브리지 노드까지 전달된다. 브리지가 그것을 MQTT로 올려
// 대시보드에 기록한다.
//
//   시리얼 모니터(115200)에 문장을 치고 엔터
//     -> mesh.sendBroadcast({"from":"이름","type":"msg","text":"문장"})
//     -> (홉 여러 번) -> 브리지 -> apuapu/nodes/<이름>/msg -> 대시보드
//
// A0 센서값도 2초마다 같은 방식으로 흘려보내므로, 공유기 신호가 안 닿는
// 자리에 있어도 대시보드에 카드가 뜬다.
//
// 업로드 시 파티션을 huge_app 으로 지정해야 한다 (기본 1.2MB로는 안 들어감):
//   --fqbn esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app

#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include "mesh_secrets.h"

#define LED_PIN 15                 // XIAO 유저 LED, active LOW
#define A0_PIN  A0

const unsigned long SENSOR_INTERVAL_MS = 2000;
const unsigned long SERIAL_IDLE_MS = 100;

Scheduler userScheduler;
painlessMesh mesh;

String serialBuf = "";
unsigned long lastSerialCharMs = 0;
unsigned long lastSensorMs = 0;

// ---------------------------------------------------------------- 보내기

// 메시 위에서는 토픽이 없다. 그래서 누가 보낸 무슨 종류인지를 페이로드 안에
// 직접 적어 보낸다. 브리지가 이걸 읽어 MQTT 토픽으로 바꿔준다.
void meshSend(const char* type, const String& body) {
  JsonDocument doc;
  doc["from"] = NODE_NAME;
  doc["type"] = type;
  doc["text"] = body;

  String out;
  serializeJson(doc, out);
  mesh.sendBroadcast(out);
  Serial.printf("mesh TX  %s\n", out.c_str());
}

void flushSerialLine() {
  serialBuf.trim();
  if (serialBuf.length() == 0) {
    serialBuf = "";
    return;
  }
  meshSend("msg", serialBuf);
  serialBuf = "";
}

void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    lastSerialCharMs = millis();
    if (c == '\n' || c == '\r') {
      flushSerialLine();
    } else if (serialBuf.length() < 120) {
      serialBuf += c;
    }
  }
  if (serialBuf.length() > 0 && millis() - lastSerialCharMs > SERIAL_IDLE_MS) {
    flushSerialLine();
  }
}

// ---------------------------------------------------------------- 받기

void onMeshReceive(uint32_t from, String& msg) {
  Serial.printf("mesh RX  from %u: %s\n", from, msg.c_str());

  // 브리지가 되돌려 보낸 LED 명령이면 반응한다.
  JsonDocument doc;
  if (deserializeJson(doc, msg)) return;              // JSON이 아니면 무시

  const char* to   = doc["to"]   | "";
  const char* type = doc["type"] | "";
  const char* text = doc["text"] | "";

  if (strcmp(type, "led") != 0) return;
  if (strcmp(to, NODE_NAME) != 0) return;             // 내 것만

  if (!strcmp(text, "on")) {
    digitalWrite(LED_PIN, LOW);
  } else if (!strcmp(text, "off")) {
    digitalWrite(LED_PIN, HIGH);
  } else if (!strcmp(text, "toggle")) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  } else {
    return;
  }
  meshSend("led/state", digitalRead(LED_PIN) == LOW ? "on" : "off");
}

void onChangedConnections() {
  Serial.printf("mesh: 연결 변경, 지금 보이는 노드 %d개\n",
                (int)mesh.getNodeList().size());
}

// ---------------------------------------------------------------- 스케치

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);   // 꺼진 상태로 시작
  pinMode(A0_PIN, INPUT);
  delay(500);

  Serial.printf("\n=== 어푸어푸 mesh node: %s ===\n", NODE_NAME);
  Serial.printf("mesh %s / 채널 %d / 포트 %d\n", MESH_PREFIX, MESH_CHANNEL, MESH_PORT);

  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  // 채널은 브리지가 붙는 공유기 채널과 같아야 한다. 다르면 서로 못 본다.
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT, WIFI_AP_STA, MESH_CHANNEL);
  mesh.onReceive(&onMeshReceive);
  mesh.onChangedConnections(&onChangedConnections);
  // 메시 안에 루트(브리지)가 있다는 걸 모든 노드가 알아야 형성이 안정적이다.
  mesh.setContainsRoot(true);

  Serial.printf("node id: %u\n", mesh.getNodeId());
  Serial.println("시리얼에 문장을 치고 엔터 -> 메시를 타고 대시보드에 기록됩니다.");
}

void loop() {
  mesh.update();
  pollSerial();

  unsigned long now = millis();
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;

    char payload[64];
    snprintf(payload, sizeof(payload), "{\"raw\":%d,\"mv\":%d}",
             analogRead(A0_PIN), (int)analogReadMilliVolts(A0_PIN));
    meshSend("sensor/a0", payload);
  }
}
