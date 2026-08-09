// 이 파일을 `arduino_secrets.h`로 복사한 뒤 본인 값으로 채우세요.
// `arduino_secrets.h`는 .gitignore에 있어서 저장소에 올라가지 않습니다.

#pragma once

#define WIFI_SSID   "ICEE"                // 2.4GHz만 가능 - ESP32C6에는 5GHz 라디오가 없음
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

// 어푸어푸 팀 브로커 = 현준 노트북. 공유기가 IP를 바꾸면 이 값도 바꿔야 한다.
// 현재 IP는 브로커 담당자가 `ipconfig`로 확인해서 알려준다.
#define MQTT_HOST   "192.168.0.31"
#define MQTT_PORT   1883

// 브로커에서 쓸 내 보드 이름. 토픽이 apuapu/nodes/<이름>/... 이 된다.
// 빈 문자열("")로 두면 MAC 뒤 3바이트를 붙인 "c6-xxxxxx"가 자동으로 쓰인다.
// 팀 안에서 유일해야 한다 - 이름이 겹치면 두 보드가 같은 토픽을 두고 싸우면서
// 서로를 브로커에서 계속 밀어낸다.
#define DEVICE_NAME "your-name"
