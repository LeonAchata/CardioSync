#include "display_ui.h"

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define BUTTON_PIN 0
#define BATTERY_PIN 36

// ============================================================================
// INTERNAL STATE
// ============================================================================

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static XSpaceBioV10Board* g_bioBoard = nullptr;

// State
static bool displayReady = false;
static DisplayMode currentMode = DISP_IDLE;
static float currentProgress = 0.0;
static String currentMessage = "";
static String currentText = "";
static unsigned long messageTimeout = 0;

// Button
static byte lastButtonReading = HIGH;
static byte stableButtonState = HIGH;
static unsigned long lastDebounceTime = 0;
static const unsigned long debounceDelay = 50;


// Timing
static unsigned long lastUpdateTime = 0;
static const unsigned long UPDATE_INTERVAL = 200; // 200ms = 5fps

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void drawBatteryIconInternal(int x, int y, int percentage) {
  display.drawRect(x, y, 18, 9, SSD1306_WHITE);
  display.fillRect(x + 18, y + 2, 2, 5, SSD1306_WHITE);
  int fillWidth = map(constrain(percentage, 0, 100), 0, 100, 0, 15);
  display.fillRect(x + 2, y + 2, fillWidth, 5, SSD1306_WHITE);
}

static BatteryInfo getBatteryStatusInternal() {
  BatteryInfo battery;
  int rawValue = analogRead(BATTERY_PIN);
  battery.voltage = (rawValue / 4095.0) * 2.0 * 3.3;
  
  if (battery.voltage >= 4.1) battery.percentage = 100;
  else if (battery.voltage >= 3.9) battery.percentage = 80;
  else if (battery.voltage >= 3.7) battery.percentage = 60;
  else if (battery.voltage >= 3.5) battery.percentage = 40;
  else if (battery.voltage >= 3.3) battery.percentage = 20;
  else battery.percentage = 10;
  
  return battery;
}

static String currentTimeString() {
  struct tm timeinfo;
  // Timeout 0: getLocalTime() blocks up to 5 s by default if the clock is not set
  if (!getLocalTime(&timeinfo, 0)) return "--:--:--";
  
  char buffer[10];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buffer);
}

static void drawIdleScreen() {
  display.clearDisplay();
  
  // Clock, top left
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(currentTimeString());
  
  // Battery, top right
  BatteryInfo battery = getBatteryStatusInternal();
  drawBatteryIconInternal(105, 0, battery.percentage);
  
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("Standby");
  display.setTextSize(1);

  // Status text
  if (currentText.length() > 0) {
    display.setCursor(0, 48);
    display.print(currentText);
  }
  
  display.display();
}

static void drawConfirmCaptureScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Record?");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.print("Press button");
  display.setCursor(10, 45);
  display.print("to confirm");
  display.display();
}

static void drawConfirmUploadScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Upload?");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.print("Press button");
  display.setCursor(10, 45);
  display.print("to confirm");
  display.display();
}

static void drawCapturingScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(5, 10);
  display.print("Recording");
  
  // Progress bar
  int barWidth = 100;
  int barX = 14;
  int barY = 35;
  display.drawRect(barX, barY, barWidth, 10, SSD1306_WHITE);
  int fillWidth = (int)(currentProgress * (barWidth - 2));
  display.fillRect(barX + 1, barY + 1, fillWidth, 8, SSD1306_WHITE);
  
  // Percentage
  display.setTextSize(1);
  display.setCursor(50, 50);
  display.printf("%d%%", (int)(currentProgress * 100));
  
  display.display();
}

static void drawUploadingScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Uploading");
  
  // Progress bar
  int barWidth = 100;
  int barX = 14;
  int barY = 35;
  display.drawRect(barX, barY, barWidth, 10, SSD1306_WHITE);
  int fillWidth = (int)(currentProgress * (barWidth - 2));
  display.fillRect(barX + 1, barY + 1, fillWidth, 8, SSD1306_WHITE);
  
  // Percentage
  display.setTextSize(1);
  display.setCursor(50, 50);
  display.printf("%d%%", (int)(currentProgress * 100));
  
  display.display();
}

static void drawMessageScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  
  // Centre text
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(currentMessage, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
  display.print(currentMessage);
  
  display.display();
}

static void drawErrorScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ERROR:");
  display.setCursor(0, 15);
  display.print(currentMessage);
  display.display();
}

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

void display_forceUpdate() {
  lastUpdateTime = 0; // Force an immediate redraw
}

void display_init(XSpaceBioV10Board* bioBoard) {
  g_bioBoard = bioBoard;
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.println("[Display] Initialising OLED...");
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[Display] OLED not found at 0x3C"));
    Serial.println(F("[Display] Trying 0x3D..."));
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("[Display] ERROR: OLED not found, continuing without display"));
      return; // Keep running headless
    }
  }
  displayReady = true;
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // Splash screen
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.println("CARDIO");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.println("CardioSync ECG");
  display.setCursor(10, 50);
  display.println("Ready!");
  display.display();
  
  Serial.println("[Display] Ready");
  
  delay(2000); // Keep the splash for 2 s
  currentMode = DISP_IDLE;
}

void display_update() {
  if (!displayReady) return;

  // Redraw at most every UPDATE_INTERVAL
  unsigned long now = millis();
  if (now - lastUpdateTime < UPDATE_INTERVAL) {
    return;
  }
  lastUpdateTime = now;
  
  // Expire temporary messages
  if (currentMode == DISP_MESSAGE && messageTimeout > 0 && (long)(now - messageTimeout) >= 0) {
    currentMode = DISP_IDLE;
  }
  
  // Draw the current screen
  switch(currentMode) {
    case DISP_IDLE:
      drawIdleScreen();
      break;
    case DISP_CONFIRM_CAPTURE:
      drawConfirmCaptureScreen();
      break;
    case DISP_CAPTURING:
      drawCapturingScreen();
      break;
    case DISP_CONFIRM_UPLOAD:
      drawConfirmUploadScreen();
      break;
    case DISP_UPLOADING:
      drawUploadingScreen();
      break;
    case DISP_MESSAGE:
      drawMessageScreen();
      break;
    case DISP_ERROR:
      drawErrorScreen();
      break;
  }
}

void display_setMode(DisplayMode mode) {
  currentMode = mode;
  display_forceUpdate();
}

DisplayMode display_getMode() {
  return currentMode;
}

bool display_checkButton() {
  byte reading = digitalRead(BUTTON_PIN);
  bool buttonPressed = false;

  // Restart the timer on every bounce; only accept a stable level
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay && reading != stableButtonState) {
    stableButtonState = reading;
    buttonPressed = (stableButtonState == LOW);
  }

  lastButtonReading = reading;
  return buttonPressed;
}

void display_setProgress(float progress) {
  currentProgress = constrain(progress, 0.0, 1.0);
}

void display_showMessage(String message, unsigned long duration_ms) {
  currentMessage = message;
  currentMode = DISP_MESSAGE;
  messageTimeout = (duration_ms > 0) ? (millis() + duration_ms) : 0;
  display_forceUpdate();
}

void display_showError(String error) {
  currentMessage = error;
  currentMode = DISP_ERROR;
  display_forceUpdate();
}

void display_clear() {
  if (!displayReady) return;
  display.clearDisplay();
  display.display();
}

BatteryInfo display_getBattery() {
  return getBatteryStatusInternal();
}

void display_drawBatteryIcon(int x, int y, int percentage) {
  if (!displayReady) return;
  drawBatteryIconInternal(x, y, percentage);
}


void display_setText(String text) {
  currentText = text;
}
