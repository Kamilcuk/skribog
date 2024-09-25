#pragma once
#include "my_ringbuf.hpp"
#include <ArduinoJson.h>
#include <DS18B20.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <SafeString.h>
#include <SafeStringReader.h>

namespace my {

union UlongAndBytes {
	unsigned long ul;
	unsigned char c[sizeof(unsigned long)];
};

struct TimeAndLineStorage {
	RingBuf<uint8_t, 2048> buffer;
	bool linestarted = false;

	void write_startline_time() {
		buffer.pushOverwrite('\x01');
		UlongAndBytes now = {millis()};
		for (int i = 0; i < sizeof(now.c); ++i) {
			buffer.pushOverwrite(now.c[i]);
		}
	}
	void write(uint8_t c) {
		if (c == '\x01' || c == '\r') {
			return;
		}
		if (!linestarted) {
			linestarted = true;
			write_startline_time();
		}
		if (c == '\n') {
			linestarted = false;
		} else {
			buffer.pushOverwrite(c);
		}
	}

	bool read_startline_time(unsigned long &ret) {
		uint8_t c;
		while (1) {
			if (!buffer.pop(c)) {
				return false;
			}
			if (c == '\x01') {
				break;
			}
		}
		UlongAndBytes now;
		for (int i = 0; i < sizeof(now.c); ++i) {
			if (!buffer.pop(now.c[i])) {
				return 0;
			}
		}
		ret = now.ul;
		return true;
	}
	bool read_line(uint8_t &c) {
		if (!buffer.peek(c)) {
			return false;
		}
		if (c == '\x01') {
			return false;
		}
		buffer.pop(c);
		return true;
	}
};

struct LogPrinter : Print {
	TimeAndLineStorage buffer;
	virtual size_t write(uint8_t c) {
		this->buffer.write(c);
		return Serial.write(c);
	}
	virtual size_t write(const uint8_t *buffer, size_t size) {
		for (const uint8_t *it = buffer, *const end = buffer + size; it != end; it++) {
			this->buffer.write(*it);
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

	template <typename... ARGS> void info(ARGS &&...args) {
		(print(args), ...);
	}
	template <typename... ARGS> void infoln(ARGS &&...args) {
		(print(args), ...);
		println();
	}

	template <typename T> void print_logs_to_loki(T &client, unsigned long timeoffset = 0) {
		// {"streams":[{"stream":[{"label":"value"}],"values":[["nanoseconds", "logline"]]}}
		client.print("{\"streams\":[{\"stream\":{");
		{
			client.print("\"source\":\"stribog\",");
			client.print("\"mac\":\"");
			client.print(WiFi.macAddress());
			client.print("\"");
		}
		client.print("},\"values\":[");
		{
			ArduinoJson::detail::TextFormatter<T> tf(client);
			unsigned long now;
			bool first = true;
			while (buffer.read_startline_time(now)) {
				if (!first) {
					tf.writeRaw(',');
				}
				first = false;
				tf.writeRaw('[');
				tf.writeRaw('"');
				tf.writeInteger(now + timeoffset);
				tf.writeRaw('"');
				tf.writeRaw(',');
				tf.writeRaw('"');
				uint8_t c;
				while (buffer.read_line(c)) {
					tf.writeChar(c);
				}
				tf.writeRaw('"');
				tf.writeRaw(']');
			}
		}
		client.print("]}]}");
	}

	template <typename T> void print_logs_no_flush(T &client, unsigned long timeoffset = 0) {
		uint8_t c;
		unsigned long now;
		size_t i = 0;
		int state = 0;
		for (; i < buffer.buffer.size(); ++i) {
			if (!buffer.buffer.peek(c, i)) {
				break;
			}
			switch (state) {
			case 0:
				if (c == '\x01') {
					state++;
				}
				break;
			case 1:
			case 2:
			case 3:
			case 4:
				now >>= 8;
				now |= c << 24;
				state++;
				break;
			case 5:
				client.print(now + timeoffset);
				client.print(' ');
				state++;
			default:
				client.print((char)c);
				if (c == '\x01') {
					client.println();
					state = 1;
				}
				break;
			}
		}
	}
};

extern LogPrinter Log;

struct PrefixLogger {
	virtual void logprefix() = 0;
	template <typename... T> void infoln(T &&...arg) {
		this->logprefix();
		Log.infoln(arg...);
	}
};

}; // namespace my
