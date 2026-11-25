// JsonSummary() and JsonDetail(): moved unchanged from utilities.cpp so they can be unit tested on the
// host (test/test_jsonsplit). setConfig() and the updater use them to parse config.txt and tables.txt in parts.
#ifdef IOTAWATT_HOST_TEST
#include "host_iotawatt.h"           // host unit tests (Firmware/test/shim)
#include "host_xbuf.h"
#include "jsonUtil.h"
#else
#include "IotaWatt.h"
#endif

/**************************************************************************************************
 * JsonSummary, JsonDetail
 * 
 * The config file can be too big to parse into Json dynamically.
 * JsonSummary produces a summary json file to the depth specified. Objects or arrays at greater 
 * depth are represented by a JsonArray with the position and condensed (no whitespace) length.
 * JsonDetail will return the condensed object or array in a char*.
 * ************************************************************************************************/
String  JsonSummary(File file, int depth){
    int     level = -1;
    char    delim[20];
    char    _char;
    bool    string = false;
    bool    escape = false;
    int     varBeg = 0;
    int     varLen = 0;
    xbuf    JsonOut;
    
    while(file.available()){
        _char = file.read();
        if(escape){
          varLen++;
          escape = false;
        }
        else {
            varLen++;
            if(string){
                if(_char == '\"'){
                    string = false;
                }
                else if(_char == '\\'){
                    escape = true;
                }
            }
            else if(isspace(_char)){
                varLen--;
                continue;
            } 
            else if(_char == '\"'){
                string = true;
            }
            else if(_char == '{'){
                delim[++level] = '}';
                if(level == depth){
                    varBeg = file.position()-1;
                    varLen = 0;
                }
            }
            else if(_char == '['){
                delim[++level] = ']';
                if(level == depth){
                    varBeg = file.position()-1;
                    varLen = 0;
                }
            }
            else if(_char == delim[level]){
                level--;
                if(level == depth - 1){
                    JsonOut.printf_P(PSTR("[%d,%d"), varBeg, varLen+1);
                    _char = ']';
                }
            }
        }
        if(level < depth) JsonOut.print(_char);
        if(level < 0){
            break;
        }
    }
    return JsonOut.readString();
}

char*  JsonDetail(File file, JsonArray locator){
    bool    string = false;
    bool    escape = false;
    int segLen = jInt<int>(locator[1]);
    char* out = new char[segLen+1];
    char _char;
    file.seek(jInt<int>(locator[0]));
    for(int i=0; i<segLen;){
        _char = file.read();
        if( ! string && isspace(_char)) continue;
        if(escape){
            escape = false;
        }
        else if(string){
            if(_char == '\"'){
                string = false;
            }
            else if(_char == '\\'){
                escape = true;
            }
        }
        else if(_char == '\"'){
            string = true;
        }
        out [i++] = _char;
    }
    out[segLen] = 0;
    return out;
}
