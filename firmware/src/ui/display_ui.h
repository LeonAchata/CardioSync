#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <XSpaceBioV10.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// SCREENS
// ============================================================================

enum DisplayMode {
  DISP_IDLE,              // Standby: clock, battery, status text
  DISP_CONFIRM_CAPTURE,   // Confirmation: "Record?"
  DISP_CAPTURING,         // Capture progress
  DISP_CONFIRM_UPLOAD,    // Confirmation: "Upload?"
  DISP_UPLOADING,         // Upload progress
  DISP_MESSAGE,           // Temporary message
  DISP_ERROR              // Error screen
};

// ============================================================================
// BATTERY
// ============================================================================

struct BatteryInfo {
  float voltage;
  int percentage;
};

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

/**
 * Initialises the SSD1306 OLED (0x3C, falls back to 0x3D) and the button pin.
 * The firmware keeps running headless if no display is found.
 */
void display_init(XSpaceBioV10Board* bioBoard);

/**
 * Redraws the current screen (rate-limited). Call periodically from loop().
 */
void display_update();

/**
 * Switches to another screen.
 */
void display_setMode(DisplayMode mode);

/**
 * @return the current screen
 */
DisplayMode display_getMode();

/**
 * Debounced button check.
 * @return true once per press
 */
bool display_checkButton();

/**
 * Progress shown on the CAPTURING / UPLOADING screens.
 * @param progress value between 0.0 and 1.0
 */
void display_setProgress(float progress);

/**
 * Shows a temporary centred message.
 * @param duration_ms how long to show it (0 = until the mode changes)
 */
void display_showMessage(String message, unsigned long duration_ms = 2000);

/**
 * Shows the error screen.
 */
void display_showError(String error);

/**
 * Clears the display.
 */
void display_clear();

/**
 * Forces a redraw on the next display_update().
 */
void display_forceUpdate();

/**
 * @return battery voltage and estimated charge
 */
BatteryInfo display_getBattery();

/**
 * Draws the battery icon at (x, y).
 */
void display_drawBatteryIcon(int x, int y, int percentage);

/**
 * Status line shown on the standby screen.
 */
void display_setText(String text);

#endif // DISPLAY_UI_H
