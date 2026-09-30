#ifndef CARDIOSYNC_NETWORK_H
#define CARDIOSYNC_NETWORK_H

#include <Arduino.h>

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

/**
 * Connects to the configured WiFi network (no-op if already connected).
 * @return true once connected, false on timeout
 */
bool net_connectWiFi();

/**
 * Disconnects WiFi and powers the radio down (less noise on the ECG, less power).
 */
void net_disconnectWiFi();

/**
 * @return true if WiFi is connected
 */
bool net_isWiFiConnected();

/**
 * Synchronises the RTC with NTP. Requires an active WiFi connection.
 * The ESP32 keeps the time across WiFi disconnects (until reset).
 * @return true if the clock holds a valid time afterwards
 */
bool net_syncClock();

/**
 * @return true if the RTC holds a plausible Unix time
 */
bool net_isClockValid();

#endif // CARDIOSYNC_NETWORK_H
