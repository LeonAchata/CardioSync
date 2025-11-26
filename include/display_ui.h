#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <XSpaceBioV10.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// MODOS DE PANTALLA
// ============================================================================

enum DisplayMode {
  DISP_IDLE,              // Pantalla normal: ECG en vivo, batería, hora
  DISP_CONFIRM_CAPTURE,   // Confirmación: "Grabar 15s?"
  DISP_CAPTURING,         // Mostrando progreso de captura
  DISP_CONFIRM_UPLOAD,    // Confirmación: "Subir a AWS?"
  DISP_UPLOADING,         // Mostrando progreso de upload
  DISP_MESSAGE,           // Mensaje temporal
  DISP_ERROR              // Pantalla de error
};

// ============================================================================
// INFORMACIÓN DE BATERÍA
// ============================================================================

struct BatteryInfo {
  float voltage;
  int percentage;
};

// ============================================================================
// INTERFACE PÚBLICA
// ============================================================================

/**
 * Inicializa el módulo de display (OLED, botones, pines)
 * Debe ser llamado en setup()
 */
void display_init(XSpaceBioV10Board* bioBoard);

/**
 * Actualiza la pantalla según el modo actual
 * Debe ser llamado periódicamente en loop()
 */
void display_update();

/**
 * Cambia el modo de pantalla
 */
void display_setMode(DisplayMode mode);

/**
 * Obtiene el modo actual de la pantalla
 */
DisplayMode display_getMode();

/**
 * Verifica si el botón fue presionado (con debounce)
 * @return true si hubo un click válido
 */
bool display_checkButton();

/**
 * Establece el progreso a mostrar en modos CAPTURING o UPLOADING
 * @param progress Valor entre 0.0 y 1.0 (0% a 100%)
 */
void display_setProgress(float progress);

/**
 * Muestra un mensaje temporal
 * @param message Texto a mostrar
 * @param duration_ms Duración en milisegundos (0 = indefinido)
 */
void display_showMessage(String message, unsigned long duration_ms = 2000);

/**
 * Muestra un mensaje de error
 * @param error Texto del error
 */
void display_showError(String error);

/**
 * Limpia la pantalla
 */
void display_clear();

/**
 * Fuerza actualización inmediata de la pantalla
 */
void display_forceUpdate();

/**
 * Obtiene la información actual de la batería
 */
BatteryInfo display_getBattery();

/**
 * Dibuja el icono de batería en la posición especificada
 */
void display_drawBatteryIcon(int x, int y, int percentage);

/**
 * Establece el valor de ECG a mostrar en pantalla idle
 */
void display_setECGValue(float derivation_I, float derivation_II, float derivation_III);

/**
 * Establece el texto adicional a mostrar en la pantalla
 */
void display_setText(String text);

#endif // DISPLAY_UI_H
