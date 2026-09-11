#define BLYNK_TEMPLATE_ID "TMPL3qYVvLoZP"
#define BLYNK_TEMPLATE_NAME "EV charger"
#define BLYNK_AUTH_TOKEN "2iMAwFncyAGJm6oEonv_pMfGYvoZbhne"


#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h>

WiFiManager wm;
LiquidCrystal_I2C lcd(0x27, 20, 4);


char ssid[] = "RANA";
char pass[] = "Rana@1234";
// ADC Pins
#define IPV1_PIN 36
#define IPV2_PIN 39
#define VBAT_PIN 34
#define VPV2_PIN 35
#define VPV1_PIN 32
#define LED_PIN 2
#define TOUCH_PIN T0   // GPIO 4 (recommended)

#define TOUCH_THRESHOLD 500   // adjust if needed
#define TOUCH_TIME 3000      // 3 seconds

unsigned long touchStartTime = 0;
bool touchActive = false;

#define BAT_SAMPLES 7

float last_ipv1_adc = 0;
float last_ipv2_adc = 0;

#define ADC_HYST 0.01

float batHistory[BAT_SAMPLES] = {0};
int batIndex = 0;

// Calibration
#define GAIN_PV 0.05
#define GAIN_VBAT 0.0502

#define CURRENT_OFFSET 0.21
#define CURRENT_SENS 0.145

#define NUM_SAMPLES 50
#define CURRENT_DEADBAND 0.3

// 🔥 Individual ADC correction factors (adjust these)
#define CORR_IPV1 1
#define CORR_IPV2 1
#define CORR_VBAT 1.105
#define CORR_VPV1 1.09
#define CORR_VPV2 1.14

// ===== Read with correction =====
float readADCVoltage(int pin, float corr) {
  int raw = analogRead(pin);
  float v = raw * (3.3 / 4095.0);
  return v * corr;
}

// ===== Averaging =====
float readAveraged(int pin, float corr, const char* name) {
  float sum = 0;

  // Serial.print(name);
  // Serial.print(" readings: ");

  for (int i = 0; i < NUM_SAMPLES; i++) {
    float val = readADCVoltage(pin, corr);
    sum += val;

    // Serial.print(val, 4);
    // Serial.print(" ");

    delay(5);
  }

  float avg = sum / NUM_SAMPLES;

  // Serial.print(" | AVG: ");
  // Serial.println(avg, 4);

  return avg;
}
void checkTouchReset() {

  int touchValue = touchRead(TOUCH_PIN);
 Serial.print("Touch-");
  Serial.println(touchValue);
  if (touchValue < TOUCH_THRESHOLD) {  // touched

    if (!touchActive) {
    touchStartTime = millis();
      touchActive = true;
    }

    // if held for 3 sec
    if (millis() - touchStartTime > TOUCH_TIME) {
      Serial.println("Touch Reset Triggered!");

      wm.resetSettings();
      delay(500);
      ESP.restart();
    }

  } else {
    touchActive = false;
  }
}


void connectWiFi() {

  Serial.println("[BOOT] Starting WiFi Manager");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // not connected

  wm.setConfigPortalBlocking(false);   // ⭐ IMPORTANT
  wm.autoConnect("PV-Charger-Setup");  // no while loop

}

  float getBatteryPercent(float vbat) {

  // store history
  batHistory[batIndex] = vbat;
  batIndex = (batIndex + 1) % BAT_SAMPLES;

  // average last 7
  float sum = 0;
  for (int i = 0; i < BAT_SAMPLES; i++) {
    sum += batHistory[i];
  }
  float avg = sum / BAT_SAMPLES;

  // 48V lead-acid approx mapping
  float percent = ((avg - 42.0) / (54.0 - 42.0)) * 100.0;

  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;

  return percent;
}

void sendToBlynk(float VPV1, float VPV2, float VBAT,
                 float IPV1, float IPV2,
                 float P1, float P2,
                 float batPercent) {

  Blynk.virtualWrite(V0, VPV1);
  Blynk.virtualWrite(V1, VPV2);
  Blynk.virtualWrite(V2, VBAT);
  Blynk.virtualWrite(V3, IPV1);
  Blynk.virtualWrite(V4, IPV2);
  Blynk.virtualWrite(V5, P1);
  Blynk.virtualWrite(V6, P2);
  Blynk.virtualWrite(V7, batPercent);
  Serial.println("Sending to Blynk...");
}


void setup() {
  Serial.begin(115200);
  

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  analogReadResolution(12);

  analogSetPinAttenuation(IPV1_PIN, ADC_11db);
  analogSetPinAttenuation(IPV2_PIN, ADC_11db);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);
  analogSetPinAttenuation(VPV1_PIN, ADC_11db);
  analogSetPinAttenuation(VPV2_PIN, ADC_11db);
 connectWiFi();
Blynk.config(BLYNK_AUTH_TOKEN);
Blynk.connect();
}

void loop() {
  wm.process();   // ⭐ VERY IMPORTANT

  Serial.println("===== NEW CYCLE =====");

  // ===== Read averaged values =====
  float v_ipv1_adc = readAveraged(IPV1_PIN, CORR_IPV1, "IPV1");
  float v_ipv2_adc = readAveraged(IPV2_PIN, CORR_IPV2, "IPV2");
  float v_vbat_adc = readAveraged(VBAT_PIN, CORR_VBAT, "VBAT");
  float v_vpv1_adc = readAveraged(VPV1_PIN, CORR_VPV1, "VPV1");
  float v_vpv2_adc = readAveraged(VPV2_PIN, CORR_VPV2, "VPV2");

  // ===== Convert =====
  float VPV1 = v_vpv1_adc / GAIN_PV;
  float VPV2 = v_vpv2_adc / GAIN_PV;
  float VBAT = v_vbat_adc / GAIN_VBAT;

 // ===== Hysteresis filter =====
if (abs(v_ipv1_adc - last_ipv1_adc) > ADC_HYST) {
  last_ipv1_adc = v_ipv1_adc;
}
if (abs(v_ipv2_adc - last_ipv2_adc) > ADC_HYST) {
  last_ipv2_adc = v_ipv2_adc;
}

float IPV1 = (last_ipv1_adc - CURRENT_OFFSET) / CURRENT_SENS;
float IPV2 = (last_ipv2_adc - CURRENT_OFFSET) / CURRENT_SENS;
  // ===== Deadband (add this block) =====
// Deadband
if (abs(IPV1) < CURRENT_DEADBAND) IPV1 = 0;
if (abs(IPV2) < CURRENT_DEADBAND) IPV2 = 0;

// No negative current
if (IPV1 < 0) IPV1 = 0;
if (IPV2 < 0) IPV2 = 0;

float P1 = VPV1 * IPV1;
float P2 = VPV2 * IPV2;

if (P1 < 5) P1 = 0;
if (P2 < 5) P2 = 0;
float batPercent = getBatteryPercent(VBAT);

  // ===== SERIAL DEBUG =====
  Serial.println("------ ADC Debug ------");

  Serial.print("VPV1 ADC: "); Serial.print(v_vpv1_adc, 4);
  Serial.print(" V | Real: "); Serial.print(VPV1, 2); Serial.println(" V");

  Serial.print("VPV2 ADC: "); Serial.print(v_vpv2_adc, 4);
  Serial.print(" V | Real: "); Serial.print(VPV2, 2); Serial.println(" V");

  Serial.print("VBAT ADC: "); Serial.print(v_vbat_adc, 4);
  Serial.print(" V | Real: "); Serial.print(VBAT, 2); Serial.println(" V");

  Serial.print("IPV1 ADC: "); Serial.print(v_ipv1_adc, 4);
  Serial.print(" V | Current: "); Serial.print(IPV1, 2); Serial.println(" A");

  Serial.print("IPV2 ADC: "); Serial.print(v_ipv2_adc, 4);
  Serial.print(" V | Current: "); Serial.print(IPV2, 2); Serial.println(" A");

  Serial.print("Power PV1: "); Serial.print(P1, 1); Serial.println(" W");
  Serial.print("Power PV2: "); Serial.print(P2, 1); Serial.println(" W");

  Serial.println("-----------------------\n");

  // ===== LCD =====
  lcd.setCursor(0, 0);
  lcd.print("PV1:");
  lcd.print(VPV1, 1);
  lcd.print("V  ");
  lcd.setCursor(10, 0);
  lcd.print("I1:");
  lcd.print(IPV1, 1);
  lcd.print("A   ");

  lcd.setCursor(0, 1);
  lcd.print("PV2:");
  lcd.print(VPV2, 1);
  lcd.print("V  ");
  lcd.setCursor(10, 1);
  lcd.print("I2:");
  lcd.print(IPV2, 1);
  lcd.print("A   ");



  lcd.setCursor(0, 2);
  lcd.print("P1:");
  lcd.print(P1, 0);
  lcd.print("W   ");
  lcd.setCursor(10, 2);
  lcd.print("P2:");
  lcd.print(P2, 0);
  lcd.print("W   ");

    lcd.setCursor(0, 3);
  lcd.print("BAT:");
  lcd.print(VBAT, 1);
  lcd.print("V   ");

  lcd.setCursor(10, 3);
lcd.print("B:");
lcd.print(batPercent, 0);
lcd.print("%   ");


// send data
  
if (WiFi.status() == WL_CONNECTED) {
  digitalWrite(LED_PIN, LOW);   // connected
  Blynk.run();
} else {
  digitalWrite(LED_PIN, HIGH);  // not connected
}
Serial.print("Blynk: ");
Serial.println(Blynk.connected());
  // your existing code (ADC + LCD) runs ALWAYS

sendToBlynk(VPV1, VPV2, VBAT, IPV1, IPV2, P1, P2, batPercent);

checkTouchReset();
  delay(200);
  
}


