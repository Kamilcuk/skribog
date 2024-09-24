#pragma once
#define LC_INCLUDE "lc-addrlabels.h"
#include "pt.h"
#include <Arduino.h>

namespace my {

#undef PT_WAITING
#undef PT_YIELDED
#undef PT_EXITED
#undef PT_ENDED
enum {
	PT_WAITING = 0,
	PT_YIELDED = 1,
	PT_EXITED = 2,
	PT_ENDED = 3,
	//
	TH_WAITING = PT_WAITING,
	TH_YIELDED = PT_YIELDED,
	TH_EXITED = PT_EXITED,
	TH_ENDED = PT_ENDED,
	//
	TH_BREAK = 1,
	TH_CONTINUE = 0,
};

struct Timer {
	constexpr static unsigned long OFFSET = std::numeric_limits<unsigned long>::max() / 2;
	unsigned long end;
	bool overflowed;
	void arm(unsigned long ms) {
		auto now = millis();
		this->end = now + ms;
		this->overflowed = this->end < now;
		if (this->overflowed) {
			this->end += OFFSET;
		}
	}
	bool fired() {
		auto now = millis();
		if (this->overflowed) {
			now += OFFSET;
		}
		return now >= end;
	}
};

struct TH_Thread {
	Timer timer;
	struct {
		struct pt _pt {
			nullptr
		};
		struct pt *operator->() {
			return &_pt;
		}
	} pt;
	virtual int run() = 0;
};

static inline bool TH_IFEXITED(int ret) {
	return ret >= TH_EXITED;
}

#define TH_BEGIN() PT_BEGIN(this->pt)
#define TH_YIELD() PT_YIELD(this->pt)
#define TH_END() PT_END(this->pt)
#define TH_RESTART() PT_RESTART(this->pt)
#define TH_WAIT_WHILE(condition) PT_WAIT_WHILE(this->pt, condition)

#define TH_DELAY(ms) \
	do { \
		static_assert(std::is_arithmetic<decltype(ms)>::value); \
		this->timer.arm(ms); \
		TH_WAIT_WHILE(!this->timer.fired()); \
	} while (0)

#define TH_WAIT_WHILE_TIMEOUTED(condition, ms) \
	__extension__({ \
		static_assert(std::is_arithmetic<decltype(ms)>::value); \
		this->timer.arm(ms); \
		int fired; \
		TH_WAIT_WHILE(!(fired = this->timer.fired()) && (condition)); \
		fired; \
	})

};

