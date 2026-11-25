#include "influxDB_uploader.h"
#include "splitstr.h"
#include "influxEscape.h"

Uploader *influxDB_v1 = nullptr;

    /*****************************************************************************************
     *          handle_query_s()
     * **************************************************************************************/
    uint32_t
    influxDB_uploader::handle_query_s()
{

    trace(T_influx1,20);
    if (!_script) {
        _script = _outputs->first();
    }
    reqData.flush();
    reqData.print(F("db="));
    reqData.print(formEncode(_database));
    reqData.print(F("&epoch=s"));
    if(_retention){
        reqData.print(F("&rp="));
        reqData.print(formEncode(_retention));
    }
    trace(T_influx1,20);
            // Quoted names and values (B50); the whole query is one form-encoded value.
    String query(F("SELECT LAST("));
    query += iqlIdent(varStr(_fieldKey, _script).c_str());
    query += F(") FROM ");
    query += iqlIdent(varStr(_measurement, _script).c_str());
    influxTag *tag = _tagSet;
    while(tag){
        query += tag == _tagSet ? F(" WHERE ") : F(" AND ");
        query += iqlIdent(tag->key);
        query += '=';
        query += iqlString(varStr(tag->value, _script).c_str());
        tag = tag->next;
    }
    reqData.print(F("&q="));
    reqData.print(formEncode(query.c_str()));
    
        // Send the request

    trace(T_influx1,20);
    HTTPPost("/query", checkQuery_s, "application/x-www-form-urlencoded");
    return 1;
}

/*****************************************************************************************
 *          handle_checkQuery_s()
 * **************************************************************************************/
uint32_t influxDB_uploader::handle_checkQuery_s(){
    trace(T_influx1,30);
    int HTTPcode = _request->responseHTTPcode();
    
    if(HTTPcode == 401){
        log("%s: Authentication failed. Stopping influx service.", _id);
        stop();
        return 1;
    }
        // Check for invalid request.

    if(HTTPcode != 200){
        trace(T_influx1,31);
        char message[100];
        if(HTTPcode < 0){
            trace(T_influx1,32);
            sprintf_P(message, PSTR("Query failed, code %d"), HTTPcode);
        }
        else {
            trace(T_influx1,33);
            sprintf_P(message, PSTR("Query failed, code %d, response: %.50s"), HTTPcode, _request->responseText().c_str());
        }
        trace(T_influx1,31);
        _statusMessage = charstar(message);
        _script = nullptr;
        delay(5, query_s);
        return 15;
    }

        // Json parse the response to get the columns and values arrays
        // and extract time

    String response = _request->responseText();
    trace(T_influx1,35);
    JsonDocument Json;
    DeserializationError parseError = deserializeJson(Json, response);
    JsonObject results = Json.as<JsonObject>();
    if( ! parseError && ! results.isNull()){
        trace(T_influx1,35);
        JsonArray columns = results["results"][0]["series"][0]["columns"];
        JsonArray values = results["results"][0]["series"][0]["values"][0];
        if( ! columns.isNull() && ! values.isNull()){
            trace(T_influx1,35);
            for(int i=0; i<columns.size(); i++){
                trace(T_influx1,35);
                const char* column = columns[i].as<const char*>();     // null if the server sent a non-string
                if(column && strcmp("time", column) == 0){
                    _lastSent = MAX(_lastSent, jInt<unsigned long>(values[i]));
                    break;
                }
            }
        }
    }
    
    trace(T_influx1,37);
    _script = _script->next();
    if(_script){
        _state = query_s;
        return 1;
    }
    trace(T_influx1,37);

    _lastSent = MAX(_lastSent, _uploadStartDate);
    if(_lastSent == 0){
        _lastSent = Current_log.lastKey();
    }
    _lastSent = MAX(_lastSent, Current_log.firstKey());
    _lastSent -= _lastSent % _interval;
    trace(T_influx1,37);
    if( ! _stop){
        log("%s: Start posting at %s", _id, localDateString(_lastSent + _interval).c_str());
    }
    _state = write_s;
    return 1;
}

/*****************************************************************************************
 *          handle_write_s())
 * **************************************************************************************/
uint32_t influxDB_uploader::handle_write_s(){
    trace(T_influx1,60);

    // This is the predominant state of the uploader
    // It will wait until there is enough data for the next post
    // then build the payload and initiate the post operation.

    if(_stop){
        stop();
        return 1;
    }

    // If not enough data to post, set wait and return.

    if(Current_log.lastKey() < (_lastSent + _interval + (_interval * _bulkSend))){
        if(oldRecord){
            delete oldRecord;
            oldRecord = nullptr;
            delete newRecord;
            newRecord = nullptr;
        }
        return UTCtime() + 1;
    }

    // If datalog buffers not allocated, do so now and prime latest.

    trace(T_influx1,60);
    if(! oldRecord){
        trace(T_influx1,61);
        oldRecord = new IotaLogRecord;
        newRecord = new IotaLogRecord;
        newRecord->UNIXtime = _lastSent + _interval;
        Current_log.readKey(newRecord);
    }

    // Build post transaction from datalog records.

    while(reqData.available() < uploaderBufferLimit && newRecord->UNIXtime < Current_log.lastKey()){

        if(micros() > bingoTime){
            return 10;
        }
        
        // Swap newRecord top oldRecord, read next into newRecord.

        trace(T_influx1,60);
        IotaLogRecord *swap = oldRecord;
        oldRecord = newRecord;
        newRecord = swap;
        newRecord->UNIXtime = oldRecord->UNIXtime + _interval;
        Current_log.readKey(newRecord);

        // Compute the time difference between log entries.
        // If zero, don't bother.

        trace(T_influx1,62);    
        double elapsedHours = newRecord->logHours - oldRecord->logHours;
        if(elapsedHours == 0){
            trace(T_influx1,63);
            if((newRecord->UNIXtime + _interval) <= Current_log.lastKey()){
                return 1;
            }
            return UTCtime() + 1;
        }

        // Build measurements for this interval

        trace(T_influx1,62); 
        String lastMeasurement;
        String thisMeasurement;
        Script *script = _outputs->first();
        while(script)
        {
            trace(T_influx1,63);     
            double value = script->run(oldRecord, newRecord);
            if(value == value){
                trace(T_influx1,64);   
                thisMeasurement = varStr(_measurement, script);
                if(_staticKeySet && thisMeasurement.equals(lastMeasurement)){
                    reqData.printf_P(PSTR(",%s=%.*f"), lpKey(varStr(_fieldKey, script).c_str()).c_str(), script->precision(), value);
                } else {
                    if(lastMeasurement.length()){
                        reqData.printf(" %d\n", oldRecord->UNIXtime);
                    }
                    reqData.write(lpMeasurement(thisMeasurement.c_str()));
                    if(_tagSet){
                        trace(T_influx1,64);
                        influxTag* tag = _tagSet;
                        while(tag){
                            reqData.printf_P(PSTR(",%s=%s"), lpKey(tag->key).c_str(), lpKey(varStr(tag->value, script).c_str()).c_str());
                            tag = tag->next;
                        }
                    }
                    trace(T_influx1,65);
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
            trace(T_influx1,66);
            Script *script = _outputs->first();
            influxTag* tag = _tagSet;
            while(tag){
                reqData.printf_P(PSTR(",%s=%s"), lpKey(tag->key).c_str(), lpKey(varStr(tag->value, script).c_str()).c_str());
                tag = tag->next;
            }
        }
        reqData.printf_P(PSTR(" value=%d %d\n"), ESP.getFreeHeap(), UTCtime());
    }       

    delete oldRecord;
    oldRecord = nullptr;
    delete newRecord;
    newRecord = nullptr; 

    // Initiate HTTP post.

    String endpoint = "/write?precision=s&db=";
    endpoint += _database;
    if(_retention){
        endpoint += "&rp=";
        endpoint += _retention;
    }
    HTTPPost(endpoint.c_str(), checkWrite_s, "text/plain");
    return 1;
}

/*****************************************************************************************
 *          handle_checkWrite_s()
 * **************************************************************************************/
uint32_t influxDB_uploader::handle_checkWrite_s(){
    trace(T_influx1,91);

    // Check the result of a write transaction.
    // Usually success (204) just note the lastSent and
    // return to buildPost.

    if(_request->responseHTTPcode() == 204){
        delete[] _statusMessage;
        _statusMessage = nullptr;
        _lastSent = _lastPost; 
        _state = write_s;
        trace(T_influx1,93);
        return 1;
    }

    // Deal with failure.

    trace(T_influx1,92);
    char msg[100];
    sprintf_P(msg, PSTR("Post failed %d"), _request->responseHTTPcode());
    delete[] _statusMessage;
    _statusMessage = charstar(msg);
    delete _request;
    _request = nullptr;
    _state = write_s;
    return UTCtime() + 10;
}

/*****************************************************************************************
 *          setRequestHeaders()
 * **************************************************************************************/
void influxDB_uploader::setRequestHeaders(){
    trace(T_influx1,95);
    _request->setDebug(false);
    if(_user && _pwd){
        xbuf xb;
        xb.printf("%s:%s", _user, _pwd);
        base64encode(&xb);
        String auth = "Basic ";
        auth += xb.readString(xb.available());
        _request->setReqHeader("Authorization", auth.c_str()); 
    }
    _request->setReqHeader("Content-Type","application/x-www-form-urlencoded");
    trace(T_influx1,95);
}


//********************************************************************************************************************
//
//               CCC     OOO    N   N   FFFFF   III    GGG    CCC   BBBB
//              C   C   O   O   NN  N   F        I    G      C   C  B   B
//              C       O   O   N N N   FFF      I    G  GG  C      BBBB
//              C   C   O   O   N  NN   F        I    G   G  C   C  B   B
//               CCC     OOO    N   N   F       III    GGG    CCC   B BBB
//
//********************************************************************************************************************
bool influxDB_uploader::configCB(JsonObject& config){
    trace(T_influx1, 101);
    delete[] _database;
    _database = charstar(jStr(config[F("database")]));
    delete[] _user;
    _user = charstar(jStr(config[F("user")]));
    delete[] _pwd;
    _pwd = charstar(jStr(config[F("pwd")]));
    delete _retention;
    _retention = charstar(jStr(config[F("retp")]));
    _heap = jBool(config[F("heap")]);

    trace(T_influx1, 101);
    delete[] _measurement;
    _measurement = charstar(jStr(config[F("measurement")]));
    if (!_measurement)
    {
        _measurement = charstar(F("$name"));
    }
    trace(T_influx1, 102);
    delete[] _fieldKey;
    _fieldKey = charstar(jStr(config[F("fieldkey")]));
    if (!_fieldKey)
    {
        _fieldKey = charstar(F("value"));
    }
    _stop = jBool(config[F("stop")]);

    // Build tagSet

    trace(T_influx1, 103);
    delete _tagSet;
    _tagSet = nullptr;
    JsonArray tagset = config[F("tagset")];
    _staticKeySet = true;
    if ( ! tagset.isNull())
    {
        trace(T_influx1, 103);
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

    const char *measurement = _measurement;
    _outputs->sort([this](Script* a, Script* b)->int {
        return strcmp(varStr(_measurement, a).c_str(), varStr(_measurement, b).c_str());
    });

    // If port wasn't specified, set influxDB_v1 default port.

    if( ! _url->port()){
        _url->port(":8086");
    }

    return true;
}
    

String influxDB_uploader::varStr(const char* in, Script* script)
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
