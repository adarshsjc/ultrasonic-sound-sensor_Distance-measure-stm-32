#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =============================================================================
// Configuration & Pin Definitions
// =============================================================================

// LCD I2C Backpack Configuration
// Common default I2C address for PCF8574 is 0x27.
// If your LCD uses the PCF8574A chip, change this address to 0x3F.
#define LCD_I2C_ADDRESS 0x27
#define LCD_COLUMNS     16
#define LCD_ROWS        2

// I2C Pins for STM32F103C8T6 Blue Pill
// Default hardware I2C1 pins:
#define PIN_I2C_SDA     PB7
#define PIN_I2C_SCL     PB6

// HC-SR04 Ultrasonic Sensor Pins
#define PIN_TRIG        PA0 // PA0 configured as digital OUTPUT
#define PIN_ECHO        PA1 // PA1 configured as digital INPUT (via 2.47V voltage divider)

// Optional on-board LED for visual heartbeat (PC13 on Blue Pill, active LOW)
#ifndef LED_BUILTIN
#define LED_BUILTIN     PC13
#endif

// Timing constants
const unsigned long ECHO_TIMEOUT_US     = 30000; // 30 ms timeout (~514 cm max theoretical range)
const unsigned long REFRESH_INTERVAL_MS = 250;   // Refresh measurement every 250 ms

// Initialize the LCD object with user-configured address
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// Global timestamp for non-blocking timing
unsigned long lastMeasurementTime = 0;

// =============================================================================
// Helper Function: Measure Distance
// =============================================================================
/**
 * Triggers the HC-SR04 sensor and measures the ECHO pulse duration.
 * 
 * @param echo_duration_us [out] Measured pulse duration in microseconds (0 if timeout).
 * @return float Calculated distance in centimeters (negative or 0 if no echo).
 */
float measureDistance(unsigned long &echo_duration_us) {
  // 1. Ensure TRIG is LOW for a clean pulse start
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);

  // 2. Generate 10-microsecond HIGH trigger pulse
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // 3. Measure the HIGH pulse duration on ECHO pin
  // The ECHO voltage divider produces ~2.47V for logic HIGH,
  // which is well above the STM32F103 digital input threshold (VIH ~ 2.0V).
  echo_duration_us = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);

  // 4. Calculate distance if a valid echo pulse was received
  if (echo_duration_us > 0) {
    // Formula: distance = (time * speed of sound in air (0.0343 cm/us)) / 2 (round-trip)
    float distance_cm = (float)echo_duration_us * 0.0343f / 2.0f;
    return distance_cm;
  }

  // No echo detected (timeout or object out of range)
  return -1.0f;
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
  // Initialize Serial Monitor at 115200 baud
  Serial.begin(115200);
  delay(500); // Short delay to allow Serial and power rails to stabilize

  Serial.println(F("=================================================="));
  Serial.println(F("STM32 Blue Pill - HC-SR04 Ultrasonic Distance Meter"));
  Serial.println(F("=================================================="));

  // Configure HC-SR04 pins
  pinMode(PIN_TRIG, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);

  // Configure PA1 as standard digital INPUT
  // Do NOT set as output or enable pull-ups: the external 10k/10k divider provides 2.47V logic HIGH
  pinMode(PIN_ECHO, INPUT);

  // Configure on-board heartbeat LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn off LED initially (active LOW)

  // Initialize I2C communication on PB7 (SDA) and PB6 (SCL)
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();

  // Quick I2C bus diagnostic check for the configured LCD address
  Wire.beginTransmission(LCD_I2C_ADDRESS);
  uint8_t i2cStatus = Wire.endTransmission();
  if (i2cStatus == 0) {
    Serial.print(F("[I2C] LCD backpack successfully detected at address: 0x"));
    Serial.println(LCD_I2C_ADDRESS, HEX);
  } else {
    Serial.print(F("[I2C WARNING] No ACK from LCD at 0x"));
    Serial.println(LCD_I2C_ADDRESS, HEX);
    Serial.println(F("If your LCD remains blank, verify wiring or change LCD_I2C_ADDRESS to 0x3F in main.cpp."));
  }

  // Initialize LCD display
  lcd.init();
  lcd.backlight();

  // Print static row 1 header
  // Note: Printing full 16 characters prevents stale text and avoids lcd.clear() flicker
  lcd.setCursor(0, 0);
  lcd.print("Distance:       ");

  // Initial standby message on row 2
  lcd.setCursor(0, 1);
  lcd.print("Initializing... ");

  delay(500);
  Serial.println(F("Initialization complete. Starting measurement loop..."));
}

// =============================================================================
// Main Loop
// =============================================================================
void loop() {
  unsigned long currentMillis = millis();

  // Refresh measurement approximately every 250 ms
  if (currentMillis - lastMeasurementTime >= REFRESH_INTERVAL_MS) {
    lastMeasurementTime = currentMillis;

    // Toggle heartbeat LED on each measurement cycle
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    unsigned long echo_duration_us = 0;
    float distance_cm = measureDistance(echo_duration_us);

    // Buffer to hold exact 16-character string for row 2
    char lineBuffer[17];

    if (echo_duration_us > 0 && distance_cm >= 0.0f) {
      // Valid echo received: format distance with 1 decimal place (e.g. "42.5 cm")
      char numBuffer[10];
      dtostrf(distance_cm, 1, 1, numBuffer);

      char textWithUnit[16];
      snprintf(textWithUnit, sizeof(textWithUnit), "%s cm", numBuffer);

      // Pad with spaces up to 16 chars so previous text is cleanly overwritten without lcd.clear()
      snprintf(lineBuffer, sizeof(lineBuffer), "%-16s", textWithUnit);

      // Print debug information to Serial Monitor
      Serial.print(F("Echo: "));
      Serial.print(echo_duration_us);
      Serial.print(F(" us | Distance: "));
      Serial.print(distance_cm, 1);
      Serial.println(F(" cm"));

    } else {
      // No echo received within 30 ms timeout
      // "No echo detected" is exactly 16 characters, perfectly filling the second row
      snprintf(lineBuffer, sizeof(lineBuffer), "%-16s", "No echo detected");

      // Print timeout notification to Serial Monitor
      Serial.print(F("Echo: "));
      Serial.print(echo_duration_us);
      Serial.println(F(" us | No echo detected"));
    }

    // Ensure First Row title is maintained
    lcd.setCursor(0, 0);
    lcd.print("Distance:       ");

    // Update Second Row without clearing screen (avoids display flicker)
    lcd.setCursor(0, 1);
    lcd.print(lineBuffer);
  }
}