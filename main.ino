// vim: ft=cpp

#include "my_lib.hpp"
#include <ESP8266WiFi.h>

using namespace my;

void setup() {
	Serial.begin(115200);
	while (!Serial) continue;
	for (int i = 5; i; --i) {
		delay(1000);
		Log.infoln("Starting... ", i);
	}
	CONFIG.load();
	CONFIG.print();
}

static StateThread stateThread(4);
static WifiThread wifiThread;
static WebServerThread webServerThread;

void loop() {
	wifiThread.run();
	webServerThread.run();
	stateThread.run();
}
