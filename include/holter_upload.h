#ifndef HOLTER_UPLOAD_H
#define HOLTER_UPLOAD_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPClient.h>

// ============================================================================
// ESTADOS DE UPLOAD
// ============================================================================

enum UploadState {
  UPLOAD_IDLE,
  UPLOAD_CONNECTING_WIFI,
  UPLOAD_CONNECTING_MQTT,
  UPLOAD_REQUESTING_URL,
  UPLOAD_UPLOADING_S3,
  UPLOAD_COMPLETE,
  UPLOAD_ERROR
};

// ============================================================================
// INTERFACE PÚBLICA
// ============================================================================

/**
 * Inicializa el módulo de upload (WiFi, MQTT, certificados AWS)
 * Debe ser llamado en setup()
 */
void holter_initUpload();

/**
 * Conecta a WiFi
 * @return true si se conectó exitosamente
 */
bool holter_connectWiFi();

/**
 * Desconecta WiFi para ahorrar energía
 */
void holter_disconnectWiFi();

/**
 * Inicia el proceso de upload de un archivo a AWS
 * @param filename Nombre del archivo a subir (con path completo)
 * @return true si se inició correctamente
 */
bool holter_startUpload(String filename);

/**
 * Loop de upload - debe ser llamado continuamente durante el upload
 * Maneja la máquina de estados: WiFi → MQTT → S3
 */
void holter_uploadLoop();

/**
 * Cancela el upload actual
 */
void holter_cancelUpload();

/**
 * Verifica si hay un upload en progreso
 * @return true si está subiendo
 */
bool holter_isUploading();

/**
 * Obtiene el progreso del upload actual
 * @return Valor entre 0.0 y 1.0 (0% a 100%)
 */
float holter_getUploadProgress();

/**
 * Obtiene el estado actual del upload
 */
UploadState holter_getUploadState();

/**
 * Obtiene un string descriptivo del estado actual
 */
String holter_getUploadStateString();

/**
 * Verifica si WiFi está conectado
 */
bool holter_isWiFiConnected();

/**
 * Verifica si MQTT está conectado
 */
bool holter_isMQTTConnected();

/**
 * Obtiene el último error ocurrido
 */
String holter_getLastError();

#endif // HOLTER_UPLOAD_H
