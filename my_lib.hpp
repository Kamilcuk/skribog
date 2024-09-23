#define private public
#include <ESP8266WiFi.h>
#include <SafeString.h>
#include <SafeStringReader.h>
#include <utility>
#include <array>
#include <EEPROM.h>
#include <DS18B20.h>
#include "my_staticslots.hpp"
#include "my_pt.h"
#include "static_vector.hpp"
#include "my_ringbuf.hpp"

namespace my {

template<typename Dest = void, typename... Arg>
constexpr auto make_array(Arg &&...arg) {
  if constexpr (std::is_same<void, Dest>::value)
    return std::array<std::common_type_t<std::decay_t<Arg>...>, sizeof...(Arg)>{ { std::forward<Arg>(arg)... } };
  else
    return std::array<Dest, sizeof...(Arg)>{ { std::forward<Arg>(arg)... } };
}

struct LogPrinter : Print {
  RingBuf<char, 1024> logbuffer;
  virtual size_t write(uint8_t c) {
    logbuffer.pushOverwrite(c);
    return Serial.write(c);
  }
  virtual size_t write(const uint8_t *buffer, size_t size) {
    for (const uint8_t *it = buffer, *const end = buffer + size; it != end; it++) {
      logbuffer.pushOverwrite(*it);
    }
    return Serial.write(buffer, size);
  }
  virtual int availableForWrite() {
    return Serial.availableForWrite();
  }
  virtual void flush() {
    return Serial.flush();
  }
  virtual bool outputCanTimeout() {
    return Serial.outputCanTimeout();
  }
};


extern LogPrinter LOGPRINTER;

namespace log {

template<typename... ARGS>
static void print(ARGS &&...args) {
  (LOGPRINTER.print(args), ...);
}

template<typename... ARGS>
static void log(ARGS &&...args) {
  // print(millis(), ':');
  print(std::forward<ARGS>(args)...);
  LOGPRINTER.println();
}

};

template<typename T, std::size_t Capacity>
using MyStaticVector = stlpb::static_vector<T, Capacity>;

struct PrefixLogger {
  virtual void logprefix() = 0;
  template<typename... T>
  void log(T &&...arg) {
    logprefix();
    log::log(arg...);
  }
};

struct Thread : PrefixLogger {
  struct {
    struct pt _pt {
      {
        NULL
      }
    };
    struct pt *operator->() {
      return &_pt;
    }
  } pt;
  virtual int run() = 0;
};

template<size_t SIZE>
struct MyStaticString {
  char buf[SIZE + 1];
  size_t pos = 0;
  MyStaticString() {
    buf[0] = 0;
  }
  bool add(char c) {
    if (pos == SIZE) return false;
    buf[pos++] = c;
    buf[pos] = 0;
    return true;
  }
  auto begin() {
    return buf;
  }
  auto end() {
    return buf + pos;
  }
  bool equal(const char *o) {
    return strcmp(buf, o);
  }
  bool contains(char c) {
    return memchr(buf, c, pos);
  }
  void clear() {
    pos = 0;
  }
  auto data() {
    return buf;
  }
};

////////////////////////////////////////////////////////////////////////////////////////

struct Config {
  constexpr static const unsigned VERSION = 1;
  unsigned version;
  char ssid[24];
  char password[24];

  void load() {
    EEPROM.begin(sizeof(Config));
    EEPROM.get(0, *this);
    EEPROM.commit();
    EEPROM.end();
    if (version != VERSION) {
      ssid[0] = password[0] = 0;
      strcpy(ssid, "CukierekKawowy");
      strcpy(password, "MietowaKuchnia7");
    }
  }

  void save() {
    EEPROM.begin(sizeof(Config));
    EEPROM.put(0, *this);
    EEPROM.commit();
    EEPROM.end();
  }

  void print() {
    log::log("--- Configuration: version=", version, " ssid=", ssid, " password=", password);
  }
};
static_assert(std::is_pod<Config>::value);
extern Config CONFIG;

////////////////////////////////////////////////////////////////////////////////////////

struct WifiThread : Thread {
  struct pt_delay pt_delay;
  void logprefix() {
    log("WIFI:");
  }
  int run() {
    PT_BEGIN(this->pt);
    this->log("start");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    PT_DELAY_MS(this->pt, pt_delay, 1000);
    if (CONFIG.ssid[0]) {
      this->log("Connecting to ", CONFIG.ssid);
      WiFi.begin(CONFIG.ssid, CONFIG.password);
    } else {
      this->log("Scan start");
      WiFi.scanNetworks(true);
      int n;
      const int STILL_SCANNING = -1;
      PT_WAIT_UNTIL(this->pt, (n = WiFi.scanComplete()) != STILL_SCANNING);
      this->log("WiFi.scanComplete=", n);
      if (n < 0) PT_RESTART(pt);
      for (int i = 0; i < n; i++) {
        if (WiFi.encryptionType(i) == ENC_TYPE_NONE) {
          const auto ssid = WiFi.SSID(i);
          this->log("Connecting to ", ssid);
          WiFi.begin(ssid);
          break;
        }
      }
    }
    PT_WAIT_WHILE(this->pt, WiFi.status() != WL_CONNECTED);
    while (WiFi.status() == WL_CONNECTED) {
      this->log("Connection IP=", WiFi.localIP(), " RSSI=", WiFi.RSSI());
      PT_DELAY_MS(pt, pt_delay, 10000);
    }
    this->log("Lost connection!");
    PT_RESTART(this->pt);
    PT_END(this->pt);
  }
};

////////////////////////////////////////////////////////////////////////////////////////

struct WebServerClient : Thread {
  WiFiClient client;
  MyStaticString<10> buf;
  bool header_sent = false;

  virtual void logprefix() {
    log("WEBCLIENT:", client.remoteIP(), ":", client.remotePort());
  }

  void send_header(const char *str) {
    header_sent = true;
    client.print("HTTP/1.1 ");
    client.log(str);
    client.log();
  }

  void send_header_ok() {
    send_header("200 OK");
  }

  void send_header_not_found() {
    send_header("404 Not Found");
  }

  void send_header_internal_server_error() {
    send_header("500 Internal Server Error");
  }

  void send_header_insufficient_storage() {
    send_header("507 Insufficient Storage");
  }

  void send_header_service_unavailable() {
    send_header("503 Service Unavailable");
  }

  int close() {
    if (this->client and this->client.connected() and not this->header_sent) {
      header_internal_server_error();
    }
    this->client.stop();
    return PT_ENDED;
  }

  WebServerClient(WiFiClient client)
    : client(client) {}

  int serve_get_slash() {
    header_ok();
    this->client.log("Hello world");
    return PT_ENDED;
  }

  int_handler_t serve() {
    std::array<std::pair<const char *, bool (WebServerClient::*)()>, 2> arr = { {
      { "GET /", &WebServerClient::serve_get_slash },
    } };
    for (auto &&i : arr) {
      if (this->buf.equal(i.first)) {
        return (this->*(i.second))();
      }
    }
    this->header_send_not_found();
    return PT_ENDED;
  }


  int run() {
    if (this->client.available()) {
      const char c = this->client.read();
      this->log("read ", c, " from client ", buf.buf);
      if (this->buf.contains(' ') == true and c == ' ') {
        if (!this->serve()) {
          return this->close();
        }
      } else {
        if (!this->buf.add(c)) {
          this->log("No place in buffer, closing");
          this->header_insufficient_storage();
          return this->close();
        }
      }
    } else if (!this->client.connected()) {
      this->log("client disconnected, closing");
      return this->close();
    }
    return PT_YIELDED;
  }
};


struct WebServerThread : Thread {
  WiFiServer server;
  StaticSlots<WebServerClient, 2> clients;

  virtual void logprefix() {
    log("WEBSERVER:");
  }

  WebServerThread()
    : server(80) {}

  int run() {
    PT_BEGIN(this->pt);
    PT_WAIT_WHILE(this->pt, WiFi.status() != WL_CONNECTED);
    this->log("begin... ", WiFi.status(), " ", WL_CONNECTED);
    this->server.begin();
    while (1) {
      PT_YIELD(this->pt);
      WiFiClient newClient = this->server.accept();
      if (newClient) {
        if (!this->clients.emplace_back(newClient)) {
          WebServerClient tmp(newClient);
          tmp.log("too many clients to accept");
          tmp.send_header_service_unavailable();
          tmp.close();
        } else {
          this->log("accepting client from ", newClient.remoteIP(), ":", newClient.remotePort());
        }
      }
      for (auto it = this->clients.begin(); it != this->clients.end(); ++it) {
        this->log("handling client");
        if (it->run() == PT_ENDED) {
          this->log("erasing client");
          this->clients.erase(it);
        }
      }
    }
    PT_END(this->pt);
  }
};

/////////////////////////////////////////////////////////////////////////////////////////////////

struct KeepaliveThread : Thread {
  unsigned interval_ms = 10000;
  unsigned i = 0;
  struct pt_delay pt_delay;
  virtual void logprefix() {
    log("KEEPALIVE:");
  }
  int run() {
    PT_BEGIN(this->pt);
    while (true) {
      this->log(i++);
      PT_DELAY_MS(this->pt, pt_delay, interval_ms);
    }
    PT_END(this->pt);
  }
};

/////////////////////////////////////////////////////////////////////////////////////////////////

struct State {
  std::array<float, 10> T;
  std::array<bool, 10> O;
};
static State STATE;

struct StateThread : Thread {
  DS18B20 ds;
  struct pt_delay pt_delay;

  virtual void logprefix() {
    log("DATA:");
  }

  float getTempC() {
    uint8_t lsb = this->ds.selectedScratchpad[TEMP_LSB];
    uint8_t msb = this->ds.selectedScratchpad[TEMP_MSB];
    switch (this->ds.selectedResolution) {
      case 9:
        lsb &= 0xF8;
        break;
      case 10:
        lsb &= 0xFC;
        break;
      case 11:
        lsb &= 0xFE;
        break;
    }
    uint8_t sign = msb & 0x80;
    int16_t temp = (msb << 8) + lsb;
    if (sign) {
      temp = ((temp ^ 0xffff) + 1) * -1;
    }
    return temp / 16.0;
  }

  int run() {
    PT_BEGIN(this->pt);
    for (int i = 0; i < STATE.T.size() && this->ds.selectNext(); ++i) {
      uint8_t address[8];
      this->ds.getAddress(address);
      this->ds.sendCommand(MATCH_ROM, CONVERT_T, !this->ds.selectedPowerMode);
      if (this->ds.selectedPowerMode) {
        PT_WAIT_UNTIL(this->pt, !this->ds.oneWire.read_bit());
      } else {
        unsigned resolution = this->ds.selectedResolution;
        unsigned waittime = resolution == 9 ? CONV_TIME_9_BIT : resolution == 10 ? CONV_TIME_10_BIT
                                                              : resolution == 11 ? CONV_TIME_11_BIT
                                                                                 : CONV_TIME_12_BIT;
        PT_DELAY_MS(pt, pt_delay, waittime);
      }
      this->ds.readScratchpad();
      float v = this->getTempC();
      STATE.T[i] = this->ds.getTempC();
      PT_YIELD(this->pt);
    }
    PT_YIELD(this->pt);
    PT_END(this->pt);
  }
};

}
