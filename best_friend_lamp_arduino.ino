//import the libraries

#include "config.h"
#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>

#define N_LEDS 10
//#define LED_PIN 4 // Not used on esp8266 Platform
#define BOT 5  // capacitive sensor pin

//////////////////LAMP ID////////////////////////////////////////////////////////////
int lampID = 2;
/////////////////////////////////////////////////////////////////////////////////////

Adafruit_NeoPixel strip(N_LEDS, 2, NEO_GRB + NEO_KHZ800);

// Adafruit inicialization
AdafruitIO_Feed *lamp = io.feed("friendship-lamp");  // Change to your feed

int recVal{ 0 };
int sendVal{ 0 };

const int max_intensity = 255;  //  Max intensity

int selected_color = 0;  //  Index for color vector

int i_breath;

char msg[50];  //  Custom messages for Adafruit IO

//  Color definitions
#define N_COLORS 11
int colors[N_COLORS][3] = {
  { 124, 0, 254 },  // Purple
  { 249, 228, 0 },
  { 253, 166, 0 },
  { 201, 247, 249 },
  { 251, 209, 209 },
  { 212, 249, 198 },
  { 249, 246, 195 },
  { 250, 132, 43 },
  { 0, 56, 255 },
  { 50, 174, 0 },
  { 255, 255, 255 },
};
int black[3] = { 0, 0, 0 };
#define BLACK -1

int state = 0;

// Time vars
unsigned long RefMillis;
unsigned long ActMillis;
const int send_selected_color_time = 4000;
const int answer_time_out = 900000;
const int on_time = 900000;

// Disconection timeout
unsigned long currentMillis;
unsigned long previousMillis = 0;
const unsigned long conection_time_out = 300000;  // 5 minutos

// Long press detection
const int long_press_time = 2000;
int lastState = LOW;  // the previous state from the input pin
int currentState;     // the current reading from the input pin
unsigned long pressedTime = 0;
unsigned long releasedTime = 0;


void show(int led, int color) {
  if (color == BLACK) {
    strip.setPixelColor(led, strip.Color(black[0], black[1], black[2]));
  }
  strip.setPixelColor(led, strip.Color(colors[color][0], colors[color][1], colors[color][2]));
}

void show_all(int color, int d = 0) {
  for (int i = 0; i < N_LEDS; i++) {
    show(i, color);
    if (d > 0) {
      strip.show();
      delay(d);
    }
  }
}

void setup() {
  //Start the serial monitor for debugging and status
  Serial.begin(115200);

  // Activate neopixels
  strip.begin();
  strip.show();
  strip.show();
  wificonfig();

  pinMode(BOT, INPUT);

  //  Set ID values
  if (lampID == 1) {
    recVal = 20;
    sendVal = 10;
  } else if (lampID == 2) {
    recVal = 10;
    sendVal = 20;
  }

  //start connecting to Adafruit IO
  Serial.printf("\nConnecting to Adafruit IO with User: %s Key: %s.\n", IO_USERNAME, IO_KEY);
  AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, "", "");
  io.connect();

  lamp->onMessage(handle_message);

  while (io.status() < AIO_CONNECTED) {
    Serial.print(".");
    spin(6);
    delay(500);
  }
  turn_off();
  Serial.println();
  Serial.println(io.statusText());
  // Animation
  spin(3);
  turn_off();
  delay(50);
  flash(8);
  turn_off();
  delay(100);
  flash(8);
  turn_off();
  delay(50);

  //get the status of our value in Adafruit IO
  lamp->get();
  sprintf(msg, "Lamp %d: connected", lampID);
  lamp->save(msg);
}

#define STATE_TURNED_OFF 0
#define STATE_TURNED_ON 1
#define STATE_COLOR_SELECTOR 2
#define STATE_SEND_COLOR 3
#define STATE_START_ANSWER_WAIT 4
#define STATE_ANSWER_WAIT 5
#define STATE_ANSWER_RECEIVED 6
#define STATE_HAVE_FRIENDSHIP 7
#define STATE_TURN_OFF 8
#define STATE_MESSAGE_RECEIVED 9
#define STATE_SEND_ANSWER_WAIT 10
#define STATE_SEND_ANSWER 11


void loop() {
  currentMillis = millis();
  io.run();
  // State machine
  switch (state) {
    case STATE_TURNED_OFF:
      {
        currentState = digitalRead(BOT);
        if (lastState == LOW && currentState == HIGH)  // Button is pressed
        {
          pressedTime = millis();
        } else if (currentState == HIGH) {
          releasedTime = millis();
          long pressDuration = releasedTime - pressedTime;
          if (pressDuration > long_press_time) {
            state = STATE_TURNED_ON;
          }
        }
        lastState = currentState;
        break;
      }
    case STATE_TURNED_ON:
      Serial.println("Turned on");
      selected_color = 0;
      light_half_intensity(selected_color);
      RefMillis = millis();
      while(digitalRead(BOT) == HIGH){}

      state = STATE_COLOR_SELECTOR;
      break;
    case STATE_COLOR_SELECTOR:
      if (digitalRead(BOT) == HIGH) {
        Serial.println("Selecting Color");
        selected_color++;
        if (selected_color == N_COLORS)
          selected_color = BLACK;
        light_half_intensity(selected_color);
        RefMillis = millis();
        while (digitalRead(BOT) == HIGH) {}
      }
      // If a color is selected more than a time, it is interpreted as the one selected
      ActMillis = millis();
      if (ActMillis - RefMillis > send_selected_color_time) {
        if (selected_color == BLACK)  //  Cancel msg
          state = STATE_TURN_OFF;
        else
          state = STATE_SEND_COLOR;
      }
      break;
    case STATE_SEND_COLOR:
      Serial.printf("Sending color %d \n", selected_color);

      sprintf(msg, "Lamp %d: sending color:", lampID);
      lamp->save(msg);
      lamp->save(selected_color + sendVal);


      flash(selected_color);
      light_half_intensity(selected_color);
      delay(100);
      flash(selected_color);
      light_half_intensity(selected_color);

      state = STATE_START_ANSWER_WAIT;
      break;
    case STATE_START_ANSWER_WAIT:
      RefMillis = millis();

      state = STATE_ANSWER_WAIT;
      break;
    case STATE_ANSWER_WAIT:
      Serial.println("Waiting for Answer...");
      // Waiting for answer, state will change by handle_message when answer is received
      for (i_breath = 0; i_breath <= 314; i_breath++) {
        breath(selected_color, i_breath);
        ActMillis = millis();
        if (ActMillis - RefMillis > answer_time_out) {
          turn_off();
          lamp->save("Lamp %d: Answer time out", lampID);
          lamp->save(0);
          state = STATE_TURN_OFF;
          break;
        }
      }
      break;
    case STATE_ANSWER_RECEIVED:
      Serial.println("Answer received");
      light_full_intensity(selected_color);
      RefMillis = millis();
      sprintf(msg, "Lamp %d: connected", lampID);
      lamp->save(msg);
      lamp->save(0);
      state = STATE_HAVE_FRIENDSHIP;
      break;
      // Turned on
    case STATE_HAVE_FRIENDSHIP:
      ActMillis = millis();
      //  Send pulse
      if (digitalRead(BOT) == HIGH) {
        lamp->save(420 + sendVal);
        pulse(selected_color);
      }
      if (ActMillis - RefMillis > on_time) {
        turn_off();
        lamp->save(0);
        state = STATE_TURN_OFF;
      }
      break;
    case STATE_TURN_OFF:
      turn_off();
      state = STATE_TURNED_OFF;
      break;
    case STATE_MESSAGE_RECEIVED:
      sprintf(msg, "Lamp %d: msg received", lampID);
      lamp->save(msg);
      RefMillis = millis();
      state = STATE_SEND_ANSWER_WAIT;
      break;
    case STATE_SEND_ANSWER_WAIT:
      for (i_breath = 236; i_breath <= 549; i_breath++) {
        breath(selected_color, i_breath);
        if (digitalRead(BOT) == HIGH) {
          state = STATE_SEND_ANSWER;
          break;
        }
        ActMillis = millis();
        if (ActMillis - RefMillis > answer_time_out) {
          turn_off();
          sprintf(msg, "%d: answer time out", lampID);
          lamp->save(msg);
          state = STATE_TURN_OFF;
          break;
        }
      }
      break;
    case STATE_SEND_ANSWER:
      light_full_intensity(selected_color);
      RefMillis = millis();
      sprintf(msg, "%d: answer sent", lampID);
      lamp->save(msg);
      lamp->save(1);
      state = STATE_HAVE_FRIENDSHIP;
      break;
    default:
      state = STATE_TURNED_OFF;
      break;
  }
  if ((currentMillis - previousMillis >= conection_time_out)) {
    if (WiFi.status() != WL_CONNECTED)
      wificonfig();
    previousMillis = currentMillis;
  }
}

//code that tells the ESP8266 what to do when it recieves new data from the Adafruit IO feed
void handle_message(AdafruitIO_Data *data) {

  //convert the recieved data to an INT
  int reading = data->toInt();
  if (reading == 66) {
    sprintf(msg, "Lamp %d: rebooting", lampID);
    lamp->save(msg);
    lamp->save(0);
    ESP.restart();
  } else if (reading == 100) {
    sprintf(msg, "Lamp %d: ping", lampID);
    lamp->save(msg);
    lamp->save(0);
  } else if (reading == 420 + recVal) {
    sprintf(msg, "Lamp %d: pulse received", lampID);
    lamp->save(msg);
    lamp->save(0);
    pulse(selected_color);
  } else if (reading != 0 && reading / 10 != lampID) {
    // Is it a color msg?
    if (state == STATE_TURNED_OFF && reading != 1) {
      state = STATE_MESSAGE_RECEIVED;
      selected_color = reading - recVal;
    }
    // Is it an answer?
    if (state == STATE_ANSWER_WAIT && reading == 1)
      state = STATE_ANSWER_RECEIVED;
  }
}

void turn_off() {
  strip.setBrightness(max_intensity);
  show_all(BLACK);
  strip.show();
}

// 50% intensity
void light_half_intensity(int color) {
  strip.setBrightness(max_intensity / 2);
  show_all(color);
  strip.show();
}

// 100% intensity
void light_full_intensity(int color) {
  strip.setBrightness(max_intensity);
  show_all(color);
  strip.show();
}

void pulse(int color) {
  int i;
  int i_step = max_intensity / 50;
  for (i = max_intensity; i > max_intensity / 2; i -= i_step) {
    strip.setBrightness(i);
    for (int i = 0; i < N_LEDS; i++) {
      show(i, color);
      strip.show();
      delay(1);
    }
  }
  delay(20);
  for (i = max_intensity / 2; i < max_intensity; i += i_step) {
    strip.setBrightness(i);
    for (int i = 0; i < N_LEDS; i++) {
      show(i, color);
      strip.show();
      delay(1);
    }
  }
}

//The code that creates the gradual color change animation in the Neopixels (thank you to Adafruit for this!!)
void spin(int color) {
  strip.setBrightness(max_intensity);
  for (int i = 0; i < N_LEDS; i++) {
    show(i, color);
    strip.show();
    delay(30);
  }
  for (int i = 0; i < N_LEDS; i++) {
    show(i, BLACK);
    strip.show();
    delay(30);
  }
}

// Inspicolors[0] by Jason Yandell
void breath(int color, int i) {
  float MaximumBrightness = max_intensity / 2;
  float SpeedFactor = 0.02;
  float intensity;
  if (state == STATE_ANSWER_WAIT)
    intensity = MaximumBrightness / 2.0 * (1 + cos(SpeedFactor * i));
  else
    intensity = MaximumBrightness / 2.0 * (1 + sin(SpeedFactor * i));
  strip.setBrightness(intensity);
  show_all(color, 1);
}

//code to flash the Neopixels when a stable connection to Adafruit IO is made
void flash(int color) {
  light_full_intensity(color);
  delay(200);
}

// Waiting connection led setup
void wait_connection() {
  strip.setBrightness(max_intensity);
  int parts = 3;
  int part_length = N_LEDS / parts;
  for (int part = 0; part < parts; part++) {
    for (int i = part * part_length; i < (part + 1) * part_length; i++) {
      show(i, part);
    }
    strip.show();
    delay(50);
  }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Enter config mode");
  wait_connection();
}

void wificonfig() {
  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager;

  std::vector<const char *> menu = { "wifi", "info" };
  wifiManager.setMenu(menu);
  // set dark theme
  wifiManager.setClass("invert");

  bool res;
  wifiManager.setAPCallback(configModeCallback);
  res = wifiManager.autoConnect("Lamp", "password123");  // password protected ap

  if (!res) {
    spin(0);
    delay(50);
    turn_off();
  } else {
    //if you get here you have connected to the WiFi
    spin(3);
    delay(50);
    turn_off();
  }
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
