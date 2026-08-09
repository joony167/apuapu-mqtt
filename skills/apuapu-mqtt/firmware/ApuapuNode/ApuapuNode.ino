// 어푸어푸 팀 - XIAO ESP32C6 MQTT 노드
//
//   구독  apuapu/nodes/<id>/led/set      payload "on" | "off" | "toggle"
//   발행  apuapu/nodes/<id>/led/state    "on" | "off"            (retained)
//   발행  apuapu/nodes/<id>/sensor/a0    {"raw":2048,"mv":1650}  2초마다
//   발행  apuapu/nodes/<id>/status       "online" | "offline"    (retained, LWT)
//   발행  apuapu/nodes/<id>/msg          시리얼 모니터에 친 문장 그대로
//
// 시리얼 모니터(115200)에 문장을 치고 엔터를 누르면 msg 토픽으로 나가고,
// 대시보드의 메시지 기록에 쌓인다.
//
// <id>는 arduino_secrets.h의 DEVICE_NAME이다. 비워두면 MAC 뒤 3바이트를 붙인
// "c6-xxxxxx"로 대체되므로, 설정을 안 해도 겹치지 않는 토픽을 갖는다.
//
// TOPIC_BASE는 팀 전용이다. 다른 팀과 겹치면 서로 남의 보드를 켜고 끄게 된다.

#include <WiFi.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

#define TOPIC_BASE "apuapu/nodes"   // 어푸어푸 팀 전용 접두사

#define LED_PIN 15                  // XIAO 유저 LED, active LOW
#define A0_PIN  A0                  // D0 / GPIO0

const unsigned long PUBLISH_INTERVAL_MS = 2000;

// 유언(LWT)은 브로커가 옛 연결이 죽은 걸 알아챈 뒤에야 나간다. 보드가 빨리
// 재부팅하면 새 online 을 먼저 쓰고, 뒤늦게 도착한 offline 이 그걸 덮어쓴다.
// 살아있는 보드가 계속 offline 으로 보이는 이유가 이것이다. 주기적으로 다시
// 알려서 스스로 회복하게 한다.
const unsigned long STATUS_REPUBLISH_MS = 15000;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String deviceId;
String topicLedSet, topicLedState, topicSensor, topicStatus, topicMsg;
unsigned long lastPublish = 0;
unsigned long lastStatusMs = 0;

// 시리얼 모니터 입력을 모으는 버퍼
const unsigned long SERIAL_IDLE_MS = 100;   // 줄바꿈을 안 보내는 모니터도 있다
String serialBuf = "";
unsigned long lastSerialCharMs = 0;

// ---------------------------------------------------------------- helpers

void setLed(bool on) {
  digitalWrite(LED_PIN, on ? LOW : HIGH);
  mqtt.publish(topicLedState.c_str(), on ? "on" : "off", true);  // retained
  Serial.printf("LED -> %s\n", on ? "ON" : "OFF");
}

bool ledIsOn() {
  return digitalRead(LED_PIN) == LOW;
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toLowerCase();

  Serial.printf("RX %s = %s\n", topic, msg.c_str());

  if (String(topic) != topicLedSet) return;

  if (msg == "on" || msg == "1" || msg == "true") {
    setLed(true);
  } else if (msg == "off" || msg == "0" || msg == "false") {
    setLed(false);
  } else if (msg == "toggle") {
    setLed(!ledIsOn());
  } else {
    Serial.printf("ignored payload: '%s'\n", msg.c_str());
  }
}

// 연결이 안 될 때 점만 찍으면 원인을 알 수 없다. 라우터가 알려주는 끊김 사유를
// 잡아두었다가 사람이 읽을 수 있는 말로 바꿔서 보여준다.
volatile uint8_t lastDisconnectReason = 0;

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
  }
}

const char* wifiReasonName(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND:
      return "SSID를 못 찾음 - 이름 오타(대소문자!), 5GHz 전용 SSID, 또는 신호 범위 밖";
    case WIFI_REASON_AUTH_FAIL:
      return "인증 실패 - 비밀번호 확인";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4-way handshake 실패 - 비밀번호가 틀렸을 때 주로 이렇게 나옴";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "handshake 타임아웃 - 비밀번호 또는 신호 문제";
    case WIFI_REASON_ASSOC_FAIL:
      return "association 거부 - AP 접속 대수 한계이거나 MAC 필터링";
    case WIFI_REASON_CONNECTION_FAIL:
      return "연결 실패 - 신호가 약하거나 AP가 응답하지 않음";
    case WIFI_REASON_AUTH_EXPIRE:
      return "인증 만료 - 신호가 약함";
    case 0:
      return "아직 사유 없음 - 스캔 중이거나 AP 응답 대기";
    default:
      return "기타";
  }
}

// SSID가 보이기는 하는지, 어느 채널/암호화인지 눈으로 확인시켜 준다.
// "비밀번호가 틀렸나 / 아예 안 보이나"를 가르는 게 핵심이다.
void diagnoseWiFi() {
  Serial.printf("\n--- WiFi 진단: '%s' 를 찾는 중 ---\n", WIFI_SSID);
  int n = WiFi.scanNetworks(false, true);
  bool found = false;

  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      found = true;
      Serial.printf("  찾음: RSSI %d dBm, 채널 %d, 암호화 %d, BSSID %s\n",
                    WiFi.RSSI(i), WiFi.channel(i), (int)WiFi.encryptionType(i),
                    WiFi.BSSIDstr(i).c_str());
    }
  }

  if (!found) {
    Serial.printf("  '%s' 가 스캔 목록에 없습니다 (주변 %d개 감지).\n", WIFI_SSID, n);
    Serial.println("  -> SSID 철자/대소문자를 확인하세요. 이 보드는 2.4GHz만 잡습니다.");
    Serial.println("  -> 목록에 보이는 SSID:");
    for (int i = 0; i < n && i < 10; i++) {
      Serial.printf("       %s (%d dBm)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  } else {
    Serial.println("  -> SSID는 보입니다. 그렇다면 비밀번호 문제일 가능성이 큽니다.");
  }

  Serial.println("--- 진단 끝, 계속 재시도합니다 ---\n");
  WiFi.scanDelete();
}

void connectWiFi() {
  Serial.printf("WiFi: connecting to %s", WIFI_SSID);
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  bool diagnosed = false;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    // 15초면 정상적인 경우 이미 붙었다. 그 뒤로도 안 되면 이유를 캐본다.
    if (!diagnosed && millis() - start > 15000) {
      diagnosed = true;
      Serial.printf("\nWiFi: 15초 동안 실패. 마지막 사유 %d - %s\n",
                    lastDisconnectReason, wifiReasonName(lastDisconnectReason));
      diagnoseWiFi();
      WiFi.begin(WIFI_SSID, WIFI_PASS);   // 스캔 후 다시 시도
      Serial.print("WiFi: 재시도");
    }
  }

  // 모뎀 슬립이 기본값인데, 다음 DTIM 비컨까지 하향 패킷을 붙잡아 둔다.
  // 사람 많은 교실 AP에서는 LED 명령이 몇 초씩 밀리거나 TCP 연결이 조용히
  // 끊기는 형태로 나타난다. USB 전원을 쓰므로 절전 대신 반응 속도를 택한다.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.printf("\nWiFi: connected, IP %s, RSSI %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void connectMqtt() {
  while (!mqtt.connected()) {
    Serial.printf("MQTT: connecting to %s:%d as %s ... ",
                  MQTT_HOST, MQTT_PORT, deviceId.c_str());

    // 유언(LWT): 이 보드가 사라지면 브로커가 대신 "offline"을 발행한다.
    bool ok = mqtt.connect(deviceId.c_str(),
                           nullptr, nullptr,
                           topicStatus.c_str(), 0, true, "offline");

    if (ok) {
      Serial.println("connected");
      mqtt.publish(topicStatus.c_str(), "online", true);
      // QoS 1: 소켓이 잠깐 바쁘다고 명령을 버리는 대신, 보드가 받았다고
      // 응답할 때까지 브로커가 들고 있는다.
      mqtt.subscribe(topicLedSet.c_str(), 1);
      Serial.printf("subscribed to %s (qos 1)\n", topicLedSet.c_str());
      setLed(ledIsOn());  // 현재 상태 재발행 - 늦게 들어온 구독자도 볼 수 있게
    } else {
      // 빠르게 재시도한다. 끊겨 있는 1초는 명령이 버려지는 1초다.
      Serial.printf("failed rc=%d, retrying in 1s\n", mqtt.state());
      delay(1000);
    }
  }
}

// 시리얼 모니터에 친 문장을 그대로 msg 토픽으로 보낸다.
void flushSerialLine() {
  serialBuf.trim();
  if (serialBuf.length() == 0) {   // \r\n 의 두 번째 문자 등
    serialBuf = "";
    return;
  }
  mqtt.publish(topicMsg.c_str(), serialBuf.c_str());
  Serial.printf("TX %s = %s\n", topicMsg.c_str(), serialBuf.c_str());
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
  // 줄바꿈을 안 붙이는 시리얼 모니터도 있어서, 입력이 끊기면 한 줄로 본다.
  if (serialBuf.length() > 0 && millis() - lastSerialCharMs > SERIAL_IDLE_MS) {
    flushSerialLine();
  }
}

void publishSensor() {
  int raw = analogRead(A0_PIN);
  int mv  = analogReadMilliVolts(A0_PIN);

  char payload[64];
  snprintf(payload, sizeof(payload), "{\"raw\":%d,\"mv\":%d}", raw, mv);

  mqtt.publish(topicSensor.c_str(), payload);
  Serial.printf("TX %s = %s\n", topicSensor.c_str(), payload);
}

// ---------------------------------------------------------------- sketch

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);   // 꺼진 상태로 시작
  pinMode(A0_PIN, INPUT);

  delay(500);

  deviceId = DEVICE_NAME;
  if (deviceId.length() == 0) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char idBuf[16];
    snprintf(idBuf, sizeof(idBuf), "c6-%02x%02x%02x", mac[3], mac[4], mac[5]);
    deviceId = idBuf;
  }

  String base   = String(TOPIC_BASE) + "/" + deviceId;
  topicLedSet   = base + "/led/set";
  topicLedState = base + "/led/state";
  topicSensor   = base + "/sensor/a0";
  topicStatus   = base + "/status";
  topicMsg      = base + "/msg";

  Serial.printf("\n=== 어푸어푸 MQTT node ===\ndevice id: %s\n", deviceId.c_str());
  Serial.printf("led set:   %s\n", topicLedSet.c_str());
  Serial.printf("sensor:    %s\n", topicSensor.c_str());

  connectWiFi();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(30);      // 기본 15초는 혼잡한 AP에서 빠듯하다
  mqtt.setSocketTimeout(10);  // 죽은 소켓을 기본 15초보다 빨리 알아챈다
  connectMqtt();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  // 왜 끊겼는지 말해준다 - 조용한 재접속은 명령이 사라진 이유를 감춘다.
  if (!mqtt.connected()) {
    Serial.printf("MQTT: disconnected (state=%d, WiFi RSSI %d dBm)\n",
                  mqtt.state(), WiFi.RSSI());
    connectMqtt();
  }

  mqtt.loop();
  pollSerial();

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    publishSensor();
  }

  if (now - lastStatusMs >= STATUS_REPUBLISH_MS) {
    lastStatusMs = now;
    mqtt.publish(topicStatus.c_str(), "online", true);
  }
}
