#pragma once

// Keep this file private: do not post it or commit it to a public repository.

// 2.4 GHz network only (the ESP8266 cannot use 5 GHz). A phone hotspot is the safest choice
// for a demo - college networks with a login page will not work.
#define WIFI_SSID  "Redmi 12C"
#define WIFI_PASS  "123456789"

// ThingSpeak channel "uv_sensor" (ID 3519777) -> API Keys tab -> Write API Key.
// Best to copy-paste it from the page rather than retyping (0 and O look alike).
#define TS_WRITE_KEY  "DCY60ZW24VQR2IH8"
