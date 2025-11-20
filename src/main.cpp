#include <Arduino.h>
#include <XSpaceBioV10.h>
#include <Wire.h>
#include <Adafruit_ADXL345_U.h>
#include <SD.h>
#include <FS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "aws_config.h"

// ============================================================================
// OBJETOS PRINCIPALES
// ============================================================================
XSpaceBioV10Board MyBioBoard;
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================================
// CONFIGURACIÓN SISTEMA
// ============================================================================
const int CAPTURE_DURATION_SEC = 10;
const int ECG_SAMPLE_RATE_HZ = 100;
const int IMU_SAMPLE_RATE_HZ = 100;
const unsigned long BAUD_RATE = 115200;
const unsigned long MAX_ECG_SAMPLES = (unsigned long)ECG_SAMPLE_RATE_HZ * CAPTURE_DURATION_SEC;
const unsigned long MAX_IMU_SAMPLES = (unsigned long)IMU_SAMPLE_RATE_HZ * CAPTURE_DURATION_SEC;

#define SD_CS_PIN 5

// Escalado para conversión float->int16
// Rango ECG típico: ±5 mV → Resolución: 5mV/32768 = 0.15 uV
const float ECG_SCALE_FACTOR = 6553.6;  // 32768 / 5.0 mV

// ============================================================================
// ESTADOS DEL SISTEMA
// ============================================================================
enum SystemState {
  STATE_INIT,
  STATE_CAPTURING,
  STATE_UPLOAD_REQUEST,
  STATE_UPLOADING,
  STATE_COMPLETE,
  STATE_ERROR
};

SystemState currentState = STATE_INIT;

// ============================================================================
// ESTRUCTURA DE ARCHIVO BINARIO
// ============================================================================
struct FileHeader {
  uint32_t magic;              // 0x45434744 = "ECGD" (ECGData)
  uint16_t version;
  uint16_t device_id;
  uint32_t session_id;
  uint32_t timestamp_start;
  uint16_t ecg_sample_rate;
  uint16_t imu_sample_rate;
  uint32_t num_ecg_samples;
  uint32_t num_imu_samples;
  uint8_t reserved[4];
} __attribute__((packed));

// Estructura para muestra ECG (int16 en lugar de float)
struct ECGSample {
  int16_t derivation_I;
  int16_t derivation_II;
  int16_t derivation_III;
} __attribute__((packed));

// Estructura IMU (6 ejes)
struct IMUSample {
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
  int16_t gyro_x;
  int16_t gyro_y;
  int16_t gyro_z;
} __attribute__((packed));

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
File dataFile;
String currentSessionFile = "";
String currentSessionGzFile = "";
String currentSessionID = "";
unsigned long captureStartTime = 0;
unsigned long sampleCount = 0;
unsigned long imuSampleCount = 0;
bool isCapturing = false;
bool imuAvailable = false;
bool sdAvailable = false;

// Timing
unsigned long lastECGSample = 0;
unsigned long lastIMUSample = 0;
const unsigned long ECG_INTERVAL_US = 1000000 / ECG_SAMPLE_RATE_HZ;
const unsigned long IMU_INTERVAL_US = 1000000 / IMU_SAMPLE_RATE_HZ;

// Buffer
const int BUFFER_SIZE = 512;
uint8_t writeBuffer[BUFFER_SIZE];
int bufferIndex = 0;

// Upload
String uploadURL = "";
bool urlReceived = false;
unsigned long uploadStartTime = 0;
const unsigned long UPLOAD_TIMEOUT_MS = 30000; // milisegundos

// ============================================================================
// FUNCIONES WIFI
// ============================================================================
void connectWiFi() {
  Serial.println("\n[WiFi] Conectando a: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Conectado");
    Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
    Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
  } else {
    Serial.println("\n[WiFi] ERROR: No se pudo conectar");
    currentState = STATE_ERROR;
  }
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WiFi] Desconectado (ahorro energía)");
}

// ============================================================================
// FUNCIONES MQTT
// ============================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n[MQTT] ========== MENSAJE RECIBIDO ==========");
  Serial.println("[MQTT] Topic: " + String(topic));
  Serial.print("[MQTT] Payload: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  
  // Parsear JSON
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.println("[ERROR] JSON parsing failed: " + String(error.c_str()));
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
    }
  } else {
    Serial.println("[WARNING] Topic no coincide. Esperado: " + String(TOPIC_RESPONSE));
  }
  Serial.println("[MQTT] ==========================================\n");
}

bool connectMQTT() {
  Serial.println("[MQTT] Configurando AWS IoT...");
  
  // Certificados
  wifiClient.setCACert(AWS_CERT_CA);
  wifiClient.setCertificate(AWS_CERT_CRT);
  wifiClient.setPrivateKey(AWS_CERT_PRIVATE);
  
  // IMPORTANTE: setBufferSize ANTES de setServer
  mqttClient.setBufferSize(4096);
  Serial.println("[DEBUG] Buffer MQTT configurado: 4096 bytes");
  
  mqttClient.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);  // 60 segundos keepAlive
  
  Serial.println("[MQTT] Conectando a AWS IoT Core...");
  Serial.println("[DEBUG] KeepAlive: 60s");
  
  int attempts = 0;
  while (!mqttClient.connected() && attempts < 3) {
    // Conectar con clean session y último will
    if (mqttClient.connect(DEVICE_ID, NULL, NULL, NULL, 0, false, NULL, true)) {
      Serial.println("[MQTT] Conectado a AWS IoT Core");
      
      // Verificar que sigue conectado
      delay(100);
      if (!mqttClient.connected()) {
        Serial.println("[ERROR] Conexión perdida inmediatamente");
        attempts++;
        continue;
      }
      
      // Suscribirse al topic específico con QoS 1
      if (mqttClient.subscribe(TOPIC_RESPONSE, 1)) {
        Serial.println("[MQTT] Suscrito a: " + String(TOPIC_RESPONSE) + " (QoS 1)");
      } else {
        Serial.println("[ERROR] No se pudo suscribir a: " + String(TOPIC_RESPONSE));
        attempts++;
        continue;
      }
      
      // Mantener conexión activa mientras esperamos
      Serial.println("[MQTT] Esperando confirmación de suscripción...");
      for (int i = 0; i < 20; i++) {
        mqttClient.loop();  // Mantener keepAlive
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
      attempts++;
      delay(2000);
    }
  }
  
  Serial.println("[MQTT] Falló después de 3 intentos");
  return false;
}

void requestUploadURL() {
  Serial.println("\n[UPLOAD] Solicitando URL de AWS...");
  
  unsigned long fileSize = 0;
  
  if (sdAvailable) {
    File file = SD.open(currentSessionFile.c_str(), FILE_READ);
    if (!file) {
      Serial.println("[ERROR] No se pudo abrir archivo");
      currentState = STATE_ERROR;
      return;
    }
    fileSize = file.size();
    file.close();
  } else {
    // Modo prueba: simular tamaño de archivo
    fileSize = sizeof(FileHeader) + (sampleCount * sizeof(ECGSample)) + (imuSampleCount * sizeof(IMUSample));
    Serial.printf("[INFO] Tamaño simulado: %lu bytes (%.2f KB)\n", fileSize, fileSize / 1024.0);
  }
  
  // Crear payload JSON
  DynamicJsonDocument doc(512);
  doc["device_id"] = DEVICE_ID;
  doc["session_id"] = currentSessionID;
  doc["timestamp"] = String(captureStartTime / 1000);
  doc["file_size"] = fileSize;
  doc["ready_for_upload"] = true;
  
  char jsonBuffer[512];
  size_t jsonSize = serializeJson(doc, jsonBuffer);
  
  Serial.println("[MQTT] Publicando solicitud...");
  Serial.println("[DEBUG] Topic: " + String(TOPIC_REQUEST));
  Serial.println("[DEBUG] Payload size: " + String(jsonSize) + " bytes");
  Serial.println("[DEBUG] Payload: " + String(jsonBuffer));
  Serial.println("[DEBUG] Esperando en: " + String(TOPIC_RESPONSE));
  
  // Mantener conexión viva antes de verificar estado
  mqttClient.loop();
  delay(10);
  
  Serial.println("[DEBUG] MQTT Estado antes de publicar: " + String(mqttClient.state()));
  Serial.println("[DEBUG] MQTT Conectado: " + String(mqttClient.connected() ? "SI" : "NO"));
  
  // Verificar conexión antes de publicar
  if (!mqttClient.connected()) {
    Serial.println("[ERROR] MQTT desconectado antes de publicar");
    Serial.println("[INFO] Intentando reconectar...");
    if (!connectMQTT()) {
      Serial.println("[ERROR] No se pudo reconectar");
      currentState = STATE_ERROR;
      return;
    }
  }
  
  // Publicar como byte array con longitud explícita
  bool publishResult = mqttClient.publish(TOPIC_REQUEST, (uint8_t*)jsonBuffer, jsonSize);
  
  Serial.println("[DEBUG] publish() retornó: " + String(publishResult ? "true" : "false"));
  
  if (publishResult) {
    Serial.println("[MQTT] Solicitud enviada");
    Serial.println("[INFO] Esperando respuesta (60s timeout)...");
    
    uploadStartTime = millis();
    urlReceived = false;
    
    // Esperar respuesta con logging cada 5 segundos
    unsigned long lastLog = millis();
    while (!urlReceived && (millis() - uploadStartTime) < 60000) {
      mqttClient.loop();
      
      // Log cada 5 segundos
      if (millis() - lastLog > 5000) {
        Serial.printf("[WAIT] Esperando... (%lus)\n", (millis() - uploadStartTime) / 1000);
        lastLog = millis();
      }
      
      delay(100);
    }
    
    if (urlReceived) {
      currentState = STATE_UPLOADING;
    } else {
      Serial.println("[ERROR] Timeout esperando URL (60s)");
      currentState = STATE_ERROR;
    }
  } else {
    Serial.println("[ERROR] No se pudo publicar");
    Serial.println("[DEBUG] MQTT Estado después de fallar: " + String(mqttClient.state()));
    Serial.println("[DEBUG] MQTT Conectado: " + String(mqttClient.connected() ? "SI" : "NO"));
    Serial.println("[DEBUG] Payload size: " + String(jsonSize) + " bytes");
    
    // Códigos de estado MQTT:
    // -4 : MQTT_CONNECTION_TIMEOUT
    // -3 : MQTT_CONNECTION_LOST
    // -2 : MQTT_CONNECT_FAILED
    // -1 : MQTT_DISCONNECTED
    //  0 : MQTT_CONNECTED
    
    currentState = STATE_ERROR;
  }
}

// ============================================================================
// FUNCIONES HTTP UPLOAD
// ============================================================================
bool uploadToS3() {
  Serial.println("\n[S3] Iniciando upload...");
  
  File file = SD.open(currentSessionFile.c_str(), FILE_READ);
  if (!file) {
    Serial.println("[ERROR] No se pudo abrir archivo");
    return false;
  }
  
  unsigned long fileSize = file.size();
  Serial.println("[S3] Archivo: " + currentSessionFile);
  Serial.println("[S3] Tamaño: " + String(fileSize / 1024) + " KB");
  
  HTTPClient http;
  http.begin(uploadURL);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("Content-Length", String(fileSize));
  
  Serial.println("[S3] Enviando datos...");
  
  // Enviar archivo en chunks
  WiFiClient* stream = http.getStreamPtr();
  const size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];
  size_t bytesUploaded = 0;
  unsigned long lastReport = millis();
  
  http.sendRequest("PUT");
  
  while (file.available()) {
    size_t bytesRead = file.read(buffer, chunkSize);
    stream->write(buffer, bytesRead);
    bytesUploaded += bytesRead;
    
    // Progreso cada segundo
    if (millis() - lastReport > 1000) {
      float progress = (bytesUploaded * 100.0) / fileSize;
      Serial.printf("[S3] Progreso: %.1f%% (%lu/%lu KB)\n", 
                    progress, bytesUploaded/1024, fileSize/1024);
      lastReport = millis();
    }
  }
  
  file.close();
  
  int httpCode = http.GET();
  http.end();
  
  if (httpCode == 200 || httpCode == 204) {
    Serial.println("[S3] Upload exitoso!");
    
    // Borrar archivo de SD
    if (SD.remove(currentSessionFile.c_str())) {
      Serial.println("[SD] Archivo eliminado (espacio liberado)");
    }
    
    return true;
  } else {
    Serial.println("[S3] Error HTTP: " + String(httpCode));
    return false;
  }
}

// ============================================================================
// FUNCIONES DE CAPTURA (igual que Fase 1)
// ============================================================================
// Forward declarations
void stopCapture();

void flushBuffer() {
  if (sdAvailable && bufferIndex > 0 && dataFile) {
    dataFile.write(writeBuffer, bufferIndex);
    bufferIndex = 0;
  }
}

void writeToBuffer(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    writeBuffer[bufferIndex++] = data[i];
    if (bufferIndex >= BUFFER_SIZE) {
      flushBuffer();
    }
  }
}

void startCapture() {
  Serial.println("\n========================================");
  Serial.println("INICIANDO CAPTURA");
  Serial.println("========================================");
  
  captureStartTime = millis();
  unsigned long timestamp = captureStartTime / 1000;
  currentSessionID = "session_" + String(timestamp);
  currentSessionFile = "/" + currentSessionID + ".bin";
  currentSessionGzFile = "/" + currentSessionID + ".bin.gz";
  
  Serial.println("[INFO] Sesión: " + currentSessionID);
  Serial.println("[INFO] Archivo: " + currentSessionFile);
  Serial.printf("[INFO] Duración configurada: %d segundos\n", CAPTURE_DURATION_SEC);
  
  if (!sdAvailable) {
    Serial.println("[WARNING] Modo prueba - saltando captura");
    // En modo prueba, simular datos mínimos
    sampleCount = 100;  // Simular 100 muestras
    imuSampleCount = 100;
    isCapturing = false;
    Serial.println("[CAPTURE] Captura simulada instantánea\n");
    currentState = STATE_UPLOAD_REQUEST;  // Ir directo a upload
    return;
  }
  
  dataFile = SD.open(currentSessionFile.c_str(), FILE_WRITE);
  if(!dataFile) {
    Serial.println("[ERROR] No se pudo crear archivo");
    currentState = STATE_ERROR;
    return;
  }
  
  if (sdAvailable) {
    FileHeader header = {0};
    header.magic = 0x45434744;  // "ECGD"
    header.version = 1;
    header.device_id = 1;
    header.session_id = timestamp;
    header.timestamp_start = timestamp;
    header.ecg_sample_rate = ECG_SAMPLE_RATE_HZ;
    header.imu_sample_rate = IMU_SAMPLE_RATE_HZ;
    
    dataFile.write((uint8_t*)&header, sizeof(FileHeader));
  }
  
  sampleCount = 0;
  imuSampleCount = 0;
  bufferIndex = 0;
  isCapturing = true;
  
  lastECGSample = micros();
  lastIMUSample = micros();
  
  currentState = STATE_CAPTURING;
  
  Serial.println("[CAPTURE] Capturando...\n");
}

void captureLoop() {
  unsigned long currentTime = micros();
  unsigned long elapsed = (millis() - captureStartTime) / 1000;
  
  if (elapsed >= CAPTURE_DURATION_SEC) {
    stopCapture();
    return;
  }
  
  // ECG
  if (currentTime - lastECGSample >= ECG_INTERVAL_US) {
    lastECGSample = currentTime;
    
    float derivationI = MyBioBoard.AD8232_GetVoltage(AD8232_XS1);
    float derivationII = MyBioBoard.AD8232_GetVoltage(AD8232_XS2);
    float derivationIII = derivationII - derivationI;
    
    // Convertir float (mV) a int16 escalado
    ECGSample sample;
    sample.derivation_I = (int16_t)(derivationI * ECG_SCALE_FACTOR);
    sample.derivation_II = (int16_t)(derivationII * ECG_SCALE_FACTOR);
    sample.derivation_III = (int16_t)(derivationIII * ECG_SCALE_FACTOR);
    
    writeToBuffer((uint8_t*)&sample, sizeof(ECGSample));
    
    sampleCount++;
  }
  
  // IMU
  if (currentTime - lastIMUSample >= IMU_INTERVAL_US) {
    lastIMUSample = currentTime;
    
    IMUSample sample;
    
    if (imuAvailable) {
      sensors_event_t event;
      accel.getEvent(&event);
      sample.accel_x = (int16_t)(event.acceleration.x * 2048.0 / 9.81);
      sample.accel_y = (int16_t)(event.acceleration.y * 2048.0 / 9.81);
      sample.accel_z = (int16_t)(event.acceleration.z * 2048.0 / 9.81);
    } else {
      // IMU no disponible - usar valores 0
      sample.accel_x = 0;
      sample.accel_y = 0;
      sample.accel_z = 0;
    }
    
    sample.gyro_x = 0;
    sample.gyro_y = 0;
    sample.gyro_z = 0;
    
    writeToBuffer((uint8_t*)&sample, sizeof(IMUSample));
    
    imuSampleCount++;
  }
  
  // Progreso
  static unsigned long lastReport = 0;
  if (elapsed > 0 && elapsed % 10 == 0 && elapsed != lastReport) {
    lastReport = elapsed;
    float progress = (elapsed * 100.0) / CAPTURE_DURATION_SEC;
    Serial.printf("[PROGRESS] %lus/%ds (%.1f%%) | ECG: %lu | IMU: %lu\n", 
                  elapsed, CAPTURE_DURATION_SEC, progress, sampleCount, imuSampleCount);
  }
}

void stopCapture() {
  if (!isCapturing) return;
  
  Serial.println("\n[CAPTURE] Finalizando...");
  
  isCapturing = false;
  
  if (!sdAvailable) {
    // Modo prueba sin SD
    Serial.println("\n========================================");
    Serial.println("CAPTURA SIMULADA COMPLETADA");
    Serial.println("========================================");
    Serial.printf("[INFO] ECG: %lu muestras (simuladas)\n", sampleCount);
    Serial.printf("[INFO] IMU: %lu muestras (simuladas)\n", imuSampleCount);
    Serial.println("[INFO] Pasando a solicitar URL de AWS...");
    Serial.println("========================================\n");
    currentState = STATE_UPLOAD_REQUEST;
    return;
  }
  
  flushBuffer();
  
  dataFile.seek(0);
  FileHeader header;
  dataFile.read((uint8_t*)&header, sizeof(FileHeader));
  header.num_ecg_samples = sampleCount;
  header.num_imu_samples = imuSampleCount;
  
  dataFile.seek(0);
  dataFile.write((uint8_t*)&header, sizeof(FileHeader));
  dataFile.close();
  
  File checkFile = SD.open(currentSessionFile.c_str(), FILE_READ);
  unsigned long fileSize = checkFile.size();
  checkFile.close();
  
  // Calcular tamaño esperado
  unsigned long expectedSize = sizeof(FileHeader) + 
                               (sampleCount * sizeof(ECGSample)) +
                               (imuSampleCount * sizeof(IMUSample));
  
  Serial.println("\n========================================");
  Serial.println("CAPTURA COMPLETADA - FORMATO BINARIO INT16");
  Serial.println("========================================");
  Serial.printf("[INFO] Tamaño: %lu KB (%.2f MB)\n", fileSize/1024, fileSize/(1024.0*1024.0));
  Serial.printf("[INFO] ECG: %lu muestras x %d bytes = %lu KB\n", 
                sampleCount, sizeof(ECGSample), (sampleCount * sizeof(ECGSample))/1024);
  Serial.printf("[INFO] IMU: %lu muestras x %d bytes = %lu KB\n",
                imuSampleCount, sizeof(IMUSample), (imuSampleCount * sizeof(IMUSample))/1024);
  Serial.printf("[INFO] Header: %d bytes\n", sizeof(FileHeader));
  Serial.printf("[VALIDATE] Esperado: %lu bytes | Real: %lu bytes\n", expectedSize, fileSize);
  
  if (fileSize == expectedSize) {
    Serial.println("[OK] Archivo íntegro - listo para upload");
  } else {
    Serial.println("[WARNING] Discrepancia detectada");
  }
  Serial.println("========================================\n");
  
  currentState = STATE_UPLOAD_REQUEST;
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(BAUD_RATE);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("HOLTER FASE 2: CAPTURA + AWS UPLOAD");
  Serial.println("========================================");
  
  // Hardware
  MyBioBoard.init();
  MyBioBoard.AD8232_Wake(AD8232_XS1);
  MyBioBoard.AD8232_Wake(AD8232_XS2);
  Serial.println("[OK] XSpaceBio + ECG");
  
  Wire.begin();
  if(!accel.begin()) {
    Serial.println("[WARNING] ADXL345 no detectado - usando datos simulados (0)");
    imuAvailable = false;
  } else {
    accel.setRange(ADXL345_RANGE_4_G);
    accel.setDataRate(ADXL345_DATARATE_100_HZ);
    Serial.println("[OK] ADXL345");
    imuAvailable = true;
  }
  
  if(!SD.begin(SD_CS_PIN)) {
    Serial.println("[WARNING] SD Card no detectada - modo prueba AWS (sin captura real)");
    sdAvailable = false;
  } else {
    Serial.println("[OK] SD Card");
    sdAvailable = true;
  }
  
  Serial.println("\n[INFO] WiFi desconectado durante captura");
  Serial.println("[INFO] Se conectará después para upload");
  Serial.println("\n[READY] Iniciando captura en 3 segundos...\n");
  
  delay(3000);
  
  startCapture();
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  switch (currentState) {
    case STATE_CAPTURING:
      captureLoop();
      break;
      
    case STATE_UPLOAD_REQUEST:
      connectWiFi();
      if (WiFi.status() == WL_CONNECTED) {
        if (connectMQTT()) {
          requestUploadURL();
        } else {
          currentState = STATE_ERROR;
        }
      }
      break;
      
    case STATE_UPLOADING:
      if (!sdAvailable) {
        Serial.println("\n[INFO] Modo prueba - no hay archivo para subir");
        Serial.println("[SUCCESS] Comunicación MQTT con AWS completada");
        Serial.println("========================================\n");
        currentState = STATE_COMPLETE;
      } else if (uploadToS3()) {
        Serial.println("\n========================================");
        Serial.println("SESIÓN COMPLETADA EXITOSAMENTE");
        Serial.println("========================================\n");
        currentState = STATE_COMPLETE;
      } else {
        currentState = STATE_ERROR;
      }
      break;
      
    case STATE_COMPLETE:
      disconnectWiFi();
      Serial.println("[INFO] Reiniciando en 10 segundos...\n");
      delay(10000);
      ESP.restart();
      break;
      
    case STATE_ERROR:
      Serial.println("\n[ERROR] Error en el sistema");
      Serial.println("[INFO] Reiniciando en 30 segundos...\n");
      delay(30000);
      ESP.restart();
      break;
  }
  
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  
  yield();
}
