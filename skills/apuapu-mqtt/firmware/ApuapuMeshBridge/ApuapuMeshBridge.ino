// 어푸어푸 팀 - painlessMesh <-> MQTT 브리지 (sink / 게이트웨이)
//
// 팀에서 이 스케치는 **한 대만** 올린다. 이 보드만 공유기(ICEE)에 붙고,
// 나머지 리프 노드는 메시로만 연결된다.
//
//   [리프 A] --mesh--> [리프 B] --mesh--> [브리지] --WiFi--> [브로커] -> 대시보드
//
// 메시에서 받은 것을 MQTT 토픽으로 바꿔 올린다:
//   {"from":"isak","type":"msg","text":"hi"} -> apuapu/nodes/isak/msg       = hi
//   {"from":"isak","type":"sensor/a0",...}   -> apuapu/nodes/isak/sensor/a0
//   {"from":"isak","type":"led/state","text":"on"} -> apuapu/nodes/isak/led/state
//
// 반대 방향도 연결한다. 대시보드에서 누른 LED 버튼을
//   apuapu/nodes/<이름>/led/set -> 메시 broadcast -> 해당 리프가 반응
//
// 그리고 메시 구성을 apuapu/mesh/topology 로 올려 누가 누구를 거쳐 붙어
// 있는지 볼 수 있게 한다.
//
// 파티션은 반드시 huge_app:
//   --fqbn esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app

#include <Arduino.h>
#include <painlessMesh.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <map>
#include "mesh_secrets.h"

#define LED_PIN 15
#define A0_PIN  A0

#define TOPIC_BASE "apuapu/nodes"

const unsigned long SENSOR_INTERVAL_MS   = 2000;
const unsigned long TOPOLOGY_INTERVAL_MS = 10000;
const unsigned long SERIAL_IDLE_MS       = 100;

Scheduler userScheduler;
painlessMesh mesh;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

IPAddress myIP(0, 0, 0, 0);
String serialBuf = "";
unsigned long lastSerialCharMs = 0;
unsigned long lastSensorMs = 0;
unsigned long lastTopologyMs = 0;

// 메시 nodeId <-> 사람이 읽는 이름. 리프가 죽었을 때 어느 이름을 offline으로
// 표시할지 알아야 해서 들고 있는다.
std::map<uint32_t, String> nodeNames;

// ---------------------------------------------------------------- MQTT

String topicFor(const String& name, const char* leaf) {
  return String(TOPIC_BASE) + "/" + name + "/" + leaf;
}

void publishStatus(const String& name, bool online) {
  mqtt.publish(topicFor(name, "status").c_str(), online ? "online" : "offline", true);
}

// ---------------------------------------------------------------- 메시 -> MQTT

void onMeshReceive(uint32_t from, String& msg) {
  Serial.printf("mesh RX  from %u: %s\n", from, msg.c_str());

  JsonDocument doc;
  if (deserializeJson(doc, msg)) {
    // 우리 형식이 아니면 nodeId 이름으로 그냥 흘려보낸다. 디버깅에 쓸모 있다.
    mqtt.publish(topicFor("mesh-" + String(from), "msg").c_str(), msg.c_str());
    return;
  }

  const char* fromName = doc["from"] | "";
  const char* type     = doc["type"] | "";
  String text          = doc["text"] | "";

  if (!strlen(fromName) || !strlen(type)) return;

  // 브리지가 자기가 broadcast 한 LED 명령을 되받는 경우는 무시한다.
  if (!strcmp(type, "led")) return;

  String name = fromName;

  // 처음 보는 노드면 카드가 뜨도록 online 을 남긴다.
  if (nodeNames.find(from) == nodeNames.end() || nodeNames[from] != name) {
    nodeNames[from] = name;
    publishStatus(name, true);
    Serial.printf("bridge: %s (id %u) 등록\n", name.c_str(), from);
  }

  mqtt.publish(topicFor(name, type).c_str(), text.c_str(),
               strcmp(type, "led/state") == 0);   // led/state 만 retained
}

// 리프가 메시에서 사라지면 대시보드에도 offline 으로 보여야 한다.
// 유언(LWT)은 브리지 자신의 MQTT 연결에만 걸리므로, 리프 몫은 여기서 챙긴다.
void onChangedConnections() {
  auto list = mesh.getNodeList(true);
  Serial.printf("mesh: 연결 변경, 노드 %d개\n", (int)list.size());

  for (auto it = nodeNames.begin(); it != nodeNames.end(); ) {
    bool alive = false;
    for (auto&& id : list) {
      if (id == it->first) { alive = true; break; }
    }
    if (alive) {
      ++it;
    } else {
      Serial.printf("bridge: %s (id %u) 사라짐 -> offline\n", it->second.c_str(), it->first);
      publishStatus(it->second, false);
      it = nodeNames.erase(it);
    }
  }
}

// ---------------------------------------------------------------- MQTT -> 메시

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String text;
  text.reserve(length);
  for (unsigned int i = 0; i < length; i++) text += (char)payload[i];
  text.trim();
  text.toLowerCase();

  // apuapu/nodes/<이름>/led/set 에서 <이름>을 꺼낸다
  String t = topic;
  String prefix = String(TOPIC_BASE) + "/";
  if (!t.startsWith(prefix)) return;
  int slash = t.indexOf('/', prefix.length());
  if (slash < 0) return;
  String name = t.substring(prefix.length(), slash);

  Serial.printf("mqtt RX  %s = %s\n", topic, text.c_str());

  if (name == NODE_NAME) {                 // 브리지 자신
    if (text == "on")          digitalWrite(LED_PIN, LOW);
    else if (text == "off")    digitalWrite(LED_PIN, HIGH);
    else if (text == "toggle") digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    else return;
    mqtt.publish(topicFor(NODE_NAME, "led/state").c_str(),
                 digitalRead(LED_PIN) == LOW ? "on" : "off", true);
    return;
  }

  // 리프에게는 메시로 전달한다. 받는 쪽이 to 를 보고 자기 것만 처리한다.
  JsonDocument doc;
  doc["from"] = NODE_NAME;
  doc["to"]   = name;
  doc["type"] = "led";
  doc["text"] = text;

  String out;
  serializeJson(doc, out);
  mesh.sendBroadcast(out);
  Serial.printf("mesh TX  %s\n", out.c_str());
}

void connectMqtt() {
  if (mqtt.connected()) return;

  Serial.printf("MQTT: connecting to %s:%d as %s-bridge ... ", MQTT_HOST, MQTT_PORT, NODE_NAME);
  String clientId = String(NODE_NAME) + "-bridge";
  String willTopic = topicFor(NODE_NAME, "status");

  if (mqtt.connect(clientId.c_str(), nullptr, nullptr,
                   willTopic.c_str(), 0, true, "offline")) {
    Serial.println("connected");
    publishStatus(NODE_NAME, true);
    // 팀 전체의 LED 명령을 받아서 메시로 중계해야 하므로 + 로 구독한다.
    mqtt.subscribe((String(TOPIC_BASE) + "/+/led/set").c_str(), 1);
    Serial.printf("subscribed to %s/+/led/set\n", TOPIC_BASE);

    // 이미 알고 있는 리프들의 상태를 다시 올려 대시보드가 비지 않게 한다.
    for (auto&& kv : nodeNames) publishStatus(kv.second, true);
  } else {
    Serial.printf("failed rc=%d\n", mqtt.state());
  }
}

// ---------------------------------------------------------------- 시리얼

void flushSerialLine() {
  serialBuf.trim();
  if (serialBuf.length() == 0) { serialBuf = ""; return; }
  mqtt.publish(topicFor(NODE_NAME, "msg").c_str(), serialBuf.c_str());
  Serial.printf("mqtt TX  %s = %s\n", topicFor(NODE_NAME, "msg").c_str(), serialBuf.c_str());
  serialBuf = "";
}

void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    lastSerialCharMs = millis();
    if (c == '\n' || c == '\r') flushSerialLine();
    else if (serialBuf.length() < 120) serialBuf += c;
  }
  if (serialBuf.length() > 0 && millis() - lastSerialCharMs > SERIAL_IDLE_MS) {
    flushSerialLine();
  }
}

// ---------------------------------------------------------------- 채널 확인

// 메시 AP와 공유기 접속은 라디오를 공유한다. 채널이 다르면 둘 중 하나가
// 조용히 죽는다. 원인을 찾기 아주 어려운 실패라서 부팅할 때 미리 확인한다.
void checkChannel() {
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  int found = 0;

  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) { found = WiFi.channel(i); break; }
  }
  WiFi.scanDelete();

  if (!found) {
    Serial.printf("경고: '%s' 를 못 찾았습니다. 범위 밖이거나 이름이 틀렸습니다.\n", WIFI_SSID);
  } else if (found != MESH_CHANNEL) {
    Serial.println("\n**********************************************************");
    Serial.printf("경고: '%s' 는 채널 %d 인데 MESH_CHANNEL 은 %d 입니다.\n",
                  WIFI_SSID, found, MESH_CHANNEL);
    Serial.printf("      모든 노드의 MESH_CHANNEL 을 %d 로 바꿔 다시 올리세요.\n", found);
    Serial.println("      그대로 두면 메시나 공유기 연결 중 하나가 안 됩니다.");
    Serial.println("**********************************************************\n");
  } else {
    Serial.printf("채널 확인 OK: '%s' 채널 %d = MESH_CHANNEL\n", WIFI_SSID, found);
  }
}

// ---------------------------------------------------------------- 스케치

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(A0_PIN, INPUT);
  delay(500);

  Serial.printf("\n=== 어푸어푸 mesh 브리지: %s ===\n", NODE_NAME);
  checkChannel();

  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA, MESH_CHANNEL);
  mesh.onReceive(&onMeshReceive);
  mesh.onChangedConnections(&onChangedConnections);

  // 공유기 접속은 painlessMesh 를 통해서 해야 한다. WiFi.begin 을 직접 부르면
  // 메시가 라디오 설정을 덮어써서 서로 싸운다.
  mesh.stationManual(WIFI_SSID, WIFI_PASS);
  mesh.setHostname((String(NODE_NAME) + "-bridge").c_str());

  // 브리지는 루트여야 메시 형성이 안정적이다.
  mesh.setRoot(true);
  mesh.setContainsRoot(true);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(10);

  Serial.printf("node id: %u\n", mesh.getNodeId());
  Serial.println("공유기 IP를 받으면 MQTT에 접속합니다...");
}

void loop() {
  mesh.update();

  // 공유기에서 IP를 받은 뒤에야 MQTT 접속이 의미가 있다.
  IPAddress ip = IPAddress(mesh.getStationIP());
  if (myIP != ip) {
    myIP = ip;
    Serial.printf("WiFi: station IP %s\n", myIP.toString().c_str());
  }

  if (myIP != IPAddress(0, 0, 0, 0)) {
    if (!mqtt.connected()) connectMqtt();
    mqtt.loop();
    pollSerial();

    unsigned long now = millis();

    if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
      lastSensorMs = now;
      char payload[64];
      snprintf(payload, sizeof(payload), "{\"raw\":%d,\"mv\":%d}",
               analogRead(A0_PIN), (int)analogReadMilliVolts(A0_PIN));
      mqtt.publish(topicFor(NODE_NAME, "sensor/a0").c_str(), payload);
    }

    if (now - lastTopologyMs >= TOPOLOGY_INTERVAL_MS) {
      lastTopologyMs = now;
      // 누가 누구를 거쳐 붙어 있는지 그대로 올린다 (JSON 트리).
      mqtt.publish("apuapu/mesh/topology", mesh.subConnectionJson().c_str(), true);
    }
  }
}
