// 이 파일을 `mesh_secrets.h` 로 복사한 뒤 채우세요. (.gitignore 처리됨)
//
// 메시 노드와 브리지가 **완전히 같은 값**을 써야 서로를 찾습니다.
// 접두사/비밀번호/포트/채널 중 하나라도 다르면 메시가 안 붙습니다.

#pragma once

// ---- 메시 (모든 노드 공통, 팀에서 합의한 값) ----
#define MESH_PREFIX   "apuapu"
#define MESH_PASSWORD "11112222"
#define MESH_PORT     5555

// 브리지가 붙을 ICEE의 **2.4GHz 채널**과 같아야 합니다.
// ICEE는 듀얼밴드라 노트북에는 5GHz(채널 36)로 보이지만, ESP32-C6는 2.4GHz만
// 쓰므로 2.4GHz BSS의 채널인 10을 맞춰야 합니다.
// painlessMesh 기본값은 1입니다. 이 줄을 빼먹으면 브리지와 서로 못 봅니다.
#define MESH_CHANNEL  10

// ---- 이 보드 ----
// 대시보드에 이 이름으로 뜹니다. 팀 안에서 유일해야 합니다.
#define NODE_NAME     "your-name"
