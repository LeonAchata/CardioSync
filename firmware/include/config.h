#ifndef CARDIOSYNC_CONFIG_H
#define CARDIOSYNC_CONFIG_H

#include <stdint.h>

// ============================================================================
// CAPTURE
// ============================================================================

// Length of each recording session
constexpr uint32_t CAPTURE_DURATION_SEC = 15;

// ECG sampling rate. Must divide 1000 evenly (FreeRTOS tick = 1 ms)
constexpr uint32_t ECG_SAMPLE_RATE_HZ = 250;

// Pause between the end of an upload and the start of the next capture
constexpr uint32_t PAUSE_BETWEEN_SESSIONS_SEC = 10;

// Delay before a reboot when the system lands in the error state
constexpr uint32_t ERROR_REBOOT_DELAY_SEC = 30;

// Maximum number of files uploaded per cycle (current + backlog on the SD)
constexpr uint32_t MAX_UPLOADS_PER_CYCLE = 5;

// ============================================================================
// SIGNAL CONDITIONING (AD8232)
// ============================================================================

constexpr float AD8232_OFFSET_V = 1.65f;   // Virtual ground of the AD8232
constexpr float AD8232_GAIN = 1100.0f;     // Total gain of the AD8232 front-end
constexpr float ECG_SCALE_FACTOR = 6553.6f; // mV -> int16 (±5 mV full scale)

// ============================================================================
// HARDWARE PINS (XSpace Bio V1.0)
// ============================================================================

// NOTE: GPIO5 is also PWDN of the on-board AFE4490. The AFE4490 is never
// initialised by this firmware so the pin is free for the SD card.
constexpr int SD_CS_PIN = 5;
constexpr int SD_MOSI_PIN = 23;
constexpr int SD_MISO_PIN = 19;
constexpr int SD_SCK_PIN = 18;
constexpr uint32_t SD_SPI_FREQ_HZ = 4000000;

// ============================================================================
// NETWORK / TIME
// ============================================================================

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t UPLOAD_URL_TIMEOUT_MS = 60000;
constexpr uint32_t S3_HTTP_TIMEOUT_MS = 30000;

#define NTP_SERVER "pool.ntp.org"
// POSIX TZ string, only used for human-readable logs (Peru, UTC-5)
#define TZ_INFO "<-05>5"

#endif // CARDIOSYNC_CONFIG_H
