#include <ESP8266WiFi.h>
#include "my_pt.h"
#include <SafeString.h>
#include <SafeStringReader.h>

template<typename T...>
void println(T &&...arg) {
  (Serial.print(arg), ...);
  Serial.println();
}

template<typename PREFIX>
class Logger {
  template<typename T...>
  void println(T &&..arg) {
    ::println(PREFIX, ' ', arg...);
  }
};

template<typename PREFIX>
class MyThread : Logger<PREFIX> {
  struct pt _pt(NULL);
  int run() {
    return _run(&self->_pt);
  }
  int _run(struct pt *pt);
}

struct config {
  String ssid;
  String password;
};
static struct config CFG;

struct MyWiFiClient : MyThread<"WIFICLIENT"> {
  WiFiClient client;
  createSafeStringReader(sfReader, 5, " ,\r\n");
  uint8_t state = 0;

  MyWiFiClient(WiFiClient client) : client(client) {
    sfReader.connect(this->client);
  }

  bool _run(struct pt *pt) {
    if (!client or !client.connected()) {
      client.stop();
      return false;
    }
    if ()
  }
};

struct ThreadWifi : MyThread<"WIFI">, Stream {
  WiFiServer server(80);
  struct pt_delay pt_delay;
  std::array<MyWiFiClient, 8> clients;  

  int _run(struct pt *pt) {
    PT_BEGIN(pt);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    PT_DELAY_MS(pt, &this->pt_delay, 100);
    if (CFG.ssid) {
      this->println("Connecting to ", CFG.ssid);
      WiFi.begin(CFG.ssid, CFG.password);
    } else {
      this->println("Scan start");
      WiFi.scanNetworks(true);
      int n;
      const int STILL_SCANNING = -1;
      PT_WAIT_UNTIL(pt, (n = WiFi.scanComplete()) != STILL_SCANNING);
      this->println("WiFi.scanComplete=", n);
      if (n < 0) PT_RESTART(pt);
      for (int i = 0; i < n; i++) {
        if (WiFi.encryptionType(i) == ENC_TYPE_NONE) {
          const auto ssid = WiFi.SSID(i);
          sthis->println("Connecting to ", ssid);
          WiFi.begin(ssid);
          break;
        }
      }
    }
    PT_WAIT_UNTIL(pt, WiFi.status() != WL_CONNECTED);
    this->println("Connection established! IP address: ", WiFi.localIP());
    PT_YIELD(pt);
    {
        WiFiClient newClient = server.accept();
        if (newClient) {
        }
    }
    PT_END(pt);
  }
};