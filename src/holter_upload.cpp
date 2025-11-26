#include "holter_upload.h"
#include "aws_config.h"
#include "holter_capture.h"
#include <ArduinoJson.h>
#include <time.h>

// ============================================================================
// VARIABLES INTERNAS (PRIVADAS)
// ============================================================================

static WiFiClientSecure wifiClient;
static PubSubClient mqttClient(wifiClient);

// Estado
static UploadState currentState = UPLOAD_IDLE;
static String currentFilename = "";
static String uploadURL = "";
static bool urlReceived = false;
static String lastError = "";
static String currentSessionID = "";

// Timing
static unsigned long uploadStartTime = 0;
static const unsigned long UPLOAD_TIMEOUT_MS = 60000;

// NTP
static const char* ntpServer = "pool.ntp.org";
static const long gmtOffset_sec = -5 * 3600;
static const int daylightOffset_sec = 0;

// ============================================================================
// FUNCIONES INTERNAS (PRIVADAS)
// ============================================================================

static void syncTime() {
  Serial.println("[NTP] Sincronizando hora...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("[WARNING] No se pudo obtener hora NTP");
    return;
  }
  
  Serial.printf("[NTP] Hora sincronizada: %02d/%02d/%04d %02d:%02d:%02d\n",
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n[MQTT] ========== MENSAJE RECIBIDO ==========");
  Serial.println("[MQTT] Topic: " + String(topic));
  Serial.print("[MQTT] Payload: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.println("[ERROR] JSON parsing failed: " + String(error.c_str()));
    lastError = "JSON parse error";
    return;
  }
  
  Serial.println("[DEBUG] JSON parseado correctamente");
  
  if (String(topic) == TOPIC_RESPONSE) {
    Serial.println("[DEBUG] Topic coincide con TOPIC_RESPONSE");
    if (doc.containsKey("upload_url")) {
      uploadURL = doc["upload_url"].as<String>();
      urlReceived = true;
      Serial.println("[MQTT] URL recibida: " + uploadURL.substring(0, 50) + "...");
    } else {
      Serial.println("[WARNING] JSON no contiene 'upload_url'");
      serializeJsonPretty(doc, Serial);
      Serial.println();
      lastError = "No upload_url in response";
    }
  } else {
    Serial.println("[WARNING] Topic no coincide. Esperado: " + String(TOPIC_RESPONSE));
  }
  Serial.println("[MQTT] ==========================================\n");
}

static bool connectMQTT() {
  Serial.println("[MQTT] Configurando AWS IoT...");
  
  mqttClient.setBufferSize(4096);
  Serial.println("[DEBUG] Buffer MQTT configurado: 4096 bytes");
  
  mqttClient.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);
  
  Serial.println("[MQTT] Conectando a AWS IoT Core...");
  
  int attempts = 0;
  while (!mqttClient.connected() && attempts < 3) {
    if (mqttClient.connect(DEVICE_ID, NULL, NULL, NULL, 0, false, NULL, true)) {
      Serial.println("[MQTT] Conectado a AWS IoT Core");
      
      delay(100);
      if (!mqttClient.connected()) {
        Serial.println("[ERROR] Conexión perdida inmediatamente");
        attempts++;
        continue;
      }
      
      if (mqttClient.subscribe(TOPIC_RESPONSE, 1)) {
        Serial.println("[MQTT] Suscrito a: " + String(TOPIC_RESPONSE) + " (QoS 1)");
      } else {
        Serial.println("[ERROR] No se pudo suscribir a: " + String(TOPIC_RESPONSE));
        attempts++;
        continue;
      }
      
      Serial.println("[MQTT] Esperando confirmación de suscripción...");
      for (int i = 0; i < 20; i++) {
        mqttClient.loop();
        if (!mqttClient.connected()) {
          Serial.println("[ERROR] Conexión perdida durante suscripción");
          attempts++;
          break;
        }
        delay(50);
      }
      
      if (mqttClient.connected()) {
        Serial.println("[MQTT] Listo para recibir mensajes");
        return true;
      }
    } else {
      Serial.println("[MQTT] Error conectando: " + String(mqttClient.state()));
      lastError = "MQTT connect failed: " + String(mqttClient.state());
      attempts++;
      delay(2000);
    }
  }
  
  Serial.println("[MQTT] Falló después de 3 intentos");
  lastError = "MQTT connection failed after 3 attempts";
  return false;
}

static void requestUploadURL() {
  Serial.println("\n[UPLOAD] Solicitando URL de AWS...");
  
  unsigned long fileSize = 0;
  
  if (holter_isSDAvailable()) {
    File file = SD.open(currentFilename.c_str(), FILE_READ);
    if (!file) {
      Serial.println("[ERROR] No se pudo abrir archivo");
      lastError = "Cannot open file";
      currentState = UPLOAD_ERROR;
      return;
    }
    fileSize = file.size();
    file.close();
  } else {
    fileSize = 1024; // Simulado
    Serial.printf("[INFO] Tamaño simulado: %lu bytes\n", fileSize);
  }
  
  // Extraer session ID del filename
  int lastSlash = currentFilename.lastIndexOf('/');
  int lastDot = currentFilename.lastIndexOf('.');
  currentSessionID = currentFilename.substring(lastSlash + 1, lastDot);
  
  DynamicJsonDocument doc(512);
  doc["device_id"] = DEVICE_ID;
  doc["session_id"] = currentSessionID;
  doc["timestamp"] = String(millis() / 1000);
  doc["file_size"] = fileSize;
  doc["ready_for_upload"] = true;
  
  char jsonBuffer[512];
  size_t jsonSize = serializeJson(doc, jsonBuffer);
  
  Serial.println("[MQTT] Publicando solicitud...");
  Serial.println("[DEBUG] Topic: " + String(TOPIC_REQUEST));
  Serial.println("[DEBUG] Payload: " + String(jsonBuffer));
  
  mqttClient.loop();
  
  bool publishResult = mqttClient.publish(TOPIC_REQUEST, (uint8_t*)jsonBuffer, jsonSize);
  
  if (publishResult) {
    Serial.println("[MQTT] Solicitud enviada");
    Serial.println("[INFO] Esperando respuesta (60s timeout)...");
    
    uploadStartTime = millis();
    urlReceived = false;
    currentState = UPLOAD_REQUESTING_URL;
  } else {
    Serial.println("[ERROR] No se pudo publicar - Estado: " + String(mqttClient.state()));
    lastError = "MQTT publish failed";
    currentState = UPLOAD_ERROR;
  }
}

static bool uploadToS3() {
  Serial.println("\n[S3] Iniciando upload...");
  
  File file = SD.open(currentFilename.c_str(), FILE_READ);
  if (!file) {
    Serial.println("[ERROR] No se pudo abrir archivo");
    lastError = "Cannot open file for upload";
    return false;
  }
  
  unsigned long fileSize = file.size();
  Serial.println("[S3] Archivo: " + currentFilename);
  Serial.println("[S3] Tamaño: " + String(fileSize / 1024) + " KB");
  
  uint8_t* fileData = (uint8_t*)malloc(fileSize);
  if (!fileData) {
    Serial.println("[ERROR] No hay memoria suficiente");
    file.close();
    lastError = "Out of memory";
    return false;
  }
  
  Serial.println("[S3] Leyendo archivo...");
  size_t bytesRead = file.read(fileData, fileSize);
  file.close();
  
  if (bytesRead != fileSize) {
    Serial.println("[ERROR] Lectura incompleta");
    free(fileData);
    lastError = "Incomplete file read";
    return false;
  }
  
  Serial.println("[S3] Conectando a S3...");
  HTTPClient http;
  http.begin(uploadURL);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("Content-Length", String(fileSize));
  http.setTimeout(30000);
  
  Serial.println("[S3] Enviando datos...");
  int httpCode = http.PUT(fileData, fileSize);
  
  free(fileData);
  
  Serial.println("[S3] HTTP Code: " + String(httpCode));
  
  if (httpCode == 200 || httpCode == 204) {
    Serial.println("[S3] Upload exitoso!");
    http.end();
    
    if (SD.remove(currentFilename.c_str())) {
      Serial.println("[SD] Archivo eliminado (espacio liberado)");
    }
    
    return true;
  } else {
    Serial.println("[S3] Error HTTP: " + String(httpCode));
    String response = http.getString();
    Serial.println("[S3] Response: " + response);
    http.end();
    lastError = "S3 upload failed: " + String(httpCode);
    return false;
  }
}

// ============================================================================
// IMPLEMENTACIÓN DE INTERFACE PÚBLICA
// ============================================================================

void holter_initUpload() {
  wifiClient.setCACert(AWS_CERT_CA);
  wifiClient.setCertificate(AWS_CERT_CRT);
  wifiClient.setPrivateKey(AWS_CERT_PRIVATE);
  
  Serial.println("[Upload] Módulo inicializado");
}

bool holter_connectWiFi() {
  Serial.println("\n[WiFi] Conectando a: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected) {
    Serial.println("\n[WiFi] Conectado");
    Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
    Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
    syncTime();
  } else {
    Serial.println("\n[WiFi] ERROR: No se pudo conectar");
    lastError = "WiFi connection failed";
  }
  
  return connected;
}

void holter_disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WiFi] Desconectado (ahorro energía)");
}

bool holter_startUpload(String filename) {
  currentFilename = filename;
  currentState = UPLOAD_CONNECTING_WIFI;
  uploadStartTime = millis();
  urlReceived = false;
  lastError = "";
  uploadURL = "";
  
  Serial.println("[Upload] Iniciando proceso de upload para: " + filename);
  return true;
}

void holter_uploadLoop() {
  switch(currentState) {
    case UPLOAD_IDLE:
      // Nada que hacer
      break;
      
    case UPLOAD_CONNECTING_WIFI:
      if (holter_connectWiFi()) {
        currentState = UPLOAD_CONNECTING_MQTT;
      } else {
        currentState = UPLOAD_ERROR;
      }
      break;
      
    case UPLOAD_CONNECTING_MQTT:
      if (connectMQTT()) {
        requestUploadURL();
        // requestUploadURL cambia el estado
      } else {
        currentState = UPLOAD_ERROR;
      }
      break;
      
    case UPLOAD_REQUESTING_URL:
      mqttClient.loop();
      
      if (urlReceived) {
        currentState = UPLOAD_UPLOADING_S3;
      } else if (millis() - uploadStartTime > UPLOAD_TIMEOUT_MS) {
        Serial.println("[ERROR] Timeout esperando URL");
        lastError = "Timeout waiting for upload URL";
        currentState = UPLOAD_ERROR;
      }
      
      // Log cada 5 segundos
      static unsigned long lastLog = 0;
      if (millis() - lastLog > 5000) {
        Serial.printf("[WAIT] Esperando URL... (%lus)\n", (millis() - uploadStartTime) / 1000);
        lastLog = millis();
      }
      break;
      
    case UPLOAD_UPLOADING_S3:
      if (uploadToS3()) {
        Serial.println("\n========================================");
        Serial.println("UPLOAD COMPLETADO EXITOSAMENTE");
        Serial.println("========================================\n");
        currentState = UPLOAD_COMPLETE;
      } else {
        currentState = UPLOAD_ERROR;
      }
      break;
      
    case UPLOAD_COMPLETE:
    case UPLOAD_ERROR:
      // Estados finales, no hacer nada
      break;
  }
  
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

void holter_cancelUpload() {
  currentState = UPLOAD_IDLE;
  holter_disconnectWiFi();
  Serial.println("[Upload] Cancelado");
}

bool holter_isUploading() {
  return (currentState != UPLOAD_IDLE && 
          currentState != UPLOAD_COMPLETE && 
          currentState != UPLOAD_ERROR);
}

float holter_getUploadProgress() {
  switch(currentState) {
    case UPLOAD_IDLE: return 0.0;
    case UPLOAD_CONNECTING_WIFI: return 0.1;
    case UPLOAD_CONNECTING_MQTT: return 0.3;
    case UPLOAD_REQUESTING_URL: return 0.5;
    case UPLOAD_UPLOADING_S3: return 0.8;
    case UPLOAD_COMPLETE: return 1.0;
    case UPLOAD_ERROR: return 0.0;
    default: return 0.0;
  }
}

UploadState holter_getUploadState() {
  return currentState;
}

String holter_getUploadStateString() {
  switch(currentState) {
    case UPLOAD_IDLE: return "Idle";
    case UPLOAD_CONNECTING_WIFI: return "Conectando WiFi...";
    case UPLOAD_CONNECTING_MQTT: return "Conectando AWS...";
    case UPLOAD_REQUESTING_URL: return "Solicitando URL...";
    case UPLOAD_UPLOADING_S3: return "Subiendo a S3...";
    case UPLOAD_COMPLETE: return "Completado";
    case UPLOAD_ERROR: return "Error: " + lastError;
    default: return "Unknown";
  }
}

bool holter_isWiFiConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

bool holter_isMQTTConnected() {
  return mqttClient.connected();
}

String holter_getLastError() {
  return lastError;
}
