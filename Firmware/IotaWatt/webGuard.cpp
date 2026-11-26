#include "webGuard.h"
#include <string.h>
#ifdef IOTAWATT_HOST_TEST
#include "host_iotawatt.h"              // PROGMEM, strcmp_P
#else
#include <pgmspace.h>
#endif

        // Requests whose reply is built from logs, measurements or directory scans. Everything else
        // (web UI files, small commands) is light. In flash: RAM is the scarce resource.

static const char heavyUris[][18] PROGMEM = {
  "/status", "/query", "/feed/data.json", "/feed/list.json", "/list", "/graph/getall", "/graph/getallplus"
};

webGuardCost webRequestCost(const char* uri, bool statusDeviceOnly){
  if( ! uri) return webLight;
  for(const auto& h : heavyUris){
    if(strcmp_P(uri, h) == 0){
      return statusDeviceOnly && strcmp(uri, "/status") == 0 ? webLight : webHeavy;
    }
  }
  return webLight;
}

        // The caller's bucket: its own, a free one, or the least recently used (reset for the new caller).

static WebGuardClient& webGuardClient(WebGuard& guard, uint32_t ip, uint32_t nowMs){
  WebGuardClient* oldest = &guard.clients[0];
  for(auto& c : guard.clients){
    if(c.started && c.ip == ip) return c;
  }
  for(auto& c : guard.clients){
    if( ! c.started){ oldest = &c; break; }
    if(nowMs - c.lastMs > nowMs - oldest->lastMs) oldest = &c;
  }
  *oldest = WebGuardClient();
  oldest->ip = ip;
  return *oldest;
}

webGuardReason webGuardCheck(WebGuard& guard, webGuardCost cost, uint32_t freeHeap, uint32_t nowMs, uint32_t clientIP){
  if(freeHeap < WEB_GUARD_MIN_HEAP) return webGuardLowHeap;
  if(cost == webLight) return webGuardOK;
  if(freeHeap < WEB_GUARD_MIN_HEAP_HEAVY) return webGuardLowHeap;

          // Token bucket, kept in milliseconds of credit so nothing is lost to rounding. millis()
          // wraps after 49 days: unsigned subtraction gives the right elapsed time across it.

  WebGuardClient& c = webGuardClient(guard, clientIP, nowMs);
  const uint32_t full = WEB_GUARD_BURST * WEB_GUARD_REFILL_MS;
  if(c.started){
    uint32_t elapsed = nowMs - c.lastMs;
    c.creditMs = elapsed >= full - c.creditMs ? full : c.creditMs + elapsed;
  }
  c.started = true;
  c.lastMs = nowMs;
  if(c.creditMs < WEB_GUARD_REFILL_MS) return webGuardTooFast;
  c.creditMs -= WEB_GUARD_REFILL_MS;
  return webGuardOK;
}

uint32_t webGuardRetryAfter(webGuardReason reason){
  return reason == webGuardLowHeap ? 5 : 1;
}
