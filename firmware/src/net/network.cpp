#include "network.h"
#include "config.h"

#include <WiFi.h>
#include <time.h>

// WiFi credentials live in wifi_config.h. Older setups kept them in
// aws_config.h, which is still accepted as a fallback.
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#include "aws_config.h"
#endif

#if !defined(WIFI_SSID) || !defined(WIFI_PASSWORD)
#error "Define WIFI_SSID and WIFI_PASSWORD in include/wifi_config.h (see wifi_config.example.h)"
#endif

static const time_t MIN_VALID_EPOCH = 1600000000; // Sep 2020

bool net_connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("\n[WiFi] Connecting to: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] ERROR: connection timed out");
    return false;
  }

  Serial.println("[WiFi] Connected - IP: " + WiFi.localIP().toString() +
                 " | RSSI: " + String(WiFi.RSSI()) + " dBm");
  return true;
}

void net_disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WiFi] Disconnected (power saving)");
}

bool net_isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool net_isClockValid() {
  return time(nullptr) >= MIN_VALID_EPOCH;
}

bool net_syncClock() {
  Serial.println("[NTP] Synchronising clock...");
  configTzTime(TZ_INFO, NTP_SERVER);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, NTP_SYNC_TIMEOUT_MS) || !net_isClockValid()) {
    Serial.println("[WARNING] NTP sync failed");
    return false;
  }

  Serial.printf("[NTP] Clock synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return true;
}
