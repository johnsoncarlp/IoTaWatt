#include "influxBase.h"
#include "influxEscape.h"

/*****************************************************************************************
 *  Shared by the InfluxDB v1 and v2 uploaders: the line-protocol payload (write state),
 *  the measurement/field/tag configuration and the $name/$units/$device substitution.
 *  Each uploader supplies its write endpoint, its query and its authentication.
 * **************************************************************************************/

/*****************************************************************************************
 *          handle_write_s()
 * **************************************************************************************/
uint32_t influxBase::handle_write_s(){
    trace(_traceModule,60);

    // This is the predominant state of the uploader
    // It will wait until there is enough data for the next post
    // then build the payload and initiate the post operation.

    if(_stop){
        stop();
        return 1;
    }

    // If not enough data to post, set wait and return.

    if( ! dataForPost()){
        freeRecords();
        return UTCtime() + 1;
    }

    // If datalog buffers not allocated, do so now and prime latest.

    trace(_traceModule,60);
    if(! oldRecord){
        trace(_traceModule,61);
        openRecords();
    }

    // Build post transaction from datalog records.

    while(reqData.available() < uploaderBufferLimit && newRecord->UNIXtime < Current_log.lastKey()){

        if(micros() > bingoTime){
            return 10;
        }
        
        // Next interval.

        trace(_traceModule,60);
        nextRecord();

        // Compute the time difference between log entries.
        // If zero, don't bother.

        trace(_traceModule,62);    
        double elapsedHours = newRecord->logHours - oldRecord->logHours;
        if(elapsedHours == 0){
            trace(_traceModule,63);
            if((newRecord->UNIXtime + _interval) <= Current_log.lastKey()){
                return 1;
            }
            return UTCtime() + 1;
        }

        // Build measurements for this interval

        trace(_traceModule,62); 
        String lastMeasurement;
        String thisMeasurement;
        Script *script = _outputs->first();
        while(script)
        {
            trace(_traceModule,63);     
            double value = script->run(oldRecord, newRecord);
            if(value == value){
                trace(_traceModule,64);   
                thisMeasurement = varStr(_measurement, script);
                if(_staticKeySet && thisMeasurement.equals(lastMeasurement)){
                    reqData.printf_P(PSTR(",%s=%.*f"), lpKey(varStr(_fieldKey, script).c_str()).c_str(), script->precision(), value);
                } else {
                    if(lastMeasurement.length()){
                        reqData.printf(" %d\n", oldRecord->UNIXtime);
                    }
                    reqData.write(lpMeasurement(thisMeasurement.c_str()));
                    if(_tagSet){
                        trace(_traceModule,64);
                        influxTag* tag = _tagSet;
                        while(tag){
                            reqData.printf_P(PSTR(",%s=%s"), lpKey(tag->key).c_str(), lpKey(varStr(tag->value, script).c_str()).c_str());
                            tag = tag->next;
                        }
                    }
                    trace(_traceModule,65);
                    reqData.printf_P(PSTR(" %s=%.*f"), lpKey(varStr(_fieldKey, script).c_str()).c_str(), script->precision(), value);
                }
            }
            lastMeasurement = thisMeasurement;
            script = script->next();
        }
        _lastPost = oldRecord->UNIXtime;
        reqData.printf(" %d\n", _lastPost);
    }

    // Add optional heap measurement

    if(_heap){
        reqData.printf_P(PSTR("heap"));
        if(_tagSet){
            trace(_traceModule,66);
            Script *script = _outputs->first();
            influxTag* tag = _tagSet;
            while(tag){
                reqData.printf_P(PSTR(",%s=%s"), lpKey(tag->key).c_str(), lpKey(varStr(tag->value, script).c_str()).c_str());
                tag = tag->next;
            }
        }
        reqData.printf_P(PSTR(" value=%d %d\n"), ESP.getFreeHeap(), UTCtime());
    }       

    freeRecords(); 

    // Initiate HTTP post.

    HTTPPost(writeEndpoint().c_str(), checkWrite_s, "text/plain");
    return 1;
}

/*****************************************************************************************
 *          configLineProtocol(): the settings both uploaders share
 * **************************************************************************************/
bool influxBase::configLineProtocol(JsonObject& config){
    _heap = jBool(config[F("heap")]);

    trace(_traceModule, 101);
    delete[] _measurement;
    _measurement = charstar(jStr(config[F("measurement")]));
    if (!_measurement)
    {
        _measurement = charstar(F("$name"));
    }
    trace(_traceModule, 102);
    delete[] _fieldKey;
    _fieldKey = charstar(jStr(config[F("fieldkey")]));
    if (!_fieldKey)
    {
        _fieldKey = charstar(F("value"));
    }
    _stop = jBool(config[F("stop")]);

    // Build tagSet

    trace(_traceModule, 103);
    delete _tagSet;
    _tagSet = nullptr;
    JsonArray tagset = config[F("tagset")];
    _staticKeySet = true;
    if ( ! tagset.isNull())
    {
        trace(_traceModule, 103);
        for (int i = tagset.size(); i > 0;)
        {
            i--;
            influxTag *tag = new influxTag;
            tag->next = _tagSet;
            _tagSet = tag;
            tag->key = charstar(jStr(tagset[i][F("key")]));
            tag->value = charstar(jStr(tagset[i][F("value")]));
            if ( ! tag->key || ! tag->value)
            {
                log("%s: tagset entry needs a key and a value", _id);
                return false;
            }
            if ((strstr(tag->value, "$units") != nullptr) || (strstr(tag->value, "$name") != nullptr))
                _staticKeySet = false;
        }
    }
    
    // sort the measurements by measurement name

    _outputs->sort([this](Script* a, Script* b)->int {
        return strcmp(varStr(_measurement, a).c_str(), varStr(_measurement, b).c_str());
    });
    return true;
}

String influxBase::varStr(const char* in, Script* script)
{
    // Return String with variable substitutions.

  String out;
  while(*in){ 
    if(strncmp(in,"$device",7) == 0){                  // strncmp: memcmp read past the end of short strings
      out += deviceName;
      in += 7;
    }
    else if(strncmp(in,"$name",5) == 0){
      out += script->name();
      in += 5;
    }
    else if(strncmp(in,"$units",6) == 0){
      out += script->getUnits();
      in += 6;
    }
    else {
      out += *(in++);
    }
  } 
  return out;
}
