#define LC_INCLUDE "lc-addrlabels.h"
#include "pt.h"

struct pt_delay {
  unsigned long end;
};

#define PT_DELAY_MS(pt, pt_delay, ms) \
  do {  \
    (pt_delay).end = millis() + ms; \
    PT_WAIT_UNTIL(pt, millis() >= pt_delay.end); \
  } while(0)

