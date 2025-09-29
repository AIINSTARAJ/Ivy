/*
  Ivy ESP32 firmware (Arduino)
  Sensors:
   - DHT11 (GPIO5)
   - HC-SR04 (TRIG=18, ECHO=19)
   - I2C LCD 16x2 (SDA=21, SCL=22) address 0x27
   - Button (GPIO2, active-low)
   - Buzzer GPIO4 (active HIGH)
   - RGB common-cathode: R=17, G=16, B=15
  Behavior:
   - Boot: white, show "Ivy 🌿" then "Press Button to activate"
   - Button press toggles active, short buzzer on activation
   - While active: poll sensors, update LCD & RGB mapping, check alarms
   - Every 120 seconds: background task sends last reading to backend (device ignores response)
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

// ----- CONFIG -----
const char* WIFI_SSID = "A.I Instaraj";
const char* WIFI_PASS = "Abbr.8080;
const char* SERVER_URL = "http://192.168.43.98:5005/data"; // change to your Flask endpoint

// I2C LCD
#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// Pins
const uint8_t PIN_SDA = 21;
const uint8_t PIN_SCL = 22;
const uint8_t DHT_PIN = 5;   // mapped D5 -> GPIO5
const uint8_t DHT_TYPE = DHT11;
DHT dht(DHT_PIN, DHT_TYPE);

const uint8_t TRIG_PIN = 18;
const uint8_t ECHO_PIN = 19;

const uint8_t BUTTON_PIN = 2; // active-low with internal pullup
const uint8_t BUZZER_PIN = 4; // active HIGH

// RGB common-cathode pins (0-255 PWM)
const uint8_t R_PIN = 17;
const uint8_t G_PIN = 16;
const uint8_t B_PIN = 15;

// Timing
const unsigned long POLL_INTERVAL_MS = 5000;   // update display every 5s while active
const unsigned long SEND_INTERVAL_MS = 120000; // 120s send interval

// Thresholds for continuous alarm
const float TEMP_THRESHOLD = 34.0; // °C
const int HUMID_THRESHOLD = 85;    // %
const float DIST_THRESHOLD_CM = 30.0; // cm

// State
volatile bool Active = false;
unsigned long lastPoll = 0;
unsigned long lastSend = 0;

// Last sensor readings
volatile int lastTemp = -999;
volatile int lastHumid = -999;
volatile float lastDist = -999.0;

// Background send task handle
TaskHandle_t sendTaskHandle = NULL;

// Helper: simple millis-based debounce
unsigned long buttonLastDebounce = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 200;

// Forward
void updateDisplayAndLED();
float measureDistanceCM();
void startBackgroundSendTask();
void sendTask(void* param);
void beep(uint16_t ms);
void setRGB(uint8_t r, uint8_t g, uint8_t b);
void showBootSequence();
void showPressToActivateScreen();

void setup() {
  Serial.begin(115200);

  // pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(R_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);
  pinMode(B_PIN, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // I2C & LCD
  Wire.begin(PIN_SDA, PIN_SCL);
  lcd.init();
  lcd.backlight();

  dht.begin();

  // Boot UI
  showBootSequence();
  delay(1200);
  showPressToActivateScreen();

  // Connect WiFi (do not block forever)
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  unsigned long start = millis();
  while (millis() - start < 8000) { // try 8s
    if (WiFi.status() == WL_CONNECTED) break;
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi not connected (will retry later)");
  }

  // initialize timestamps
  lastPoll = millis();
  lastSend = millis();

  // create send task but suspended initially
  xTaskCreatePinnedToCore(
    sendTask,      // function
    "sendTask",    // name
    4096,          // stack
    NULL,          // param
    1,             // priority
    &sendTaskHandle,
    1              // core
  );
  // suspend immediately; will resume when it's time to send (and only if Active)
  vTaskSuspend(sendTaskHandle);
}

void loop() {
  // button handling (toggle Active)
  int btnState = digitalRead(BUTTON_PIN);
  if (btnState == LOW && millis() - buttonLastDebounce > BUTTON_DEBOUNCE_MS) {
    buttonLastDebounce = millis();
    // debounce passed -> toggle
    Active = !Active;
    if (Active) {
      // short feedback
      beep(80);
      // update timestamps to avoid immediate send
      lastPoll = millis();
      lastSend = millis();
      // resume send task (task will check Active before sending)
      vTaskResume(sendTaskHandle);
    } else {
      // became inactive
      // suspend send task and show idle screen
      vTaskSuspend(sendTaskHandle);
      // show idle state
      setRGB(255, 255, 255); // white
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ivy 🌿");
      lcd.setCursor(0, 1);
      lcd.print("Press button to");
      delay(2000);
      showPressToActivateScreen();
    }
  }

  if (Active) {
    unsigned long now = millis();
    // Poll sensors periodically
    if (now - lastPoll >= POLL_INTERVAL_MS) {
      lastPoll = now;

      // DHT read
      float t = dht.readTemperature(); // Celsius
      float h = dht.readHumidity();

      if (isnan(t) || isnan(h)) {
        Serial.println("DHT read failed");
      } else {
        lastTemp = int(round(t));
        lastHumid = int(round(h));
      }

      // distance
      float dist = measureDistanceCM();
      if (dist > 0) lastDist = dist;

      // update display and LEDs
      updateDisplayAndLED();
    }

    // Sending is handled by background task every SEND_INTERVAL_MS
    // But we need to resume task periodically when SEND_INTERVAL reached
    if (millis() - lastSend >= SEND_INTERVAL_MS) {
      lastSend = millis();
      // Only send when Active
      if (Active && WiFi.status() == WL_CONNECTED) {
        // resume the task to perform a single send
        vTaskResume(sendTaskHandle);
      } else {
        Serial.println("Skipping send (inactive or no WiFi)");
      }
    }
  }

  // small yield
  delay(10);
}

// -------------------- helpers --------------------

void showBootSequence() {
  setRGB(255, 255, 255); // white
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Ivy  🌿   ");
  lcd.setCursor(0, 1);
  lcd.print("   Starting... ");
  delay(1500);
}

void showPressToActivateScreen() {
  setRGB(100, 100, 255); // subtle blue
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Press Button to");
  lcd.setCursor(0,1);
  lcd.print("activate");
  // small animated dots
  for (int i=0; i<3; i++) {
    lcd.print(".");
    delay(400);
  }
}

void updateDisplayAndLED() {
  lcd.clear();
  // display top line: Temp and Humid
  lcd.setCursor(0, 0);
  if (lastTemp > -500) {
    lcd.printf("Temp:%2dC ", lastTemp);
  } else {
    lcd.print("Temp:--C ");
  }
  if (lastHumid > -500) {
    lcd.printf("Humid:%2d%%", lastHumid);
  } else {
    lcd.print("Humid:--%");
  }

  // bottom line: distance
  lcd.setCursor(0, 1);
  if (lastDist > 0) {
    if (lastDist >= 1000) { // unlikely
      lcd.print("Dist: >999cm");
    } else {
      char buf[17];
      snprintf(buf, sizeof(buf), "Dist:%4.0f cm", lastDist);
      lcd.print(buf);
    }
  } else {
    lcd.print("Dist: -- cm");
  }

  // color mapping based on environmental & color theory
  // Use smooth mapping with priority for critical alarms
  bool alarm = false;
  if (lastTemp > TEMP_THRESHOLD) alarm = true;
  if (lastHumid > HUMID_THRESHOLD) alarm = true;
  if (lastDist > 0 && lastDist < DIST_THRESHOLD_CM) alarm = true;

  if (alarm) {
    // continuous alarm: red full, buzzer continuous tone
    setRGB(255, 0, 0);
    // continuous beep: pulse buzzer on repeatedly
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    // turn buzzer off
    digitalWrite(BUZZER_PIN, LOW);

    // temperature color band mapping (comfortable -> green, hot->red, cool->blue)
    if (lastTemp <= 18) {
      // cool - blue
      setRGB(80, 140, 255);
    } else if (lastTemp <= 24) {
      // cool-warm: cyan/teal
      setRGB(60, 200, 200);
    } else if (lastTemp <= 28) {
      // comfortable - green
      setRGB(0, 220, 100);
    } else if (lastTemp <= 34) {
      // warm - amber-ish
      setRGB(255, 160, 60);
    } else {
      // hot - red
      setRGB(255, 60, 60);
    }

    // humidity influence: if very high, add bluish tint
    if (lastHumid > 75) {
      // blend towards blue a bit
      // read current pwm values via mixing (we will recompute)
      // For simplicity, apply a mild blue pulse overlay
      setRGB(120, 120, 255);
    }

    // proximity influence: if object is close (<100cm) slightly increase red portion
    if (lastDist > 0 && lastDist < 100) {
      // small red flash
      setRGB(255, 120, 80);
    }
  }
}

float measureDistanceCM() {
  // HC-SR04 pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // duration in microseconds
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // timeout 30ms -> ~5m
  if (duration == 0) {
    return -1.0;
  }
  float distanceCm = (duration / 2.0) * 0.0343; // speed of sound 343 m/s
  return distanceCm;
}

void beep(uint16_t ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  // common cathode: write PWM HIGH to increase brightness
  ledcSetup(0, 5000, 8); // channel 0
  ledcAttachPin(R_PIN, 0);
  ledcSetup(1, 5000, 8); // channel 1
  ledcAttachPin(G_PIN, 1);
  ledcSetup(2, 5000, 8); // channel 2
  ledcAttachPin(B_PIN, 2);

  ledcWrite(0, r);
  ledcWrite(1, g);
  ledcWrite(2, b);
}

// Background send task: performs one send, then suspends itself.
// It expects global lastTemp/lastHumid/lastDist to be set.
void sendTask(void* param) {
  while (1) {
    // suspend until resumed from main loop
    vTaskSuspend(NULL);

    // Only send if Active and WiFi connected
    if (!Active) {
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[SEND] WiFi not connected; skipping send");
      continue;
    }

    // Build JSON payload
    String payload = "{";
    payload += "\"device_id\":\"ivy-01\",";
    payload += "\"Temp\":" + String(lastTemp) + ",";
    payload += "\"Humid\":" + String(lastHumid) + ",";
    payload += "\"Proxy\":" + String((int)round(lastDist));
    payload += "}";

    Serial.println("[SEND] Posting payload:");
    Serial.println(payload);

    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
      String resp = http.getString();
      Serial.printf("[SEND] HTTP %d\n", httpCode);
      Serial.println("[SEND] Response:");
      Serial.println(resp);
      // per requirement: ignore response (device doesn't display it)
    } else {
      Serial.printf("[SEND] POST failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();

    // done - loop will suspend again at top
  }
}
