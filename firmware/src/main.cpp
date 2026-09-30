#include <Arduino.h>
#include <XSpaceBioV10.h>

#include "config.h"
#include "capture/holter_capture.h"
#include "net/network.h"
#include "upload/holter_upload.h"

#ifdef CARDIOSYNC_ENABLE_DISPLAY
#include "ui/display_ui.h"
#endif

// ============================================================================
// MAIN OBJECTS
// ============================================================================

XSpaceBioV10Board MyBioBoard;

// ============================================================================
// SYSTEM STATES
// ============================================================================

enum SystemState {
  STATE_WAITING,     // Pause between sessions
  STATE_CAPTURING,   // Recording ECG to the SD card
  STATE_UPLOADING,   // Uploading the current file (and backlog) to S3
  STATE_ERROR        // Unrecoverable error, reboot after a delay
};

static SystemState currentState = STATE_WAITING;
static unsigned long nextCaptureAt = 0;

// Upload queue: the file just captured plus older files left on the SD
static String uploadQueue[MAX_UPLOADS_PER_CYCLE];
static size_t uploadQueueLen = 0;
static size_t uploadQueueIdx = 0;
static size_t uploadsOk = 0;

// ============================================================================
// OPTIONAL OLED UI (build with -D CARDIOSYNC_ENABLE_DISPLAY)
// Safe to redraw during capture: sampling runs in its own task.
// ============================================================================

static void ui_init() {
#ifdef CARDIOSYNC_ENABLE_DISPLAY
  display_init(&MyBioBoard);
#endif
}

static void ui_update(float progress) {
#ifdef CARDIOSYNC_ENABLE_DISPLAY
  display_setProgress(progress);
  display_update();
#else
  (void)progress;
#endif
}

enum UiScreen { UI_IDLE, UI_CAPTURING, UI_UPLOADING };

static void ui_setScreen(UiScreen screen) {
#ifdef CARDIOSYNC_ENABLE_DISPLAY
  switch (screen) {
    case UI_IDLE:      display_setMode(DISP_IDLE); break;
    case UI_CAPTURING: display_setMode(DISP_CAPTURING); break;
    case UI_UPLOADING: display_setMode(DISP_UPLOADING); break;
  }
#else
  (void)screen;
#endif
}

static void ui_showText(const String& text) {
#ifdef CARDIOSYNC_ENABLE_DISPLAY
  display_setText(text);
#else
  (void)text;
#endif
}

static void ui_showError(const String& error) {
#ifdef CARDIOSYNC_ENABLE_DISPLAY
  display_showError(error);
  display_update();
#else
  (void)error;
#endif
}

// ============================================================================
// STATE HELPERS
// ============================================================================

static void enterError(const String& reason) {
  Serial.println("[ERROR] " + reason);
  ui_showError(reason);
  currentState = STATE_ERROR;
}

static void scheduleNextCapture(unsigned long delayMs) {
  nextCaptureAt = millis() + delayMs;
  currentState = STATE_WAITING;
  ui_setScreen(UI_IDLE);
}

static void syncClockAtBoot() {
  if (net_connectWiFi()) {
    net_syncClock();
  }
  net_disconnectWiFi();

  if (!net_isClockValid()) {
    Serial.println("[WARNING] Clock not synced - session files will use a boot counter");
  }
}

static void buildUploadQueue(const String& justCaptured) {
  uploadQueueLen = 0;
  uploadQueueIdx = 0;
  uploadsOk = 0;

  if (justCaptured.length() > 0) {
    uploadQueue[uploadQueueLen++] = justCaptured;
  }

  // Backlog from previous cycles whose upload failed
  String pending[MAX_UPLOADS_PER_CYCLE];
  size_t found = holter_listPendingFiles(pending, MAX_UPLOADS_PER_CYCLE);
  for (size_t i = 0; i < found && uploadQueueLen < MAX_UPLOADS_PER_CYCLE; i++) {
    if (pending[i] != justCaptured) {
      uploadQueue[uploadQueueLen++] = pending[i];
    }
  }

  if (uploadQueueLen > 1) {
    Serial.printf("[SYSTEM] %u file(s) queued for upload (incl. backlog)\n", (unsigned)uploadQueueLen);
  }
}

static void finishUploadCycle() {
  holter_disconnectMQTT();

  // Retry NTP opportunistically if the boot-time sync failed
  if (!net_isClockValid() && net_isWiFiConnected()) {
    net_syncClock();
  }
  net_disconnectWiFi();

  Serial.println("\n========================================");
  Serial.printf("CYCLE DONE: %u/%u file(s) uploaded\n", (unsigned)uploadsOk, (unsigned)uploadQueueLen);
  Serial.printf("Next capture in %u s\n", (unsigned)PAUSE_BETWEEN_SESSIONS_SEC);
  Serial.println("========================================\n");

  ui_showText(String(uploadsOk) + "/" + String(uploadQueueLen) + " uploaded");
  scheduleNextCapture(PAUSE_BETWEEN_SESSIONS_SEC * 1000UL);
}

static void startNextUpload() {
  while (uploadQueueIdx < uploadQueueLen) {
    if (holter_startUpload(uploadQueue[uploadQueueIdx])) {
      currentState = STATE_UPLOADING;
      ui_setScreen(UI_UPLOADING);
      return;
    }
    uploadQueueIdx++;
  }
  finishUploadCycle();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================");
  Serial.println("CARDIOSYNC - ESP32 HOLTER ECG");
  Serial.println("========================================");
  Serial.printf("[INFO] 3-lead ECG @ %u Hz, %u s sessions\n",
                (unsigned)ECG_SAMPLE_RATE_HZ, (unsigned)CAPTURE_DURATION_SEC);
  Serial.println("[INFO] Capture -> SD -> AWS IoT Core -> S3");
  Serial.println("========================================\n");

  ui_init();
  holter_init(&MyBioBoard);
  holter_initUpload();

  if (!holter_isSDAvailable()) {
    Serial.println("[INFO] Check that the SD card is inserted, FAT32 formatted and wired to SPI");
    enterError("SD card not available");
    return;
  }

  syncClockAtBoot();

  Serial.println("[SYSTEM] Ready\n");
  scheduleNextCapture(0);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  switch (currentState) {

    case STATE_WAITING: {
      if ((long)(millis() - nextCaptureAt) < 0) break;

      if (holter_startCapture()) {
        currentState = STATE_CAPTURING;
        ui_setScreen(UI_CAPTURING);
      } else {
        enterError("Could not start capture");
      }
      break;
    }

    case STATE_CAPTURING: {
      holter_captureLoop();
      ui_update(holter_getProgress());

      if (!holter_isCapturing()) {
        if (holter_getECGSampleCount() == 0) {
          enterError("Capture produced no samples");
          break;
        }
        buildUploadQueue(holter_getCurrentFile());
        startNextUpload();
      }
      break;
    }

    case STATE_UPLOADING: {
      holter_uploadLoop();
      ui_update(holter_getUploadProgress());

      static unsigned long lastStatusLog = 0;
      if (millis() - lastStatusLog > 5000) {
        Serial.printf("[STATUS] %s (%.0f%%)\n", holter_getUploadStateString().c_str(),
                      holter_getUploadProgress() * 100);
        lastStatusLog = millis();
      }

      if (holter_isUploading()) break;

      if (holter_getUploadState() == UPLOAD_COMPLETE) {
        uploadsOk++;
        uploadQueueIdx++;
        startNextUpload();
      } else {
        // The file stays on the SD card and is retried next cycle
        Serial.println("[UPLOAD] Failed: " + holter_getLastError());
        finishUploadCycle();
      }
      break;
    }

    case STATE_ERROR: {
      holter_disconnectMQTT();
      if (net_isWiFiConnected()) {
        net_disconnectWiFi();
      }

      Serial.printf("\n[SYSTEM] Rebooting in %u s to recover...\n", (unsigned)ERROR_REBOOT_DELAY_SEC);
      delay(ERROR_REBOOT_DELAY_SEC * 1000UL);
      ESP.restart();
      break;
    }
  }
}
