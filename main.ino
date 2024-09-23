#include <ESP8266WiFi.h>
#include "my_lib.hpp"

using namespace my;


void setup() {
  Serial.begin(115200);
  delay(5000);
  log::println("Starting...");
  log::println("Starting...");
  log::println("Starting...");
  CONFIG.load();
  CONFIG.print();
}

static WifiThread wifiThread;
static KeepaliveThread keepaliveThread;
static KeepaliveThread keepaliveThread2;
static WebServerThread webServerThread;

void loop() {
  wifiThread.run();
  keepaliveThread.run();
  keepaliveThread2.run();
  webServerThread.run();
}
