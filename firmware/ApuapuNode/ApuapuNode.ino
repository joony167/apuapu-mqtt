// 어푸어푸 팀 - XIAO ESP32C6 MQTT 노드
//
//   구독  apuapu/nodes/<id>/led/set      payload "on" | "off" | "toggle"
//   발행  apuapu/nodes/<id>/led/state    "on" | "off"            (retained)
//   발행  apuapu/nodes/<id>/sensor/a0    {"raw":2048,"mv":1650}  2초마다
//   발행  apuapu/nodes/<id>/status       "online" | "offline"    (retained, LWT)
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

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String deviceId;
String topicLedSet, topicLedState, topicSensor, topicStatus;
unsigned long lastPublish = 0;

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

void connectWiFi() {
  Serial.printf("WiFi: connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
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

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    publishSensor();
  }
}
