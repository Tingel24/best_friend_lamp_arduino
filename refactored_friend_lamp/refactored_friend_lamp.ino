// Import libraries
#include "config.h"
#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>

// Pin definitions
#define N_LEDS 9
#define BOT 5 // Capacitive sensor pin

// Constants
int lampID = 2;
const int max_intensity = 255;
const int send_selected_color_time = 4000;
const int answer_time_out = 900000;
const int on_time = 900000;
const unsigned long connection_timeout = 300000;
const int long_press_time = 2000;
Adafruit_NeoPixel strip(N_LEDS, 2, NEO_GRB + NEO_KHZ800);
AdafruitIO_Feed *lamp = io.feed("friendship-lamp");

// Variables
int recVal = 0, sendVal = 0, state = 0;
int selected_color = 0, lastState = LOW, currentState;
int i_breath = 0;
char msg[50];
unsigned long RefMillis, ActMillis, currentMillis, previousMillis = 0, pressedTime = 0, releasedTime = 0;
int colors[5][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {0, 0, 0} };

void setup() {
  Serial.begin(115200);
  strip.begin(); strip.show();
  pinMode(BOT, INPUT);
  configureLampID();
  connectToWiFi();
  connectToAdafruitIO();
  lamp->onMessage(handleMessage);
}

void loop() {
  currentMillis = millis();
  io.run();
  updateStateMachine();
  if (millis() - previousMillis >= connection_timeout && WiFi.status() != WL_CONNECTED) ESP.restart();
}

void configureLampID() {
  recVal = lampID == 1 ? 20 : 10;
  sendVal = lampID == 1 ? 10 : 20;
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager;
  wifiManager.setClass("invert");
  wifiManager.setAPCallback([](WiFiManager *) { Serial.println("Enter config mode"); waitConnection(); });
  if (!wifiManager.autoConnect("Lamp", "password123")) { performSpin(0); delay(50); turnOff(); }
  else { performSpin(3); delay(50); turnOff(); }
  Serial.printf("Ready, IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectToAdafruitIO() {
  Serial.printf("\nConnecting to Adafruit IO with User: %s Key: %s.\n", IO_USERNAME, IO_KEY);
  AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, "", "");
  io.connect();
  while (io.status() < AIO_CONNECTED) { Serial.print("."); performSpin(6); delay(500); }
  turnOff();
  performFlash(8);
  lamp->get();
  snprintf(msg, sizeof(msg), "L%d: connected", lampID); lamp->save(msg);
}

void updateStateMachine() {
  switch (state) {
    case 0: waitForButtonPress(); break;
    case 1: prepareColorSelection(); break;
    case 2: selectColor(); break;
    case 3: publishMessage(); break;
    case 4: startAnswerTimer(); break;
    case 5: waitForAnswer(); break;
    case 6: processReceivedAnswer(); break;
    case 7: monitorButtonDuringOn(); break;
    case 8: resetToIdle(); break;
    case 9: prepareResponse(); break;
    case 10: waitForAnswerConfirmation(); break;
    case 11: sendAnswer(); break;
    default: state = 0; break;
  }
}

void waitForButtonPress() {
  currentState = digitalRead(BOT);
  if (lastState == LOW && currentState == HIGH) pressedTime = millis();
  else if (currentState == HIGH && millis() - pressedTime > long_press_time) state = 1;
  lastState = currentState;
}

void prepareColorSelection() {
  selected_color = 0;
  setLEDIntensity(selected_color, max_intensity / 2);
  state = 2;
  RefMillis = millis();
  while (digitalRead(BOT) == HIGH) {}
}

void selectColor() {
  if (digitalRead(BOT) == HIGH) {
    selected_color = (selected_color + 1) % 10;
    while (digitalRead(BOT) == HIGH) { delay(50); }
    setLEDIntensity(selected_color, max_intensity / 2);
    RefMillis = millis();
  }
  if (millis() - RefMillis > send_selected_color_time) state = (selected_color == 9) ? 8 : 3;
}

void publishMessage() {
  snprintf(msg, sizeof(msg), "L%d: color send", lampID); lamp->save(msg);
  lamp->save(selected_color + sendVal);
  Serial.println(selected_color + sendVal);
  performFlash(selected_color);
  setLEDIntensity(selected_color, max_intensity / 2);
  delay(100);
  performFlash(selected_color);
  setLEDIntensity(selected_color, max_intensity / 2);
  state = 4;
}

void startAnswerTimer() {
  RefMillis = millis();
  state = 5;
  i_breath = 0;
}

void waitForAnswer() {
  for (i_breath = 0; i_breath <= 314; i_breath++) {
    performBreath(selected_color, i_breath);
    if (millis() - RefMillis > answer_time_out) {
      turnOff();
      snprintf(msg, sizeof(msg), "L%d: Answer time out", lampID);
      lamp->save(msg); lamp->save(0);
      state = 8;
      break;
    }
  }
}

void processReceivedAnswer() {
  Serial.println("Answer received");
  setLEDIntensity(selected_color, max_intensity);
  snprintf(msg, sizeof(msg), "L%d: connected", lampID);
  lamp->save(msg); lamp->save(0);
  RefMillis = millis();
  state = 7;
}

void monitorButtonDuringOn() {
  if (digitalRead(BOT) == HIGH) {
    lamp->save(420 + sendVal);
    performPulse(selected_color);
  }
  if (millis() - RefMillis > on_time) {
    turnOff();
    lamp->save(0);
    state = 8;
  }
}

void resetToIdle() { turnOff(); state = 0; }

void prepareResponse() {
  snprintf(msg, sizeof(msg), "L%d: msg received", lampID);
  lamp->save(msg);
  RefMillis = millis();
  state = 10;
}

void waitForAnswerConfirmation() {
  for (i_breath = 236; i_breath <= 549; i_breath++) {
    performBreath(selected_color, i_breath);
    if (digitalRead(BOT) == HIGH) { state = 11; break; }
    if (millis() - RefMillis > answer_time_out) {
      turnOff();
      snprintf(msg, sizeof(msg), "L%d: answer time out", lampID);
      lamp->save(msg);
      state = 8;
      break;
    }
  }
}

void sendAnswer() {
  setLEDIntensity(selected_color, max_intensity);
  snprintf(msg, sizeof(msg), "L%d: answer sent", lampID);
  lamp->save(msg); lamp->save(1);
  state = 7;
}

void handleMessage(AdafruitIO_Data *data) {
  int reading = data->toInt();
  if (reading == 66) {
    snprintf(msg, sizeof(msg), "L%d: rebooting", lampID);
    lamp->save(msg); lamp->save(0);
    ESP.restart();
  } else if (reading == 100) {
    snprintf(msg, sizeof(msg), "L%d: ping", lampID);
    lamp->save(msg); lamp->save(0);
  } else if (reading == 420 + recVal) {
    snprintf(msg, sizeof(msg), "L%d: pulse received", lampID);
    lamp->save(msg); lamp->save(0);
    performPulse(selected_color);
  } else if (reading != 0 && reading / 10 != lampID) {
    if (state == 0 && reading != 1) {
      state = 9;
      selected_color = reading - recVal;
    }
    if (state == 5 && reading == 1) state = 6;
  }
}

void turnOff() { setLEDColor(4, max_intensity); }
void setLEDIntensity(int colorIdx, int brightness) { setLEDColor(colorIdx, brightness); }
void performSpin(int colorIdx) { 
  setLEDColor(4, max_intensity);
  for (int i = 0; i < N_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(colors[ind][0], colors[ind][1], colors[ind][2]));
    strip.show();
    strip.show();
    delay(30);
  }
  for (int i = 0; i < N_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(colors[4][0], colors[4][1], colors[4][2]));
    strip.show();
    strip.show();
    delay(30);
  }
 }
void performFlash(int colorIdx) { setLEDColor(colorIdx, max_intensity); delay(200); }
void performPulse(int colorIdx) { 
  for (i_breath = 236; i_breath <= 262; i_breath++) {
    performBreath(selected_color, i_breath);
  }
 }
void performBreath(int colorIdx, int i) {
  float brightness = (max_intensity / 2.0) * (1 + sin(0.02 * i));
  setLEDIntensity(colorIdx, brightness);
}

void setLEDColor(int colorIdx, int brightness) {
  strip.setBrightness(brightness);
  for (int i = 0; i < N_LEDS; i++) strip.setPixelColor(i, strip.Color(colors[colorIdx][0], colors[colorIdx][1], colors[colorIdx][2]));
  strip.show();
}

void waitConnection() { for (int i = 0; i < 3; i++) { performSpin(2); delay(200); } turnOff(); }
