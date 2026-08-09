// 이 파일을 `mesh_secrets.h` 로 복사한 뒤 채우세요. (.gitignore 처리됨)
//
// 브리지(sink/게이트웨이)는 팀에서 **한 대만** 올립니다.
// 메시 설정 4개(접두사/비밀번호/포트/채널)는 리프 노드와 완전히 같아야 합니다.

#pragma once

// ---- 메시 (모든 노드 공통, 팀에서 합의한 값) ----
#define MESH_PREFIX   "apuapu"
#define MESH_PASSWORD "11112222"
#define MESH_PORT     5555

// 아래 WIFI_SSID 의 **2.4GHz** 채널과 같아야 합니다.
// 브리지가 부팅할 때 스캔해서 다르면 경고를 띄웁니다.
#define MESH_CHANNEL  10

// ---- 공유기 (브리지만 필요) ----
#define WIFI_SSID     "ICEE"
#define WIFI_PASS     "icee2026"

// ---- 브로커 ----
#define MQTT_HOST     "192.168.0.31"
#define MQTT_PORT     1883

// ---- 이 보드 ----
#define NODE_NAME     "hyunjoon"
