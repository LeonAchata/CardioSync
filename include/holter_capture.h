#ifndef HOLTER_CAPTURE_H
#define HOLTER_CAPTURE_H

#include <Arduino.h>
#include <XSpaceBioV10.h>
#include <XSpaceV21.h>
#include <SD.h>

// ============================================================================
// ESTRUCTURAS DE DATOS
// ============================================================================

struct FileHeader {
  uint32_t magic;              // 0x45434744 = "ECGD"
  uint16_t version;
  uint16_t device_id;
  uint32_t session_id;
  uint32_t timestamp_start;
  uint16_t ecg_sample_rate;
  uint16_t imu_sample_rate;
  uint32_t num_ecg_samples;
  uint32_t num_imu_samples;
} __attribute__((packed));

struct ECGSample {
  int16_t derivation_I;
  int16_t derivation_II;
  int16_t derivation_III;
} __attribute__((packed));

struct IMUSample {
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
} __attribute__((packed));

// ============================================================================
// INTERFACE PÚBLICA
// ============================================================================

/**
 * Inicializa el módulo de captura (SD Card, IMU)
 * Debe ser llamado en setup()
 */
void holter_init(XSpaceBioV10Board* bioBoard, XSpaceV21Board* v21Board);

/**
 * Inicia una sesión de captura de ECG + IMU
 * Crea archivo en SD y comienza a grabar
 */
bool holter_startCapture();

/**
 * Loop de captura - debe ser llamado continuamente durante la captura
 * Maneja el muestreo de ECG (250Hz) e IMU (25Hz)
 */
void holter_captureLoop();

/**
 * Detiene la captura actual, cierra el archivo y actualiza header
 */
void holter_stopCapture();

/**
 * Verifica si hay una captura en progreso
 * @return true si está capturando
 */
bool holter_isCapturing();

/**
 * Obtiene el progreso de la captura actual
 * @return Valor entre 0.0 y 1.0 (0% a 100%)
 */
float holter_getProgress();

/**
 * Obtiene el tiempo transcurrido de captura en segundos
 */
unsigned long holter_getElapsedSeconds();

/**
 * Obtiene el nombre del archivo actual
 */
String holter_getCurrentFile();

/**
 * Obtiene el número de muestras ECG capturadas
 */
unsigned long holter_getECGSampleCount();

/**
 * Obtiene el número de muestras IMU capturadas
 */
unsigned long holter_getIMUSampleCount();

/**
 * Verifica si la SD Card está disponible
 */
bool holter_isSDAvailable();

/**
 * Verifica si el IMU está disponible
 */
bool holter_isIMUAvailable();

#endif

