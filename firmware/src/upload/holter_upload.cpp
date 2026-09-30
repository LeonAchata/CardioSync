#include "holter_upload.h"
#include "aws_config.h"
#include "config.h"
#include "net/network.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <SD.h>
#include <WiFiClientSecure.h>
#include <time.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static WiFiClientSecure mqttTransport;
static PubSubClient mqttClient(mqttTransport);

static UploadState currentState = UPLOAD_IDLE;
static String currentFilename = "";
static String currentSessionID = "";
static String uploadURL = "";
static volatile bool urlReceived = false;
static String lastError = "";

static unsigned long requestSentTime = 0;

static const uint16_t MQTT_BUFFER_SIZE = 4096;  // Presigned URLs are ~1.5 KB

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void fail(const String& error) {
  Serial.println("[ERROR] " + error);
  lastError = error;
  currentState = UPLOAD_ERROR;
}

static String sessionIdFromPath(const String& path) {
  int lastSlash = path.lastIndexOf('/');
  int lastDot = path.lastIndexOf('.');
  if (lastDot <= lastSlash) lastDot = path.length();
  return path.substring(lastSlash + 1, lastDot);
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_RESPONSE) != 0) {
    Serial.println("[MQTT] Ignoring message on unexpected topic: " + String(topic));
    return;
  }

  // payload is mutable, so ArduinoJson parses it in place (zero-copy)
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.println("[MQTT] Invalid JSON: " + String(error.c_str()));
    return;
  }

  // Ignore late replies addressed to a previous session
  const char* session = doc["session_id"];
  if (session != nullptr && currentSessionID != session) {
    Serial.println("[MQTT] Ignoring reply for session " + String(session));
    return;
  }

  const char* url = doc["upload_url"];
  if (url == nullptr) {
    const char* message = doc["error"] | "no upload_url in response";
    lastError = String("Cloud rejected request: ") + message;
    Serial.println("[MQTT] " + lastError);
    return;
  }

  uploadURL = url;
  urlReceived = true;
  Serial.println("[MQTT] Upload URL received");
}

static bool connectMQTT() {
  if (mqttClient.connected()) return true;

  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);

  Serial.println("[MQTT] Connecting to AWS IoT Core...");

  for (int attempt = 1; attempt <= 3; attempt++) {
    // Client ID must match the IoT thing name allowed by the IoT policy
    if (mqttClient.connect(DEVICE_ID)) {
      if (mqttClient.subscribe(TOPIC_RESPONSE, 1)) {
        Serial.println("[MQTT] Connected, subscribed to " + String(TOPIC_RESPONSE));
        return true;
      }
      Serial.println("[MQTT] Subscribe failed");
      mqttClient.disconnect();
    } else {
      Serial.printf("[MQTT] Connect attempt %d failed (state %d)\n", attempt, mqttClient.state());
    }
    delay(2000);
  }

  lastError = "MQTT connection failed (state " + String(mqttClient.state()) + ")";
  return false;
}

static void requestUploadURL() {
  File file = SD.open(currentFilename.c_str(), FILE_READ);
  if (!file) {
    fail("Cannot open " + currentFilename);
    return;
  }
  size_t fileSize = file.size();
  file.close();

  time_t now = time(nullptr);

  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["session_id"] = currentSessionID;
  doc["timestamp"] = net_isClockValid() ? (uint32_t)now : 0;
  doc["file_size"] = fileSize;

  char jsonBuffer[256];
  size_t jsonSize = serializeJson(doc, jsonBuffer);

  Serial.println("[MQTT] Requesting upload URL: " + String(jsonBuffer));

  urlReceived = false;
  uploadURL = "";
  if (!mqttClient.publish(TOPIC_REQUEST, (const uint8_t*)jsonBuffer, jsonSize)) {
    fail("MQTT publish failed (state " + String(mqttClient.state()) + ")");
    return;
  }

  requestSentTime = millis();
  currentState = UPLOAD_REQUESTING_URL;
}

static bool uploadToS3() {
  File file = SD.open(currentFilename.c_str(), FILE_READ);
  if (!file) {
    lastError = "Cannot open file for upload";
    return false;
  }
  size_t fileSize = file.size();
  Serial.printf("[S3] Uploading %s (%u bytes)...\n", currentFilename.c_str(), (unsigned)fileSize);

  WiFiClientSecure s3Transport;
#ifdef S3_ROOT_CA
  s3Transport.setCACert(S3_ROOT_CA);
#else
  // Presigned URLs are signed, but without a CA the server identity is not
  // verified. Define S3_ROOT_CA in aws_config.h to enable verification.
  s3Transport.setInsecure();
#endif

  HTTPClient http;
  if (!http.begin(s3Transport, uploadURL)) {
    file.close();
    lastError = "Invalid upload URL";
    return false;
  }
  http.setTimeout(S3_HTTP_TIMEOUT_MS);
  // Must match the ContentType the presigned URL was signed with
  http.addHeader("Content-Type", "application/octet-stream");

  // Streams straight from the SD card, so file size is not limited by RAM
  int httpCode = http.sendRequest("PUT", &file, fileSize);
  file.close();

  if (httpCode == 200 || httpCode == 204) {
    http.end();
    Serial.println("[S3] Upload OK");
    if (SD.remove(currentFilename.c_str())) {
      Serial.println("[SD] Local file removed");
    }
    return true;
  }

  Serial.println("[S3] Response: " + http.getString());
  http.end();
  lastError = "S3 upload failed: HTTP " + String(httpCode);
  return false;
}

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

void holter_initUpload() {
  mqttTransport.setCACert(AWS_CERT_CA);
  mqttTransport.setCertificate(AWS_CERT_CRT);
  mqttTransport.setPrivateKey(AWS_CERT_PRIVATE);
  Serial.println("[Upload] Module initialised (device: " + String(DEVICE_ID) + ")");
}

bool holter_startUpload(const String& filename) {
  if (holter_isUploading()) return false;

  currentFilename = filename;
  currentSessionID = sessionIdFromPath(filename);
  uploadURL = "";
  urlReceived = false;
  lastError = "";
  currentState = UPLOAD_CONNECTING_WIFI;

  Serial.println("\n[Upload] Starting upload of " + filename);
  return true;
}

void holter_uploadLoop() {
  switch (currentState) {
    case UPLOAD_CONNECTING_WIFI:
      if (net_connectWiFi()) {
        currentState = UPLOAD_CONNECTING_MQTT;
      } else {
        fail("WiFi connection failed");
      }
      break;

    case UPLOAD_CONNECTING_MQTT:
      if (connectMQTT()) {
        requestUploadURL();  // Moves to REQUESTING_URL or ERROR
      } else {
        fail(lastError);
      }
      break;

    case UPLOAD_REQUESTING_URL: {
      mqttClient.loop();

      if (urlReceived) {
        currentState = UPLOAD_UPLOADING_S3;
      } else if (!mqttClient.connected()) {
        fail("MQTT connection lost while waiting for URL");
      } else if (millis() - requestSentTime > UPLOAD_URL_TIMEOUT_MS) {
        fail(lastError.length() > 0 ? lastError : "Timeout waiting for upload URL");
      }

      static unsigned long lastLog = 0;
      if (currentState == UPLOAD_REQUESTING_URL && millis() - lastLog > 5000) {
        Serial.printf("[WAIT] Waiting for URL... (%lus)\n", (millis() - requestSentTime) / 1000);
        lastLog = millis();
      }
      break;
    }

    case UPLOAD_UPLOADING_S3:
      if (uploadToS3()) {
        currentState = UPLOAD_COMPLETE;
      } else {
        fail(lastError);
      }
      break;

    case UPLOAD_IDLE:
    case UPLOAD_COMPLETE:
    case UPLOAD_ERROR:
      break;
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

void holter_cancelUpload() {
  currentState = UPLOAD_IDLE;
  holter_disconnectMQTT();
  Serial.println("[Upload] Cancelled");
}

void holter_disconnectMQTT() {
  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
}

bool holter_isUploading() {
  return currentState != UPLOAD_IDLE &&
         currentState != UPLOAD_COMPLETE &&
         currentState != UPLOAD_ERROR;
}

float holter_getUploadProgress() {
  switch (currentState) {
    case UPLOAD_CONNECTING_WIFI: return 0.1f;
    case UPLOAD_CONNECTING_MQTT: return 0.3f;
    case UPLOAD_REQUESTING_URL:  return 0.5f;
    case UPLOAD_UPLOADING_S3:    return 0.8f;
    case UPLOAD_COMPLETE:        return 1.0f;
    default:                     return 0.0f;
  }
}

UploadState holter_getUploadState() {
  return currentState;
}

String holter_getUploadStateString() {
  switch (currentState) {
    case UPLOAD_IDLE:            return "Idle";
    case UPLOAD_CONNECTING_WIFI: return "Connecting WiFi...";
    case UPLOAD_CONNECTING_MQTT: return "Connecting AWS IoT...";
    case UPLOAD_REQUESTING_URL:  return "Requesting URL...";
    case UPLOAD_UPLOADING_S3:    return "Uploading to S3...";
    case UPLOAD_COMPLETE:        return "Complete";
    case UPLOAD_ERROR:           return "Error: " + lastError;
    default:                     return "Unknown";
  }
}

bool holter_isMQTTConnected() {
  return mqttClient.connected();
}

String holter_getLastError() {
  return lastError;
}
