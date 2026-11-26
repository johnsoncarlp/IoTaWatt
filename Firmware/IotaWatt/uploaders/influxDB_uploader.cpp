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
 *          writeEndpoint(): where influxBase::handle_write_s() posts
 * **************************************************************************************/
String influxDB_uploader::writeEndpoint(){
    String endpoint = "/write?precision=s&db=";
    endpoint += _database;
    if(_retention){
        endpoint += "&rp=";
        endpoint += _retention;
    }
    return endpoint;
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
    delete[] _retention;                // was delete: allocated with new[] (charstar)
    _retention = charstar(jStr(config[F("retp")]));
    if( ! configLineProtocol(config)) return false;

    // If port wasn't specified, set influxDB_v1 default port.

    if( ! _url->port()){
        _url->port(":8086");
    }

    return true;
}
