/*******************************************************************************************************
 *  Local time and daylight saving rules: UTC2Local(), local2UTC(), testRule().
 *  Moved unchanged from timeServices.cpp so they can be unit tested on the host (test/test_timerules).
 ******************************************************************************************************/
#ifdef IOTAWATT_HOST_TEST
#include "host_iotawatt.h"           // host unit tests (Firmware/test/shim)
#include "timeServices.h"
extern int32_t localTimeDiff;
extern tzRule* timezoneRule;
#else
#include "IotaWatt.h"
#endif

uint32_t UTC2Local(uint32_t UTCtime){
    uint32_t result = UTCtime + localTimeDiff * 60;
    if( ! timezoneRule) return result;

    if(timezoneRule->begPeriod.month <= timezoneRule->endPeriod.month) {
      if((testRule(timezoneRule->useUTC ? UTCtime : result, timezoneRule->begPeriod) &&
        !testRule(timezoneRule->useUTC ? UTCtime : result, timezoneRule->endPeriod)) &&
        !testRule(timezoneRule->useUTC ? UTCtime : result+timezoneRule->adjMinutes*60, timezoneRule->endPeriod)){ 
        result += timezoneRule->adjMinutes * 60;
      }
    } else {
      result += timezoneRule->adjMinutes * 60;
      if(testRule(timezoneRule->useUTC ? UTCtime : result, timezoneRule->endPeriod)){
        result -= timezoneRule->adjMinutes * 60;
      } 
      if(testRule(timezoneRule->useUTC ? UTCtime : result, timezoneRule->begPeriod)){
        result += timezoneRule->adjMinutes * 60;
      } 
    }
    return result;
}

uint32_t  local2UTC(uint32_t localTime){
  uint32_t trialUTC = localTime - localTimeDiff * 60;
  uint32_t testLocal = UTC2Local(trialUTC);
  return trialUTC - testLocal + localTime;
}

bool testRule(uint32_t standardTime, dateTimeRule dtrule){
    uint8_t daysInMonth[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    DateTime now = DateTime(standardTime);
    if(now.month() < dtrule.month) return false;
    if(now.month() > dtrule.month) return true;
    int fwdm = ((standardTime-(now.day()-1)*86400)/86400+4)%7+1;     // weekday of first day in month
    int lwdm = (fwdm+daysInMonth[now.month()-1]-2)%7+1;         // weekday of last day in month
    int startDay;
    if(dtrule.instance > 0){
        startDay = (dtrule.weekday-fwdm+7)%7+1+(dtrule.instance-1)*7;
    } else {
        startDay = daysInMonth[now.month()-1]-(lwdm-dtrule.weekday+7)%7+dtrule.instance*7+7;
    }
    if(now.day() < startDay) return false;
    if(now.day() > startDay) return true;
    return (now.hour()*60+now.minute()) >= dtrule.time;
}
