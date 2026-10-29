#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "node.h"
#include "storage.h"

Node node;

void setup() {
  Serial.begin(115200);
  randomSeed(RANDOM_REG32);  // hardware RNG
  WiFi.persistent(false);    // don't write Wi-Fi config to flash on every change
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // modem sleep would make the master miss ESP-NOW packets
  WiFi.disconnect();
  if (!storage::begin()) Serial.println(F("[boot] LittleFS failed: settings won't save, web app won't load"));
  node.begin(millis());
}

void loop() { node.loop(millis()); }
