#pragma once
#define private public
#include "my_log.hpp"
#include "my_staticslots.hpp"
#include "my_thread.hpp"
#include "static_vector.hpp"
#include <ArduinoJson.h>
#include <DS18B20.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <SafeString.h>
#include <SafeStringReader.h>
#include <array>
#include <utility>

namespace my {

template <typename Dest = void, typename... Arg> constexpr auto make_array(Arg &&...arg) {
	if constexpr (std::is_same<void, Dest>::value)
		return std::array<std::common_type_t<std::decay_t<Arg>...>, sizeof...(Arg)>{
		    {std::forward<Arg>(arg)...}};
	else
		return std::array<Dest, sizeof...(Arg)>{{std::forward<Arg>(arg)...}};
}

template <typename T, std::size_t Capacity> using MyStaticVector = stlpb::static_vector<T, Capacity>;

struct Thread : TH_Thread, PrefixLogger {};

template <size_t SIZE> struct StaticString : Printable {
	char buf[SIZE + 1];
	size_t pos = 0;
	StaticString() {
		buf[0] = 0;
	}
	virtual size_t printTo(Print &p) const {
		return p.write(buf, pos);
	}
	bool add(char c) {
		if (pos == SIZE)
			return false;
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
		return strcmp(buf, o) == 0;
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
	auto c_str() {
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
		if (this->version != VERSION) {
			this->version = VERSION;
			strcpy(this->ssid, "CukierekKawowy");
			strcpy(this->password, "MietowaKuchnia7");
		}
	}

	void save() {
		EEPROM.begin(sizeof(Config));
		EEPROM.put(0, *this);
		EEPROM.commit();
		EEPROM.end();
	}

	void print() {
		Log.infoln("--- Configuration: compiletime=" __DATE__ " " __TIME__ " version=", this->version,
			   " ssid=", this->ssid, " password=", this->password);
	}
};
static_assert(std::is_pod<Config>::value);
extern Config CONFIG;

////////////////////////////////////////////////////////////////////////////////////////

struct WifiThread : Thread {
	void logprefix() {
		Log.info("WIFI:");
	}
	int run() {
		TH_BEGIN();
		while (1) {
			this->infoln("start");
			WiFi.mode(WIFI_STA);
			WiFi.disconnect();
			TH_DELAY(1000);
			if (CONFIG.ssid[0]) {
				this->infoln("Connecting to ", CONFIG.ssid);
				WiFi.begin(CONFIG.ssid, CONFIG.password);
			} else {
				this->infoln("Scan start");
				WiFi.scanNetworks(true);
				int n;
				const int STILL_SCANNING = -1;
				TH_WAIT_WHILE((n = WiFi.scanComplete()) == STILL_SCANNING);
				this->infoln("WiFi.scanComplete=", n);
				if (n < 0) {
					TH_RESTART();
				}
				for (int i = 0; i < n; i++) {
					if (WiFi.encryptionType(i) == ENC_TYPE_NONE) {
						const auto ssid = WiFi.SSID(i);
						this->infoln("Connecting to ", ssid);
						WiFi.begin(ssid);
						break;
					}
				}
			}
			TH_WAIT_WHILE(WiFi.status() != WL_CONNECTED);
			while (WiFi.status() == WL_CONNECTED) {
				this->infoln("Connection IP=", WiFi.localIP(), " RSSI=", WiFi.RSSI());
				TH_DELAY(10000);
			}
			this->infoln("Lost connection!");
		}
		TH_END();
	}
};

////////////////////////////////////////////////////////////////////////////////////////

struct WebServerClient : Thread {
	WiFiClient client;
	StaticString<20> request;
	StaticString<200> post;
	bool header_sent = false;
	bool header_recv = false;

	virtual void logprefix() {
		Log.info("WEBCLIENT:", client.remoteIP(), ":", client.remotePort(), ":");
	}

	void send_header(const char *header, size_t content_length = 0) {
		if (not this->header_sent) {
			this->header_sent = true;
			this->client.print("HTTP/1.1 ");
			this->client.println(header);
			// I am always closing the connectin.
			this->client.println("Connection: close");
			if (content_length != 0) {
				this->client.print("Content-Length: ");
				this->client.println(content_length);
			}
			this->client.println();
		}
	}

	constexpr static const char *STATUS_OK = "200 OK";
	constexpr static const char *STATUS_BAD_REQUEST = "400 Bad Request";
	constexpr static const char *STATUS_NOT_FOUND = "404 Not Found";
	constexpr static const char *STATUS_INTERNAL_SERVER_ERROR = "500 Internal Server Error";
	constexpr static const char *STATUS_SERVICE_UNAVAILABLE = "503 Service Unavailable";
	constexpr static const char *STATUS_INSUFFICIENT_STORAGE = "507 Insufficient Storage";

	int close(const char *reason = nullptr, const char *message = nullptr) {
		if (reason && this->client.connected()) {
			this->send_header(reason, message ? strlen(message) + 1 : 0);
			if (message) {
				this->infoln(message);
				this->client.println(message);
			}
		}
		this->client.stop();
		return TH_EXITED;
	}

	int close_bad_request(const char *message = nullptr) {
		return this->close(STATUS_BAD_REQUEST, message);
	}

	int close_ok(const char *message = nullptr) {
		return this->close(STATUS_OK, message);
	}

	WebServerClient(WiFiClient client) : client(client) {
	}

	enum method {
		METHOD_UNSET = 0,
		METHOD_GET = 1,
		METHOD_POST = 2,
	};

	static const char *method_to_str(enum method method) {
		return method == METHOD_GET ? "GET" : method == METHOD_POST ? "POST" : "";
	}

	struct Router {
		enum method method;
		const char *const path;
		int (WebServerClient::*cb)();
	};


	char prevc = 0;
	enum method method = METHOD_UNSET;

	//////////////////////////////////////////////

	int serve_get_slash() {
		this->send_header(STATUS_OK);
		for (auto &&i : routes) {
			this->client.print(method_to_str(i.method));
			this->client.print(" ");
			this->client.println(i.path);
		}
		return this->close();
	}

	int serve_get_config() {
		JsonDocument doc;
		doc["version"] = CONFIG.version;
		doc["ssid"] = CONFIG.ssid;
		doc["password"] = CONFIG.password;
		// +2 for CRLF. CRLF is the delimiter in HTTP.
		this->send_header(STATUS_OK, measureJson(doc) + 2);
		serializeJson(doc, this->client);
		this->client.println();
		return this->close();
	}

	int serve_post_config() {
		{
			JsonDocument doc;
			deserializeJson(doc, this->client);
			if (0) {
				this->infoln("read: ");
				serializeJson(doc, Serial);
				Log.infoln();
			}
			const int version = doc["version"].as<int>();
			if (version == 0) {
				return this->close_bad_request("version field is invalid or missing");
			}
			if (version != CONFIG.VERSION) {
				return this->close_bad_request("version field is wrong");
			}
			const char *ssid = doc["ssid"];
			if (not ssid) {
				return this->close_bad_request("ssid field is missing");
			}
			const size_t ssid_len = strlen(ssid) + 1;
			if (ssid_len > sizeof(CONFIG.ssid)) {
				return this->close_bad_request("ssid field is too long");
			}
			const char *password = doc["password"];
			if (not password) {
				return this->close_bad_request("password field is missing");
			}
			const size_t password_len = strlen(password) + 1;
			if (password_len > sizeof(CONFIG.password)) {
				return this->close_bad_request("password field is too long");
			}
			// OK
			memcpy(CONFIG.ssid, ssid, ssid_len);
			memcpy(CONFIG.password, password, password_len);
		}
		return this->serve_get_config();
	}

	int serve_get_logsflush() {
		this->send_header(STATUS_OK);
		Log.print_logs_to_loki(client);
		return 0;
	}

	constexpr static const std::array<Router, 4> routes = {
	    Router{METHOD_GET, "/", &WebServerClient::serve_get_slash},
	    Router{METHOD_GET, "/config", &WebServerClient::serve_get_config},
	    Router{METHOD_POST, "/config", &WebServerClient::serve_post_config},
	    Router{METHOD_GET, "/logsflush", &WebServerClient::serve_get_logsflush},
	};

	//////////////////////////////////////////////

	int read_until_space() {
		if (this->client.available()) {
			const char c = this->client.read();
			// this->infoln("read ", c, " from client '", buf.buf, "'");
			if (c == ' ') {
				return TH_BREAK;
			}
			if (!this->request.add(c)) {
				this->infoln("No place in buffer, closing");
				return this->close(STATUS_INSUFFICIENT_STORAGE);
			}
		}
		return 0;
	}

	int read_until_doublenewline() {
		if (this->client.available()) {
			const char c = this->client.read();
			if (c == '\r') {
				return 0;
			}
			// this->infoln("read '", c, "' from client '", this->prevc, "'");
			if (this->prevc == '\n' and c == '\n') {
				return TH_BREAK;
			}
			this->prevc = c;
		}
		return 0;
	}

	int run() {
		int ret;
		if (!this->client.connected()) {
			this->infoln("client disconnected, closing");
			return this->close();
		}
		TH_BEGIN();
		{
			this->infoln("Read METHOD of request line.");
			if (TH_WAIT_WHILE_TIMEOUTED((ret = read_until_space()) == 0, 1000))
				return this->close();
			if (ret == PT_EXITED)
				return this->close();
		}
		{
			this->infoln("Handle METHOD of request line.");
			if (this->request.equal("GET")) {
				this->method = METHOD_GET;
			} else if (this->request.equal("POST")) {
				this->method = METHOD_POST;
			} else {
				return this->close(STATUS_BAD_REQUEST);
			}
			this->request.clear();
		}
		{
			this->infoln("Read URL of request line.");
			if (TH_WAIT_WHILE_TIMEOUTED(read_until_space() == 0, 1000))
				return this->close();
		}
		{
			this->infoln("Ignore the rest of REQUEST.");
			TH_WAIT_WHILE_TIMEOUTED(read_until_doublenewline() == 0, 1000);
		}
		{
			this->infoln("Handle REQUEST ", method_to_str(this->method), " ", this->request);
			for (auto &&route : this->routes) {
				if (this->method == route.method && this->request.equal(route.path)) {
					(this->*route.cb)();
					return this->close(STATUS_INTERNAL_SERVER_ERROR);
				}
			}
			this->infoln("Url not found: ", request.buf);
			this->close(STATUS_NOT_FOUND);
		}
		TH_END();
	}
};

struct WebServerThread : Thread {
	WiFiServer server;
	StaticSlots<WebServerClient, 2> clients;

	virtual void logprefix() {
		Log.info("WEBSERVER:");
	}

	WebServerThread() : server(80) {
	}

	int run() {
		TH_BEGIN();
		TH_WAIT_WHILE(WiFi.status() != WL_CONNECTED);
		this->infoln("begin... ", WiFi.status(), " ", WL_CONNECTED);
		this->server.begin();
		while (1) {
			TH_YIELD();
			WiFiClient newClient = this->server.accept();
			if (newClient) {
				if (!this->clients.emplace_back(newClient)) {
					WebServerClient tmp(newClient);
					tmp.infoln("too many clients to accept");
					tmp.close(tmp.STATUS_SERVICE_UNAVAILABLE);
				} else {
					this->infoln("accepting client from ", newClient.remoteIP(), ":",
						     newClient.remotePort());
				}
			}
			for (auto it = this->clients.begin(); it != this->clients.end(); ++it) {
				// this->infoln("handling client");
				if (TH_IFEXITED(it->run())) {
					this->infoln("erasing client");
					this->clients.erase(it);
				}
			}
		}
		TH_END();
	}
};

/////////////////////////////////////////////////////////////////////////////////////////////////

struct KeepaliveThread : Thread {
	unsigned interval_ms = 10000;
	unsigned i = 0;
	virtual void logprefix() {
		Log.info("KEEPALIVE:");
	}
	int run() {
		TH_BEGIN();
		while (true) {
			this->infoln(i++);
			TH_DELAY(this->interval_ms);
		}
		TH_END();
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
	int dscnt = 0;

	StateThread(int pin) : ds(pin) {
	}

	virtual void logprefix() {
		Log.info("DATA:");
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
		TH_BEGIN();
		while (1) {
			for (dscnt = 0; dscnt < STATE.T.size() && this->ds.selectNext(); ++dscnt) {
				uint8_t address[8];
				this->ds.getAddress(address);
				this->ds.sendCommand(MATCH_ROM, CONVERT_T, !this->ds.selectedPowerMode);
				if (this->ds.selectedPowerMode) {
					TH_WAIT_WHILE(this->ds.oneWire.read_bit());
				} else {
					unsigned resolution = this->ds.selectedResolution;
					unsigned waittime = resolution == 9    ? CONV_TIME_9_BIT
							    : resolution == 10 ? CONV_TIME_10_BIT
							    : resolution == 11 ? CONV_TIME_11_BIT
									       : CONV_TIME_12_BIT;
					TH_DELAY(waittime);
				}
				this->ds.readScratchpad();
				float v = this->getTempC();
				STATE.T[dscnt] = this->ds.getTempC();
				TH_YIELD();
			}
			this->infoln("Detected ", dscnt, " DS18B20 sensors");
			for (int i = 0; i < dscnt; i++) {
				this->infoln("STATE.T[", i, "]=", STATE.T[i]);
			}
			TH_DELAY(5000);
		}
		TH_END();
	}
};

///////////////////////////////////////////////////////////

struct WiFiClientSecureWithWrite : public WiFiClientSecure {
	using WiFiClientSecure::write;
	virtual size_t write(uint8_t c) {
		return this->WiFiClientSecure::write((const uint8_t *)&c, 1);
	}
};

struct ForwardLogsThread : Thread {
	WiFiClientSecureWithWrite client;

	bool should_send_logs() {
		const auto &buf = Log.buffer.buffer;
		return WiFi.status() == WL_CONNECTED && buf.maxSize() < buf.size() * 2;
	}

	int run() {
		const char *url = "loki.kamcuk.top";
		TH_BEGIN();
		while (1) {
			TH_WAIT_WHILE(!should_send_logs());
			if (!client.connect(url, 443)) {
				this->infoln("Connection to loki failed");
			} else {
				client.println("POST /loki/api/v1/push HTTP/1.0");
				client.print("Host: ");
				client.println(url);
				client.println("Connection: close");
				client.println("Content-Type: application/json");
				client.println();
				Log.print_logs_to_loki(client);
			}
		}
		TH_END();
	}
};

///////////////////////////////////////////////////////////////

} // namespace my
