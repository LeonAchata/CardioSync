#include "display_ui.h"

// ============================================================================
// CONFIGURACIÓN HARDWARE
// ============================================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define BUTTON_PIN 0
#define BATTERY_PIN 36

// ============================================================================
// VARIABLES INTERNAS (PRIVADAS)
// ============================================================================

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static XSpaceBioV10Board* g_bioBoard = nullptr;

// Estado
static DisplayMode currentMode = DISP_IDLE;
static float currentProgress = 0.0;
static String currentMessage = "";
static String currentText = "";
static unsigned long messageTimeout = 0;

// Botón
static byte lastButtonState = HIGH;
static unsigned long lastDebounceTime = 0;
static const unsigned long debounceDelay = 50;

// ECG para display
static float ecg_I = 0.0;
static float ecg_II = 0.0;
static float ecg_III = 0.0;

// Timing
static unsigned long lastUpdateTime = 0;
static const unsigned long UPDATE_INTERVAL = 200; // 200ms = 5fps

// ============================================================================
// FUNCIONES INTERNAS (PRIVADAS)
// ============================================================================

static void drawBatteryIconInternal(int x, int y, int percentage) {
  display.drawRect(x, y, 18, 9, SSD1306_WHITE);
  display.fillRect(x + 18, y + 2, 2, 5, SSD1306_WHITE);
  int fillWidth = map(percentage, 0, 100, 0, 15);
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

static String obtenerHoraActual() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  
  char buffer[10];
  sprintf(buffer, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buffer);
}

static void drawIdleScreen() {
  display.clearDisplay();
  
  // Hora arriba a la izquierda
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(obtenerHoraActual());
  
  // Batería arriba a la derecha
  BatteryInfo battery = getBatteryStatusInternal();
  drawBatteryIconInternal(105, 0, battery.percentage);
  
  // ECG en el centro
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.printf("I:  %.2f mV", ecg_I);
  display.setCursor(0, 32);
  display.printf("II: %.2f mV", ecg_II);
  display.setCursor(0, 44);
  display.printf("III:%.2f mV", ecg_III);
  
  // Texto adicional
  if (currentText.length() > 0) {
    display.setCursor(0, 56);
    display.print(currentText);
  }
  
  display.display();
}

static void drawConfirmCaptureScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Grabar?");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.print("Presiona boton");
  display.setCursor(10, 45);
  display.print("para confirmar");
  display.display();
}

static void drawConfirmUploadScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Subir?");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.print("Presiona boton");
  display.setCursor(10, 45);
  display.print("para confirmar");
  display.display();
}

static void drawCapturingScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(5, 10);
  display.print("Grabando");
  
  // Barra de progreso
  int barWidth = 100;
  int barX = 14;
  int barY = 35;
  display.drawRect(barX, barY, barWidth, 10, SSD1306_WHITE);
  int fillWidth = (int)(currentProgress * (barWidth - 2));
  display.fillRect(barX + 1, barY + 1, fillWidth, 8, SSD1306_WHITE);
  
  // Porcentaje
  display.setTextSize(1);
  display.setCursor(50, 50);
  display.printf("%d%%", (int)(currentProgress * 100));
  
  display.display();
}

static void drawUploadingScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Subiendo");
  
  // Barra de progreso
  int barWidth = 100;
  int barX = 14;
  int barY = 35;
  display.drawRect(barX, barY, barWidth, 10, SSD1306_WHITE);
  int fillWidth = (int)(currentProgress * (barWidth - 2));
  display.fillRect(barX + 1, barY + 1, fillWidth, 8, SSD1306_WHITE);
  
  // Porcentaje
  display.setTextSize(1);
  display.setCursor(50, 50);
  display.printf("%d%%", (int)(currentProgress * 100));
  
  display.display();
}

static void drawMessageScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  
  // Centrar texto
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
// IMPLEMENTACIÓN DE INTERFACE PÚBLICA
// ============================================================================

void display_forceUpdate() {
  lastUpdateTime = 0; // Forzar actualización inmediata
}

void display_init(XSpaceBioV10Board* bioBoard) {
  g_bioBoard = bioBoard;
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.println("[Display] Inicializando OLED...");
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[Display] ERROR: No se pudo inicializar OLED en 0x3C"));
    Serial.println(F("[Display] Intentando con 0x3D..."));
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("[Display] ERROR: No se pudo inicializar OLED"));
      return; // No bloquear, continuar sin display
    }
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // Mensaje de bienvenida
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.println("HOLTER");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.println("ECG-IMU System");
  display.setCursor(10, 50);
  display.println("Listo!");
  display.display();
  
  Serial.println("[Display] Inicializado correctamente");
  
  delay(2000); // Mostrar mensaje 2 segundos
  currentMode = DISP_IDLE;
}

void display_update() {
  // Actualizar solo cada UPDATE_INTERVAL
  unsigned long now = millis();
  if (now - lastUpdateTime < UPDATE_INTERVAL) {
    return;
  }
  lastUpdateTime = now;
  
  // Verificar timeout de mensaje
  if (currentMode == DISP_MESSAGE && messageTimeout > 0 && now > messageTimeout) {
    currentMode = DISP_IDLE;
  }
  
  // Dibujar según modo
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
  byte currentState = digitalRead(BUTTON_PIN);
  bool buttonPressed = false;
  
  if (currentState != lastButtonState) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (currentState == LOW) {
        buttonPressed = true;
      }
      lastDebounceTime = millis();
    }
  }
  lastButtonState = currentState;
  
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
  display.clearDisplay();
  display.display();
}

BatteryInfo display_getBattery() {
  return getBatteryStatusInternal();
}

void display_drawBatteryIcon(int x, int y, int percentage) {
  drawBatteryIconInternal(x, y, percentage);
}

void display_setECGValue(float derivation_I, float derivation_II, float derivation_III) {
  ecg_I = derivation_I;
  ecg_II = derivation_II;
  ecg_III = derivation_III;
}

void display_setText(String text) {
  currentText = text;
}
