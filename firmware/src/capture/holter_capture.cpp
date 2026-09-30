#include "holter_capture.h"
#include "config.h"
#include "ecg_format.h"

#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

static_assert(1000 % ECG_SAMPLE_RATE_HZ == 0,
              "ECG_SAMPLE_RATE_HZ must divide 1000 (1 ms FreeRTOS tick)");

// ============================================================================
// INTERNAL STATE
// ============================================================================

static XSpaceBioV10Board* g_bioBoard = nullptr;

static const size_t WRITE_BUFFER_SIZE = 8192;
static const size_t SAMPLE_QUEUE_LEN = 1024;           // ~4 s of margin @ 250 Hz
static const uint32_t PERIODIC_FLUSH_MS = 2000;
static const uint32_t TOTAL_SAMPLES = CAPTURE_DURATION_SEC * ECG_SAMPLE_RATE_HZ;
static const time_t MIN_VALID_EPOCH = 1600000000;       // Sep 2020

static bool sdAvailable = false;
static bool isCapturing = false;

static File dataFile;
static String currentSessionFile = "";

static unsigned long captureStartTime = 0;
static unsigned long samplesWritten = 0;

// Sampler task <-> writer shared state
static QueueHandle_t sampleQueue = nullptr;
static volatile bool samplerFinished = true;
static volatile bool abortRequested = false;
static volatile uint32_t droppedSamples = 0;

// Write buffer
static uint8_t writeBuffer[WRITE_BUFFER_SIZE];
static size_t bufferIndex = 0;
static unsigned long lastFlush = 0;

// ============================================================================
// SAMPLING (runs in its own task)
// ============================================================================

static int16_t toFixedPoint(float mV) {
  float scaled = mV * ECG_SCALE_FACTOR;
  if (scaled > INT16_MAX) return INT16_MAX;
  if (scaled < INT16_MIN) return INT16_MIN;
  return (int16_t)lroundf(scaled);
}

static ECGSample readECGSample() {
  float voltageI = g_bioBoard->AD8232_GetVoltage(AD8232_XS1);
  float voltageII = g_bioBoard->AD8232_GetVoltage(AD8232_XS2);

  float ecgI_mV = ((voltageI - AD8232_OFFSET_V) * 1000.0f) / AD8232_GAIN;
  float ecgII_mV = ((voltageII - AD8232_OFFSET_V) * 1000.0f) / AD8232_GAIN;
  float ecgIII_mV = ecgII_mV - ecgI_mV;  // Einthoven: III = II - I

  ECGSample sample;
  sample.derivation_I = toFixedPoint(ecgI_mV);
  sample.derivation_II = toFixedPoint(ecgII_mV);
  sample.derivation_III = toFixedPoint(ecgIII_mV);
  return sample;
}

static void samplerTask(void*) {
  const TickType_t period = pdMS_TO_TICKS(1000 / ECG_SAMPLE_RATE_HZ);
  TickType_t lastWake = xTaskGetTickCount();

  for (uint32_t i = 0; i < TOTAL_SAMPLES && !abortRequested; i++) {
    vTaskDelayUntil(&lastWake, period);
    ECGSample sample = readECGSample();
    if (xQueueSend(sampleQueue, &sample, 0) != pdTRUE) {
      droppedSamples++;
    }
  }

  samplerFinished = true;
  vTaskDelete(nullptr);
}

// ============================================================================
// SD HELPERS
// ============================================================================

static void flushBuffer() {
  if (bufferIndex == 0) return;

  if (!dataFile) {
    Serial.println("[ERROR] Data file is not open");
    bufferIndex = 0;
    return;
  }

  size_t written = dataFile.write(writeBuffer, bufferIndex);
  if (written != bufferIndex) {
    Serial.printf("[ERROR] SD write failed: %u/%u bytes\n",
                  (unsigned)written, (unsigned)bufferIndex);
  }
  bufferIndex = 0;
}

static void writeToBuffer(const uint8_t* data, size_t len) {
  if (bufferIndex + len > WRITE_BUFFER_SIZE) {
    flushBuffer();
  }
  memcpy(writeBuffer + bufferIndex, data, len);
  bufferIndex += len;
}

static void drainQueue() {
  ECGSample sample;
  while (xQueueReceive(sampleQueue, &sample, 0) == pdTRUE) {
    writeToBuffer((const uint8_t*)&sample, sizeof(ECGSample));
    samplesWritten++;
  }
}

static bool mountSD() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  for (int attempt = 0; attempt < 5; attempt++) {
    if (attempt > 0) {
      Serial.printf("[SD] Retry %d...\n", attempt);
      SD.end();
      delay(500);
    }
    if (SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQ_HZ) && SD.cardType() != CARD_NONE) {
      return true;
    }
  }
  return false;
}

static void logCardInfo() {
  const char* type = "UNKNOWN";
  switch (SD.cardType()) {
    case CARD_MMC:  type = "MMC"; break;
    case CARD_SD:   type = "SDSC"; break;
    case CARD_SDHC: type = "SDHC"; break;
    default: break;
  }
  Serial.printf("[SD] Type: %s | Size: %lluMB | Used: %lluMB / %lluMB\n", type,
                SD.cardSize() / (1024 * 1024), SD.usedBytes() / (1024 * 1024),
                SD.totalBytes() / (1024 * 1024));
}

// Unique session id: Unix time when the clock is valid, NVS counter otherwise
static uint32_t nextSessionId(String& name) {
  time_t now = time(nullptr);
  if (now >= MIN_VALID_EPOCH) {
    name = "session_" + String((uint32_t)now);
    return (uint32_t)now;
  }

  Preferences prefs;
  prefs.begin("cardiosync", false);
  uint32_t counter = prefs.getUInt("session", 0) + 1;
  prefs.putUInt("session", counter);
  prefs.end();

  name = "session_boot" + String(counter);
  return counter;
}

static void finalizeFile() {
  drainQueue();
  flushBuffer();

  if (!dataFile) return;

  // Patch the sample counters in the header now that they are known
  uint32_t ecgCount = samplesWritten;
  uint32_t imuCount = 0;
  bool headerOk = dataFile.seek(offsetof(FileHeader, num_ecg_samples)) &&
                  dataFile.write((const uint8_t*)&ecgCount, sizeof(ecgCount)) == sizeof(ecgCount) &&
                  dataFile.write((const uint8_t*)&imuCount, sizeof(imuCount)) == sizeof(imuCount);

  size_t fileSize = dataFile.size();
  dataFile.close();

  size_t expectedSize = sizeof(FileHeader) + samplesWritten * sizeof(ECGSample);
  float realRate = (float)samplesWritten / (float)CAPTURE_DURATION_SEC;

  Serial.println("\n========================================");
  Serial.println("CAPTURE FINISHED");
  Serial.println("========================================");
  Serial.printf("[INFO] File: %s\n", currentSessionFile.c_str());
  Serial.printf("[INFO] Size: %u bytes (expected %u)\n", (unsigned)fileSize, (unsigned)expectedSize);
  Serial.printf("[INFO] ECG samples: %lu (%.1f Hz)\n", samplesWritten, realRate);
  if (droppedSamples > 0) {
    Serial.printf("[WARNING] Dropped samples: %u\n", (unsigned)droppedSamples);
  }
  Serial.println(headerOk ? "[OK] Header updated" : "[ERROR] Could not update header counters");
  Serial.println("========================================\n");
}

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

void holter_init(XSpaceBioV10Board* bioBoard) {
  g_bioBoard = bioBoard;
  Serial.println("[INIT] Initialising capture module...");

  // Both AD8232 front-ends must be configured and woken up; otherwise
  // AD8232_GetVoltage() reads an unconfigured pin instead of the ECG output.
  g_bioBoard->AD8232_init(AD8232_XS1, SDN_1, LOH_1, 0, ECG_OUT_1);
  g_bioBoard->AD8232_init(AD8232_XS2, SDN_2, LOH_2, 0, ECG_OUT_2);
  g_bioBoard->AD8232_Wake(AD8232_XS1);
  g_bioBoard->AD8232_Wake(AD8232_XS2);
  Serial.println("[INIT] AD8232 front-ends enabled");

  if (sampleQueue == nullptr) {
    sampleQueue = xQueueCreate(SAMPLE_QUEUE_LEN, sizeof(ECGSample));
  }

  Serial.printf("[INIT] SD pins - CS:%d MOSI:%d MISO:%d SCK:%d\n",
                SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN);
  sdAvailable = mountSD();
  if (sdAvailable) {
    Serial.println("[SD] Mounted");
    logCardInfo();
  } else {
    Serial.println("[ERROR] SD card not available");
  }
}

bool holter_startCapture() {
  if (isCapturing) return false;

  if (!sdAvailable || SD.cardType() == CARD_NONE) {
    Serial.println("[ERROR] SD card not available - cannot capture");
    sdAvailable = false;
    return false;
  }
  if (sampleQueue == nullptr) {
    Serial.println("[ERROR] Sample queue could not be allocated");
    return false;
  }

  String sessionName;
  uint32_t sessionId = nextSessionId(sessionName);
  currentSessionFile = "/" + sessionName + ".bin";

  Serial.println("\n========================================");
  Serial.println("STARTING CAPTURE");
  Serial.println("========================================");
  Serial.println("[INFO] File: " + currentSessionFile);
  Serial.printf("[INFO] Duration: %us @ %u Hz\n",
                (unsigned)CAPTURE_DURATION_SEC, (unsigned)ECG_SAMPLE_RATE_HZ);

  dataFile = SD.open(currentSessionFile.c_str(), FILE_WRITE);
  if (!dataFile) {
    Serial.println("[ERROR] Could not create file on SD");
    return false;
  }

  time_t now = time(nullptr);
  FileHeader header = {};
  header.magic = ECG_FILE_MAGIC;
  header.version = ECG_FILE_VERSION;
  header.device_id = 1;
  header.session_id = sessionId;
  header.timestamp_start = now >= MIN_VALID_EPOCH ? (uint32_t)now : 0;
  header.ecg_sample_rate = ECG_SAMPLE_RATE_HZ;
  header.imu_sample_rate = 0;
  header.num_ecg_samples = 0;  // Patched in finalizeFile()
  header.num_imu_samples = 0;

  if (dataFile.write((const uint8_t*)&header, sizeof(header)) != sizeof(header)) {
    Serial.println("[ERROR] Could not write file header");
    dataFile.close();
    return false;
  }
  dataFile.flush();

  samplesWritten = 0;
  droppedSamples = 0;
  bufferIndex = 0;
  abortRequested = false;
  samplerFinished = false;
  xQueueReset(sampleQueue);

  captureStartTime = millis();
  lastFlush = captureStartTime;

  // Core 0 is idle during capture (WiFi is off), keep loop() on core 1
  BaseType_t created = xTaskCreatePinnedToCore(samplerTask, "ecg_sampler", 4096, nullptr,
                                               5, nullptr, 0);
  if (created != pdPASS) {
    Serial.println("[ERROR] Could not start sampler task");
    samplerFinished = true;
    dataFile.close();
    return false;
  }

  isCapturing = true;
  Serial.println("[CAPTURE] Recording...\n");
  return true;
}

void holter_captureLoop() {
  if (!isCapturing) return;

  drainQueue();

  if (samplerFinished && uxQueueMessagesWaiting(sampleQueue) == 0) {
    isCapturing = false;
    finalizeFile();
    return;
  }

  // Commit to the card periodically so a power loss costs at most ~2 s of data
  if (millis() - lastFlush >= PERIODIC_FLUSH_MS) {
    flushBuffer();
    dataFile.flush();
    lastFlush = millis();
  }

  static unsigned long lastReport = 0;
  unsigned long elapsed = holter_getElapsedSeconds();
  if (elapsed > 0 && elapsed % 3 == 0 && elapsed != lastReport) {
    lastReport = elapsed;
    Serial.printf("[PROGRESS] %lus/%us | ECG: %lu samples\n",
                  elapsed, (unsigned)CAPTURE_DURATION_SEC, samplesWritten);
  }
}

void holter_stopCapture() {
  if (!isCapturing) return;

  Serial.println("\n[CAPTURE] Stopping capture...");
  abortRequested = true;

  // Wait for the sampler task to exit before touching the queue
  unsigned long waitStart = millis();
  while (!samplerFinished && millis() - waitStart < 100) {
    delay(1);
  }

  isCapturing = false;
  finalizeFile();
}

bool holter_isCapturing() {
  return isCapturing;
}

float holter_getProgress() {
  if (!isCapturing) return 0.0f;
  return constrain((float)samplesWritten / (float)TOTAL_SAMPLES, 0.0f, 1.0f);
}

unsigned long holter_getElapsedSeconds() {
  if (!isCapturing) return 0;
  return (millis() - captureStartTime) / 1000;
}

String holter_getCurrentFile() {
  return currentSessionFile;
}

unsigned long holter_getECGSampleCount() {
  return samplesWritten;
}

unsigned long holter_getDroppedSampleCount() {
  return droppedSamples;
}

bool holter_isSDAvailable() {
  return sdAvailable;
}

size_t holter_listPendingFiles(String* out, size_t max) {
  if (!sdAvailable || max == 0) return 0;

  File root = SD.open("/");
  if (!root || !root.isDirectory()) return 0;

  size_t count = 0;
  File entry = root.openNextFile();
  while (entry && count < max) {
    String name = entry.name();
    bool isSession = !entry.isDirectory() && name.startsWith("session_") && name.endsWith(".bin");
    bool hasData = entry.size() > sizeof(FileHeader);
    String path = "/" + name;

    if (isSession && hasData && !(isCapturing && path == currentSessionFile)) {
      out[count++] = path;
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return count;
}
