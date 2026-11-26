#ifndef webGuard_h
#define webGuard_h

/*******************************************************************************************************
 * Web server overload guard
 *
 * The web server handles one request at a time, between measurement cycles. Nothing stopped a caller
 * that polls too fast (or a stuck script) from keeping it busy, or a large reply from being started
 * when the heap is nearly exhausted, which can restart the device. Before a request is handled,
 * webGuardCheck() decides whether to answer "503 Service Unavailable" with a Retry-After instead:
 *
 *  - free heap below WEB_GUARD_MIN_HEAP: every request is refused (static files included);
 *  - "heavy" requests (status, queries, feed data, file lists, graph lists) also need
 *    WEB_GUARD_MIN_HEAP_HEAVY, and take a token from the caller's bucket, which holds WEB_GUARD_BURST
 *    tokens and gains one every WEB_GUARD_REFILL_MS. Bursts (Graph+ loading, a dashboard refreshing)
 *    pass; a caller asking faster than that, sustained, is told to retry.
 *
 * Each caller (remote IP) has its own bucket, WEB_GUARD_CLIENTS of them, the least recently used one
 * reused for a new caller: a script polling too fast is refused, not Home Assistant or the web UI
 * beside it. Buckets are charged before authentication, but only the caller's own. The small device
 * status request the menu bar makes on every page (/status?device=yes) is light.
 *
 * The logic is pure (heap and time are arguments), so it is unit tested on the host.
 ******************************************************************************************************/

#include <stdint.h>

#define WEB_GUARD_MIN_HEAP        6000      // bytes: below this, refuse everything
#define WEB_GUARD_MIN_HEAP_HEAVY 10000      // bytes: below this, refuse heavy requests
#define WEB_GUARD_BURST              6      // heavy requests allowed back to back
#define WEB_GUARD_REFILL_MS        400      // one more heavy request allowed every this many ms
#define WEB_GUARD_CLIENTS            4      // callers tracked separately

enum webGuardCost { webLight, webHeavy };
enum webGuardReason { webGuardOK = 0, webGuardLowHeap, webGuardTooFast };

struct WebGuardClient {
  uint32_t ip = 0;
  uint32_t creditMs = WEB_GUARD_BURST * WEB_GUARD_REFILL_MS;   // bucket in ms: a heavy request costs
  uint32_t lastMs = 0;                                         // WEB_GUARD_REFILL_MS, time adds 1 per ms
  bool     started = false;
};

struct WebGuard {
  WebGuardClient clients[WEB_GUARD_CLIENTS];
  uint32_t refusedHeap = 0;                         // counts since the last log line
  uint32_t refusedFast = 0;
  uint32_t lastLogMs = 0;
  bool     logged = false;
};

webGuardCost   webRequestCost(const char* uri, bool statusDeviceOnly = false);
webGuardReason webGuardCheck(WebGuard& guard, webGuardCost cost, uint32_t freeHeap, uint32_t nowMs, uint32_t clientIP = 0);
uint32_t       webGuardRetryAfter(webGuardReason reason);   // seconds, for the Retry-After header

#endif
