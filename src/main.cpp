#include <Arduino.h>
#include <XSpaceBioV10.h>
#include <XSpaceV21.h>
#include "holter_capture.h"
#include "holter_upload.h"

// ============================================================================
// OBJETOS PRINCIPALES
// ============================================================================
XSpaceBioV10Board MyBioBoard;
XSpaceV21Board XSBoard; // Mantener por compatibilidad, pero no se usa

// ============================================================================
// ESTADOS DEL SISTEMA
// ============================================================================
enum SystemState {
  STATE_INIT,              // Inicialización
  STATE_CAPTURING,         // Capturando datos ECG
  STATE_UPLOADING,         // Subiendo a AWS S3
  STATE_COMPLETE,          // Completado exitosamente
  STATE_ERROR              // Error en el sistema
};

SystemState currentState = STATE_INIT;
String currentFilename = "";
unsigned long stateStartTime = 0;

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(2000); // Delay más largo para estabilizar Serial
 
  Serial.println("\n\n========================================");
  Serial.println("HOLTER ECG SYSTEM v2.0");
  Serial.println("========================================");
  Serial.println("[INFO] ESP32 Holter Monitoring System");
  Serial.println("[INFO] ECG 3-lead @ 250Hz");
  Serial.println("[INFO] Auto-capture y auto-upload a AWS");
  Serial.println("========================================\n");
  
  // Inicializar módulos
  Serial.println("[SETUP] Inicializando módulos...");
  
  // Primero inicializar captura (SD Card)
  holter_init(&MyBioBoard, nullptr); // nullptr porque IMU no se usa
  
  // Luego inicializar upload (WiFi/MQTT)
  holter_initUpload();
  
  Serial.println("[SETUP] Sistema inicializado\n");
  
  // Verificar si SD está disponible
  if (!holter_isSDAvailable()) {
    Serial.println("[ERROR] SD Card no disponible");
    Serial.println("[ERROR] El sistema requiere SD Card para funcionar");
    Serial.println("[INFO] Por favor:");
    Serial.println("  1. Verifica que la tarjeta SD esté insertada");
    Serial.println("  2. Verifica que esté formateada en FAT32");
    Serial.println("  3. Verifica las conexiones SPI");
    Serial.println("  4. Presiona RESET para reintentar");
    currentState = STATE_ERROR;
    stateStartTime = millis();
    return;
  }
  
  // Pequeño delay antes de iniciar captura
  Serial.println("[INFO] Iniciando captura en 3 segundos...");
  delay(3000);
  
  // Iniciar captura automáticamente
  Serial.println("[SYSTEM] Iniciando captura automática...\n");
  
  if (holter_startCapture()) {
    currentFilename = holter_getCurrentFile();
    Serial.println("[OK] Captura iniciada exitosamente");
    Serial.println("[INFO] Archivo: " + currentFilename + "\n");
    currentState = STATE_CAPTURING;
  } else {
    Serial.println("[ERROR] No se pudo iniciar captura");
    Serial.println("[ERROR] Revisa los mensajes anteriores para más detalles");
    currentState = STATE_ERROR;
  }
  
  stateStartTime = millis();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  switch(currentState) {
    
    // ========================================================================
    // ESTADO: CAPTURING
    // ========================================================================
    case STATE_CAPTURING: {
      // Ejecutar loop de captura
      holter_captureLoop();
      
      // Verificar si terminó
      if (!holter_isCapturing()) {
        Serial.println("\n[CAPTURE] ¡Captura completada!");
        
        // Asegurar cierre
        holter_stopCapture();
        
        // Verificar que el archivo existe y tiene datos
        if (currentFilename.length() == 0) {
          Serial.println("[ERROR] No hay archivo para subir");
          currentState = STATE_ERROR;
          break;
        }
        
        Serial.println("[SYSTEM] Preparando para upload...\n");
        delay(1000);
        
        // Iniciar upload
        Serial.println("[UPLOAD] Iniciando proceso de upload...");
        if (holter_startUpload(currentFilename)) {
          Serial.println("[OK] Upload iniciado");
          currentState = STATE_UPLOADING;
        } else {
          Serial.println("[ERROR] No se pudo iniciar upload");
          currentState = STATE_ERROR;
        }
        
        stateStartTime = millis();
      }
      break;
    }
   
    // ========================================================================
    // ESTADO: UPLOADING
    // ========================================================================
    case STATE_UPLOADING: {
      // Ejecutar loop de upload
      holter_uploadLoop();
      
      // Mostrar estado periódicamente
      static unsigned long lastStatusLog = 0;
      if (millis() - lastStatusLog > 5000) {
        String status = holter_getUploadStateString();
        float progress = holter_getUploadProgress();
        Serial.printf("[STATUS] %s (%.0f%%)\n", status.c_str(), progress * 100);
        lastStatusLog = millis();
      }
      
      // Verificar si terminó
      if (!holter_isUploading()) {
        UploadState uploadState = holter_getUploadState();
        
        if (uploadState == UPLOAD_COMPLETE) {
          Serial.println("\n[UPLOAD] ¡Upload completado exitosamente!");
          currentState = STATE_COMPLETE;
        } else if (uploadState == UPLOAD_ERROR) {
          Serial.println("\n[UPLOAD] Error en upload");
          String error = holter_getLastError();
          if (error.length() > 0) {
            Serial.println("[ERROR] " + error);
          }
          currentState = STATE_ERROR;
        }
        
        stateStartTime = millis();
      }
      break;
    }
   
    // ========================================================================
    // ESTADO: COMPLETE
    // ========================================================================
    case STATE_COMPLETE: {
      // Desconectar WiFi para ahorrar energía
      holter_disconnectWiFi();
      
      Serial.println("\n========================================");
      Serial.println("✓ SESIÓN COMPLETADA EXITOSAMENTE");
      Serial.println("========================================");
      Serial.println("[INFO] Archivo capturado y subido a AWS S3");
      Serial.println("[INFO] El sistema se reiniciará en 10 segundos");
      Serial.println("[INFO] para iniciar una nueva sesión...");
      Serial.println("========================================\n");
      
      delay(10000);
      
      Serial.println("[SYSTEM] Reiniciando ESP32...\n");
      delay(1000);
      ESP.restart();
      break;
    }
   
    // ========================================================================
    // ESTADO: ERROR
    // ========================================================================
    case STATE_ERROR: {
      // Desconectar WiFi si estaba conectado
      if (holter_isWiFiConnected()) {
        holter_disconnectWiFi();
      }
      
      Serial.println("\n========================================");
      Serial.println("✗ ERROR EN EL SISTEMA");
      Serial.println("========================================");
      
      String error = holter_getLastError();
      if (error.length() > 0) {
        Serial.println("[ERROR] " + error);
      }
      
      Serial.println("\n[INFO] El sistema se reiniciará en 30 segundos");
      Serial.println("[INFO] para intentar recuperarse...");
      Serial.println("========================================\n");
      
      delay(30000);
      
      Serial.println("[SYSTEM] Reiniciando ESP32...\n");
      delay(1000);
      ESP.restart();
      break;
    }
    
    // ========================================================================
    // ESTADO: INIT (no debería llegar aquí)
    // ========================================================================
    case STATE_INIT:
    default: {
      Serial.println("[WARNING] Estado inválido, reiniciando...");
      delay(1000);
      ESP.restart();
      break;
    }
  }
  
  // Yield para el watchdog
  yield();
}