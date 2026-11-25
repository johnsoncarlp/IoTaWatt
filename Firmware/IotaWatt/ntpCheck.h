#ifndef ntpCheck_h
#define ntpCheck_h

#include <stdint.h>

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

#endif
