#ifndef ntpCheck_h
#define ntpCheck_h

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NTP_UNIX_OFFSET 2208988800UL        // NTP epoch (1900) to UNIX epoch (1970), = SECONDS_PER_SEVENTY_YEARS
#define UNIX2018        1514796044UL        // Earliest plausible time (sanity check)

/*******************************************************************************************************
 * ntpReplyPlausible(flags, trans_ts_sec) - is an NTP reply's transmit time usable?
 *
 * Rejects unsynchronized servers (leap indicator 3) and a zero transmit time (RFC 4330), then sanity
 * checks as UNIX time. NTP seconds roll over in Feb 2036; the uint32 arithmetic here and in
 * NTPtime()/UTCtime() stays correct across that (until 2106), so there is no upper bound. (An upper
 * bound of 2028 stopped all time sync from 2028-01-01.)
 ******************************************************************************************************/
inline bool ntpReplyPlausible(uint8_t flags, uint32_t trans_ts_sec){
  if((flags >> 6) == 3 || trans_ts_sec == 0) return false;
  return (uint32_t)(trans_ts_sec - NTP_UNIX_OFFSET) >= UNIX2018;
}

/*******************************************************************************************************
 * ntpServerName(index, configured, dhcp, nDhcp, out, outLen) - which time server to ask this time
 *
 * Successive requests (index 0, 1, 2, ...) rotate through the servers of the first source, from source
 * `from` on (see ntpFallback()), that has any:
 *   1. configured: the "ntp" device setting, host names or IPv4 addresses separated by commas, spaces
 *      or semicolons. Names that don't fit in out are skipped.
 *   2. dhcp: servers from DHCP option 42 (lwIP sntp_getserver()), IPv4 as lwIP stores them (first
 *      octet in the low byte); 0 = none. Written as dotted quads, so no DNS lookup is needed.
 *   3. default: time1.google.com .. time4.google.com, as before.
 * Writes the name to out (always terminated) and returns the source: 1, 2 or 3 as above.
 ******************************************************************************************************/
enum ntpSource : uint8_t { ntpConfigured = 1, ntpDHCP = 2, ntpDefault = 3 };

inline uint8_t ntpServerName(uint32_t index, const char* configured, const uint32_t* dhcp, int nDhcp,
                             char* out, size_t outLen, uint8_t from = 1){
  static const char* seps = " ,;\t\r\n";
  if(configured && from <= 1){
    int count = 0;                                      // usable configured names
    for(const char* p = configured; *p; ){
      p += strspn(p, seps); size_t len = strcspn(p, seps);
      if(len && len < outLen) count++;
      p += len;
    }
    if(count){
      int want = index % count;
      for(const char* p = configured; *p; ){
        p += strspn(p, seps); size_t len = strcspn(p, seps);
        if(len && len < outLen && want-- == 0){ memcpy(out, p, len); out[len] = 0; return ntpConfigured; }
        p += len;
      }
    }
  }
  int count = 0;
  for(int i = 0; dhcp && from <= 2 && i < nDhcp; i++) if(dhcp[i]) count++;
  if(count){
    int want = index % count;
    for(int i = 0; i < nDhcp; i++){
      if(dhcp[i] && want-- == 0){
        snprintf(out, outLen, "%u.%u.%u.%u", (unsigned)(dhcp[i] & 0xff), (unsigned)(dhcp[i] >> 8 & 0xff),
                 (unsigned)(dhcp[i] >> 16 & 0xff), (unsigned)(dhcp[i] >> 24));
        return ntpDHCP;
      }
    }
  }
  snprintf(out, outLen, "time%u.google.com", (unsigned)(index % 4 + 1));
  return ntpDefault;
}

/*******************************************************************************************************
 * ntpFallback(from, used, failures, ok) - the source to start from next time (B72)
 *
 * A source can have servers that never answer (a router that hands itself out with DHCP option 42 but
 * runs no time server, a mistyped name). After NTP_FALLBACK_FAILURES failed requests in a row to the
 * source used, timeSync moves on to the sources after it; after the defaults fail too, it starts over.
 * A successful sync goes back to the preferred order, so a source that comes back is used again.
 * failures is the count of consecutive failures, updated here.
 ******************************************************************************************************/
#define NTP_FALLBACK_FAILURES 3

inline uint8_t ntpFallback(uint8_t from, uint8_t used, uint8_t& failures, bool ok){
  if(ok){
    failures = 0;
    return ntpConfigured;
  }
  if(++failures < NTP_FALLBACK_FAILURES) return from;
  failures = 0;
  return used < ntpDefault ? used + 1 : ntpConfigured;
}

#endif
