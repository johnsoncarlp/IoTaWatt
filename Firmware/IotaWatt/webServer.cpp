#ifdef IOTAWATT_HOST_TEST
#include "host_web_deps.h"              // host unit tests (Firmware/test/shim)
#include "webServer.h"
#include "webui.h"
#else
#include "IotaWatt.h"
#include "uploaders/Uploader_Registry.h"
#endif
#include "webGuard.h"


/*
  This WebServer code is incorporated with very little modification.
  Very simple yet powerful.

  A few new handlers were added at the end, and appropriate server.on
  declarations define them in the Setup code.

  The server supports reading and writing files to/from the SDcard.
  It also serves up HTML files to a browser.  The configuration utility is
  index.htm in the root directory of the SD card.
  The server also came with a great editor utility which, if placed on the SD,
  can be used to edit the web pages or any other text file on the SDcard.
  
  Small parts of the code are imbedded elsewhere as needed in the preamble and Setup sections.
  and "handleClient()" is invoked as often as practical in Loop to keep it running.
  
  The author's copyright and license info follows:

  --------------------------------------------------------------------------------------------------
 
  SDWebServer - Example WebServer with SD Card backend for esp8266

  Copyright (c) 2015  . All rights reserved.
  This file is part of the ESP8266WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Have a FAT Formatted SD Card connected to the SPI port of the ESP8266
  The web root is the SD Card root folder
  File extensions with more than 3 charecters are not supported by the SD Library
  File Names longer than 8 charecters will be truncated by the SD library, so keep filenames shorter
  index.htm is the default index (works on subfolders as well)

  upload the contents of SdRoot to the root of the SDcard and access the editor by going to http://esp8266sd.local/edit

  ----------------------------------------------------------------------------------------------------------------
*/

const char txtPlain_P[] PROGMEM = "text/plain";
const char appJson_P[]  PROGMEM = "application/json";
const char txtJson_P[]  PROGMEM = "text/json";

bool authenticate(authLevel level){
  if(auth(level)){
    return true;
  } 
  requestAuth();
  return false;
}

    /* handleRequest - basic gatekeeper to administrate authorization */

WebGuard webGuard;                      // overload guard state (webGuard.h)

        // Refuse a request with 503 when the guard says so; log the refusals at most once a minute.

static void webGuardLog(){
  log("webServer: busy, refused %u request(s) for low memory (heap %u) and %u for coming too fast",
      webGuard.refusedHeap, ESP.getFreeHeap(), webGuard.refusedFast);
  webGuard.refusedHeap = webGuard.refusedFast = 0;
  webGuard.lastLogMs = millis();
  webGuard.logged = true;
}

static bool webGuardRefuse(const String& uri){
  bool deviceOnly = server.args() == 1 && server.hasArg(F("device"));      // the menu bar's /status?device=yes
  webGuardReason reason = webGuardCheck(webGuard, webRequestCost(uri.c_str(), deviceOnly), ESP.getFreeHeap(), millis(),
                                        (uint32_t)server.client().remoteIP());
  bool due = millis() - webGuard.lastLogMs >= 60000UL;
  if(reason == webGuardOK){
    if(due && (webGuard.refusedHeap || webGuard.refusedFast)) webGuardLog();   // the refusals since the last line
    return false;
  }
  if(reason == webGuardLowHeap) webGuard.refusedHeap++;
  else webGuard.refusedFast++;
  if( ! webGuard.logged || due) webGuardLog();
  server.sendHeader(F("Retry-After"), String(webGuardRetryAfter(reason)));
  server.send(503, txtPlain_P, F("Busy, try again shortly\r\n"));
  return true;
}

void handleRequest(){
  String uri = server.uri();
  if(webGuardRefuse(uri)) return;
  if(serverOn(authUser,  F("/status"),HTTP_GET, handleStatus)) return;
  if(serverOn(authAdmin, F("/vcal"),HTTP_GET, handleVcal)) return;
  if(serverOn(authAdmin, F("/command"), HTTP_GET, handleCommand)) return;
  if(serverOn(authUser,  F("/list"), HTTP_GET, printDirectory)) return;
  if(serverOn(authAdmin, F("/edit"), HTTP_DELETE, handleDelete)) return;
  if(serverOn(authAdmin, F("/edit"), HTTP_PUT, handleCreate)) return;
  if(serverOn(authUser,  F("/feed/list.json"), HTTP_GET, handleGetFeedList)) return;
  if(serverOn(authUser,  F("/feed/data.json"), HTTP_GET, handleGetFeedData)) return;
  if(serverOn(authAdmin, F("/graph/create"),HTTP_POST, handleGraphCreate)) return;
  if(serverOn(authAdmin, F("/graph/update"),HTTP_POST, handleGraphCreate)) return;
  if(serverOn(authAdmin, F("/graph/delete"),HTTP_POST, handleGraphDelete)) return;
  if(serverOn(authUser,  F("/graph/getall"), HTTP_GET, handleGraphGetall)) return;
  if(serverOn(authUser,  F("/graph/getallplus"), HTTP_GET, handleGraphGetallplus)) return;
  if(serverOn(authAdmin, F("/auth"), HTTP_POST, handlePasswords)) return;
  if(serverOn(authUser,  F("/nullreq"), HTTP_GET, returnOK)) return;
  if(serverOn(authUser,  F("/query"), HTTP_GET, handleQuery)) return;
  if(serverOn(authUser,  F("/DSTtest"), HTTP_GET, handleDSTtest)) return;
  if(serverOn(authAdmin, F("/update"), HTTP_GET, handleUpdate)) return;


  if(loadFromSdCard(uri)){
    return;
  }
  
  trace(T_WEB,12); 
  String message = "Not found: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += ", URI: ";
  message += server.uri();
  server.send(404, txtPlain_P, message);
}

bool serverOn(authLevel level, const __FlashStringHelper *uri, HTTPMethod method, genericHandler fn){
  if(strcmp_P(server.uri().c_str(),(PGM_P)uri) == 0 && server.method() == method){
    if( ! authenticate(level)) return true;
    fn();
    return true;
  }
  return false;
}

void returnOK() {
  server.send(200, txtPlain_P, "");
}

void returnFail(String msg, int HTTPcode) {
  server.send(HTTPcode, txtPlain_P, msg + "\r\n");
}

bool loadFromSdCard(String path){
  trace(T_WEB,13);
  if( ! path.startsWith("/")) path = '/' + path;
  String dataType = FPSTR(txtPlain_P);
  if(path.endsWith("/")) path += F("index.htm");
  if(path.equals(F("/edit")) || path.equals(F("/graph")) || path.equals(F("/graph2"))){
    path += F(".htm");
  }
  
  if(path.endsWith(F(".src"))) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(F(".htm"))) dataType = F("text/html");
  else if(path.endsWith(F(".css"))) dataType = F("text/css");
  else if(path.endsWith(F(".js")))  dataType = F("application/javascript");
  else if(path.endsWith(F(".png"))) dataType = F("image/png");
  else if(path.endsWith(F(".gif"))) dataType = F("image/gif");
  else if(path.endsWith(F(".jpg"))) dataType = F("image/jpeg");
  else if(path.endsWith(F(".ico"))) dataType = F("image/x-icon");
  else if(path.endsWith(F(".xml"))) dataType = F("text/xml");
  else if(path.endsWith(F(".pdf"))) dataType = F("application/pdf");
  else if(path.endsWith(F(".zip"))) dataType = F("application/zip");

  if(path.startsWith(F("/esp_spiffs/"))){
    return loadFromSpiffs(path.substring(11), dataType);
  }

  if(path == F(IOTA_AUTH_PATH)){
    returnFail("Protected", 403);
    return false;
  }

  File dataFile = SD.open(path.c_str());
  if(dataFile.isDirectory()){
    path += F("/index.htm");
    dataType = F("text/html");
    dataFile = SD.open(path.c_str());
  }

          // If reading user directory or root files,
          // authenticate as user
          // otherwise require admin.

  authLevel level = authAdmin;
  if(! path.startsWith(F("/config")) &&
     (path.startsWith(F("/user/")) ||
     path.startsWith(F("/graphs/")) ||
     path.substring(1).indexOf('/') == -1) 
     ){
    level = authUser;
  }
  if( ! authenticate(level)) return true;

#ifdef IOTAWATT_EMBED_WEBUI
  const char* movedTo = webuiRedirect(path.c_str(), (bool)dataFile && ! dataFile.isDirectory());
  if(movedTo){
    server.sendHeader(F("Location"), movedTo);
    server.send(302, F("text/plain"), "");   // 302: not cached, so the SD override still works later
    return true;
  }
  {
          // The embedded copy is gzipped. A client that doesn't accept gzip (curl without --compressed,
          // scripts) gets the SD copy if there is one (B75); ?download=true is a download either way.
    bool sdCopy = (bool)dataFile && ! dataFile.isDirectory();
    bool gzipOK = server.header(F("Accept-Encoding")).indexOf(F("gzip")) >= 0;
    if(webuiUseEmbedded(path.c_str(), sdCopy, server.arg(F("ui")) == "embedded") && (gzipOK || ! sdCopy)){
      bool download = server.arg(F("download")) == "true";
      webuiServe(path.c_str(), download ? "application/octet-stream" : dataType.c_str());
      return true;
    }
  }
#endif

  if (server.hasArg(F("download"))){
    if(server.arg(F("download")) == "true") dataType = F("application/octet-stream");  
    else if(server.arg(F("download")) == "yes"){
      handleQuery();
      return true;
    }
  } 

  if (!dataFile){
    return false;
  }

  if(server.hasArg(F("textpos"))){
    sendMsgFile(dataFile, server.arg(F("textpos")).toInt());
  }

  else {
    if(path.equalsIgnoreCase(F("/config.txt"))){
      server.sendHeader(F("X-configSHA256"), base64encode(configSHA256, 32));
    }
    size_t sent = server.streamFile(dataFile, dataType);
    if ( sent != dataFile.size()) {
      Serial.printf_P(PSTR("Server: sent less data than expected. file %s, sent %d, expected %d\r\n"), dataFile.name(), sent, dataFile.size());
    }
  }
  dataFile.close();
  return true;
}

bool loadFromSpiffs(String path, String dataType){
  if( ! spiffsFileExists(path.c_str())){
    server.send(404, txtPlain_P, "Not Found");
    return false;
  }
  String contents = spiffsRead(path.c_str());
  server.send(200, dataType, contents);
  return true;
}

void handleFileUpload(){
  trace(T_WEB, 11);
  static File uploadFile;
  HTTPUpload& upload = server.upload();
  trace(T_WEB, 11, upload.status);
  if(upload.filename.startsWith(F("/esp_spiffs/"))){
    handleSpiffsUpload();
    return;
  }
  
  if(upload.status == UPLOAD_FILE_START){
    if(server.uri() != "/edit") return;
    upload.filename.toLowerCase();
    if(!upload.filename.startsWith("/")){
      upload.filename = "/" + upload.filename;
    }
    //DBG_OUTPUT_PORT.println(upload.filename);
    if(upload.filename.equals(F(IOTA_CONFIG_PATH)) ||
       upload.filename.startsWith(F(IOTA_SYSTEM_DIR))){
      returnFail("Protected", 403);
      return;
    }
    if( ! authenticate(authAdmin)) return;
    if(upload.filename.equals(F(IOTA_CONFIG_NEW_PATH))){
      if(server.hasHeader(F("X-configSHA256"))){
        if(server.header(F("X-configSHA256")) != base64encode(configSHA256, 32)){
          server.send(409, txtPlain_P, F("Config not current"));
          return;
        }
      }
    }
    if(SD.exists((char *)upload.filename.c_str())) SD.remove((char *)upload.filename.c_str());
    if(uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE)){
      //DBG_OUTPUT_PORT.printf_P(PSTR("Upload: START, filename: %s\r\n"), upload.filename.c_str());
    }
  } 
  
  else if(upload.status == UPLOAD_FILE_WRITE){
    if(uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } 
  
  else if(upload.status == UPLOAD_FILE_END){
    if(uploadFile){
      if(upload.filename.equals(F(IOTA_CONFIG_NEW_PATH))){
        hashFile(configSHA256, uploadFile);
        server.sendHeader("X-configSHA256", base64encode(configSHA256, 32));
        getNewConfig = true;
      }
      uploadFile.close();
      //DBG_OUTPUT_PORT.printf_P(PSTR("Upload: END, Size: %d\r\n"), upload.totalSize);
    }
  } else if(upload.status == UPLOAD_FILE_ABORTED){
    if(uploadFile){
      uploadFile.close();
    }
    log("WebServer: Upload aborted: %s", upload.filename.c_str());
  }
}

void handleSpiffsUpload(){
  trace(T_WEB,11);
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    if( ! authenticate(authAdmin)) return;
    upload.filename.toLowerCase();
    //DBG_OUTPUT_PORT.printf_P(PSTR("Upload: START, filename: %s\r\n"), upload.filename.c_str());
    spiffsWrite(upload.filename.substring(11).c_str(), "", 0);        // Create a null file

  } else if(upload.status == UPLOAD_FILE_WRITE){
      spiffsWrite(upload.filename.substring(11).c_str(), upload.buf, upload.currentSize, true);   // append to the file (true)

  } else if(upload.status == UPLOAD_FILE_END){
      //DBG_OUTPUT_PORT.printf_P(PSTR("Upload: END, Size: %d\r\n"), upload.totalSize);
  }
}

void deleteRecursive(String path){
  File file = SD.open((char *)path.c_str());
  if(!file.isDirectory()){
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }

  file.rewindDirectory();
  while(true) {
    File entry = file.openNextFile();
    if (!entry) break;
    String entryPath = path + "/" +entry.name();
    if(entry.isDirectory()){
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    yield();
  }

  SD.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete(){
  trace(T_WEB,9); 
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path.startsWith(F("/esp_spiffs"))){
    spiffsRemove(path.substring(11).c_str());
    returnOK();
    return;
  } 
    if(path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH", 400);
    return;
  }

  if(path == F(IOTA_CONFIG_PATH) || 
     path == (F(IOTA_CURRENT_LOG_PATH)) ||
     path == (F(IOTA_HISTORY_LOG_PATH)) ||
     path == (F(IOTA_AUTH_PATH)))
  {
    returnFail("Restricted File", 403);
    return;
  }
  deleteRecursive(path);
  returnOK();
}

void handleCreate(){
  trace(T_WEB,10); 
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);

  if(path.startsWith(F("/esp_spiffs"))){
    if(spiffsFileExists(path.substring(11).c_str())) return returnFail("BAD PATH");
    spiffsWrite(path.substring(11).c_str(),"");
    returnOK();
    return;
  } 

  if(path == "/" || SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH", 400);
    return;
  }

  if(path.indexOf('.') > 0){
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if(file){
      file.write((const char *)0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
}

void printDirectory() {
  trace(T_WEB,7); 
  if(!server.hasArg("dir")) return returnFail("BAD ARGS");
  String response;
  String path = server.arg("dir");
  if(path.startsWith("/esp_spiffs")){
    response = spiffsDirectory(path.substring(11));
  }
  else {
    if(path != "/" && !SD.exists((char *)path.c_str())) return returnFail("BAD PATH");
    Dir dir = SDFS.openDir(path);
    JsonDocument jsonDoc;
    JsonArray array = jsonDoc.to<JsonArray>();
    if(path == "/"){
      JsonObject object = array.add<JsonObject>();
      object["type"] = "dir";
      object["name"] = "esp_spiffs";
    }
    dir.rewind();
    while(dir.next()){
      if(dir.isDirectory() && !dir.fileName().equalsIgnoreCase(F("system volume information"))){
        JsonObject object = array.add<JsonObject>();
        object["type"] = "dir";
        object["name"] = String(dir.fileName());
      }
    }
    dir.rewind();
    while(dir.next()){
      if(dir.isFile()){
        JsonObject object = array.add<JsonObject>();
        object["type"] = "file";
        object["name"] = String(dir.fileName());
      }
    }
    serializeJson(array, response);
  }  
  server.send(200, appJson_P, response);
}

/************************************************************************************************
 * 
 * Following handlers added to WebServer for IotaWatt specific requests
 * 
 **********************************************************************************************/
void handlePasswords(){
  trace(T_WEB,21);
  int len = server.arg("plain").length();
  char* buff = new char[(len + 2) * 3 / 4];
  len = base64_decode_chars(server.arg("plain").c_str(), len, buff);
  buff[len] = 0;
  String body = buff;
  delete[] buff;
  JsonDocument Json;
  DeserializationError parseError = deserializeJson(Json, body);
  JsonObject request = Json.as<JsonObject>();
  if(parseError || request.isNull()){
    server.send(400, txtPlain_P, F("Json parse failed."));
    return;
  }
  if(adminH1){
    if( ! request["oldadmin"].is<const char*>()){
      server.send(400, txtPlain_P, F("Current admin password required."));
      return;
    }
    String testH1 = calcH1("admin", deviceName, request["oldadmin"].as<const char*>());
    if( adminH1 && ! testH1.equals(bin2hex(adminH1,16))){
      server.send(400, txtPlain_P, F("Current admin password invalid."));
      return;
    }
  }
  
  if( ! request[F("newadmin")].isNull()){
    delete[] adminH1;
    adminH1 = nullptr;
    delete[] userH1;
    userH1 = nullptr;
    String newAdmin = request[F("newadmin")].as<const char*>();
    if(newAdmin.length()){
      String newAdminH1 = calcH1("admin", deviceName, request["newadmin"].as<const char*>());
      adminH1 = new uint8_t[16];
      hex2bin(adminH1, newAdminH1.c_str(), 16);
    } 
    if( ! request[F("newuser")].isNull()){
      String newAdmin = request["newuser"].as<const char*>();
      if(newAdmin.length()){
        String newUserH1 = calcH1("user", deviceName, request["newuser"].as<const char*>());
        userH1 = new uint8_t[16];
        hex2bin(userH1, newUserH1.c_str(), 16);
      } 
    }
    if( ! request[F("localAccess")].isNull()){
      localAccess = jBool(request["localAccess"]);
    }
    if(authSavePwds()){
      server.send(200, txtPlain_P, F("Passwords reset."));
      log("New passwords saved.");
    } else {
      server.send(400, txtPlain_P, F("Error saving passwords."));
      log("Password save failed.");
    } 
  } else {
    server.send(200, txtPlain_P, "");
  } 
  return;
}

void handleStatus(){
  trace(T_WEB,0);
  uint32_t heapEntry = ESP.getFreeHeap();
  String response = "";
  {
    JsonDocument jsonDoc;
    JsonObject root = jsonDoc.to<JsonObject>();

    if(server.hasArg(F("device"))){
      JsonObject device = root[F("device")].to<JsonObject>();
      device[F("name")] = deviceName;
      device[F("timediff")] = localTimeDiff;
      device[F("allowdst")] = timezoneRule?true:false;
      device[F("update")] = updateClass;
    }
    
    if(server.hasArg(F("stats"))){
      trace(T_WEB,14);
      JsonObject stats = root[F("stats")].to<JsonObject>();
      trace(T_WEB,14);
      stats[F("cyclerate")] = samplesPerCycle;
      trace(T_WEB,14);
      stats[F("chanrate")] = cycleSampleRate;
      trace(T_WEB,14);
      stats[F("starttime")] = programStartTime;
      trace(T_WEB,14);
      stats[F("currenttime")] = UTCtime();
      trace(T_WEB,14);
      stats[F("runseconds")] = UTCtime()-programStartTime;
      trace(T_WEB,14);
      stats[F("stack")] = ESP.getFreeHeap();
      trace(T_WEB,14);
      stats[F("version")] = IOTAWATT_VERSION;
      trace(T_WEB,14);
      stats[F("frequency")] = frequency;
      trace(T_WEB,14);
      stats[F("lowbat")] = RTClowBat;
    }
    
    if(server.hasArg(F("inputs"))){
      trace(T_WEB,15);
      JsonArray channelArray = root["inputs"].to<JsonArray>();
      for(int i=0; i<maxInputs; i++){
        if(inputChannel[i]->isActive()){
          JsonObject channelObject = channelArray.add<JsonObject>();
          channelObject[F("channel")] = inputChannel[i]->_channel;
          if(inputChannel[i]->_type == channelTypeVoltage){
            channelObject[F("Vrms")] = statRecord.accum1[i];
            channelObject[F("Hz")] = statRecord.accum2[i];
            channelObject["phase"] = inputChannel[i]->getPhase(inputChannel[i]->dataBucket.volts);
          }
          else if(inputChannel[i]->_type == channelTypePower){
            if(statRecord.accum1[i] > -2 && statRecord.accum1[i] < 2) statRecord.accum1[i] = 0;
            channelObject[F("Watts")] = String(statRecord.accum1[i],0);
            double pf = statRecord.accum2[i];
            if(pf != 0){
              pf = statRecord.accum1[i] / pf;
            }
            channelObject["Pf"] = pf;
            if(inputChannel[i]->_reversed){
              channelObject[F("reversed")] = true;
            }
            double volts = inputChannel[inputChannel[i]->_vchannel]->dataBucket.volts;
            double amps = (volts < 50) ? 0 : inputChannel[i]->dataBucket.VA / volts;
            channelObject["phase"] = inputChannel[i]->getPhase(amps);
            channelObject["lastphase"] = inputChannel[i]->_lastPhase;
          }
        }
      }
    }

    if(server.hasArg(F("outputs"))){
      trace(T_WEB,16);
      JsonArray outputArray = root["outputs"].to<JsonArray>();
      Script* script = outputs->first();
      statRecord.UNIXtime = UTCtime();
      statRecord.logHours = 1;
      while(script){
        trace(T_WEB,16,1);
        JsonObject channelObject = outputArray.add<JsonObject>();
        channelObject[F("name")] = script->name();
        channelObject[F("units")] = script->getUnits();
        double value = script->run(nullptr, &statRecord);
        channelObject[F("value")] = value;
        script = script->next();
      }
      trace(T_WEB,16,2);
    }

    if(server.hasArg(F("uploaders"))){
      char **uploader_names = get_uploader_list();
      int i = -1;
      JsonArray uploaders = root[F("uploaders")].to<JsonArray>();
      while(uploader_names[++i]){
        Uploader *uploader = get_uploader_instance(uploader_names[i]);
        if (uploader){
          JsonObject status = uploaders.add<JsonObject>();
          uploader->getStatusJson(status);
        }
      }

      trace(T_WEB,17,4);
      if(PVoutput){
        JsonObject status = uploaders.add<JsonObject>();
        PVoutput->getStatusJson(status);
      }
    }

    if(server.hasArg(F("datalogs"))){
      trace(T_WEB,18,1);
      JsonArray datalogs = root[F("datalogs")].to<JsonArray>();

      JsonObject currlog = datalogs.add<JsonObject>();
      currlog[F("id")] = "Current";
      currlog[F("firstkey")] = Current_log.firstKey();
      currlog[F("lastkey")] = Current_log.lastKey();
      currlog[F("size")] = Current_log.fileSize();
      currlog[F("interval")] = Current_log.interval();
      //currlog["wrap"] = Current_log._wrap ? true : false;

      trace(T_WEB,18,2);  
      JsonObject histlog = datalogs.add<JsonObject>();
      histlog[F("id")] = "History";
      histlog[F("firstkey")] = History_log.firstKey();
      histlog[F("lastkey")] = History_log.lastKey();
      histlog[F("size")] = History_log.fileSize();
      histlog[F("interval")] = History_log.interval();

      trace(T_WEB,18,3);  
      Script *script = integrations->first();
      while(script){
        integrator *intgr = (integrator *)script->getParm();
        IotaLog *log = intgr ? intgr->get_log() : nullptr;
        if( ! log){                           // integration log not open (yet)
          script = script->next();
          continue;
        }
        JsonObject intlog = datalogs.add<JsonObject>();
        intlog[F("id")] = script->name();
        intlog[F("firstkey")] = log->firstKey();
        intlog[F("lastkey")] = log->lastKey();
        intlog[F("size")] = log->fileSize();
        intlog[F("interval")] = log->interval();
        script = script->next();
      }

      trace(T_WEB,18,4);
    }

    if(server.hasArg(F("wifi"))){
      trace(T_WEB,19,1);
      JsonObject wifi = root[F("wifi")].to<JsonObject>();
      wifi[F("connecttime")] = wifiConnectTime;
      if(wifiConnectTime){
        wifi[F("SSID")] = WiFi.SSID();
        String ip = WiFi.localIP().toString();
        wifi[F("IP")] = ip;
        //Serial.printf("SSID: %s, IP: %s\r\n", WiFi.SSID().c_str(), ip.c_str());
        wifi[F("channel")] = WiFi.channel();
        wifi[F("RSSI")] = WiFi.RSSI();
        wifi[F("mac")] = WiFi.macAddress();
      }
    }

    if(server.hasArg(F("passwords"))){
      trace(T_WEB,19,2);
      JsonObject passwords = root[F("passwords")].to<JsonObject>();
      passwords[F("admin")] = adminH1 != nullptr;
      passwords[F("user")] = userH1 != nullptr;
      passwords[F("localAccess")] = localAccess;
    }
    serializeJson(root, response);
  }
  server.send(200, appJson_P, response);
}

void handleVcal(){
  trace(T_WEB,1); 
  if( ! (server.hasArg(F("channel")) && server.hasArg("cal"))){
    server.send(400, txtJson_P, F("Missing parameters"));
    return;
  }
  JsonDocument jsonDoc;
  JsonObject root = jsonDoc.to<JsonObject>();
  int channel = server.arg(F("channel")).toInt();
  if(channel < 0 || channel >= maxInputs){
    server.send(400, txtJson_P, F("Invalid channel"));
    return;
  }
  float Vrms = sampleVoltage(channel, server.arg("cal").toFloat());
  root["vrms"] = Vrms;
  String response = "";
  serializeJson(root, response);
  server.send(200, txtJson_P, response);  
}

void handleCommand(){
  trace(T_WEB,2); 
  if(server.hasArg(F("restart"))) {
    trace(T_WEB,3); 
    server.send(200, "text/plain", "ok");
    log("Restart command received.");
    delay(500);
    ESP.restart();
  }
  if(server.hasArg(F("vtphase"))){
    trace(T_WEB,4); 
    uint16_t chan = server.arg("vtphase").toInt();
    int refChan = 0;
    if(server.hasArg(F("refchan"))){
      refChan = server.arg(F("refchan")).toInt();
    }
    uint16_t shift = 100;
    if(server.hasArg(F("shift"))){
      shift = server.arg(F("shift")).toInt();
    }
    if(chan >= maxInputs || refChan < 0 || refChan >= maxInputs){
      server.send(400, txtPlain_P, F("Invalid channel"));
      return;
    }
    char response[100];
    sprintf_P(response, PSTR("Phase Shift chan %d ref %d, %.2f"), chan, refChan, samplePhase(refChan, chan, shift));
    server.send(200, txtPlain_P, response);
    return; 
  }
  if(server.hasArg(F("sample"))){
    trace(T_WEB,5); 
    uint16_t chan = server.arg(F("sample")).toInt();
    if(chan >= maxInputs){
      server.send(400, txtPlain_P, F("Invalid channel"));
      return;
    }
    int rtc = sampleCycle(inputChannel[inputChannel[chan]->_vchannel], inputChannel[chan]);
    if(rtc){
      char response[100];
      sprintf_P(response, PSTR("Sample Channel %d, return code %d"), chan, rtc);
      server.send(200, txtPlain_P, response);
      return;
    }
    getSamples();
    return; 
  }
  if(server.hasArg(F("disconnect"))) {
    trace(T_WEB,6); 
    server.send(200, txtPlain_P, "ok");
    log("Disconnect command received.");
    WiFi.disconnect(false);
    return;
  }
  if(server.hasArg(F("deletelog"))) {
    trace(T_WEB,21); 
    String arg = server.arg(F("deletelog"));
    log("deletelog=%s command received.", arg.c_str());
    if(arg == "current"){
      trace(T_WEB,21); 
      Current_log.end();
      deleteRecursive(IOTA_CURRENT_LOG_PATH);
    } 
    else if(arg == "history"){
      trace(T_WEB,22); 
      History_log.end();
      deleteRecursive(IOTA_HISTORY_LOG_PATH);
    }
    else if(arg == "both"){
      trace(T_WEB,23);
      Current_log.end();
      deleteRecursive(IOTA_CURRENT_LOG_PATH);
      History_log.end();
      deleteRecursive(IOTA_HISTORY_LOG_PATH);
    }
    else {
      server.send(400, txtPlain_P, F("Specify current, history, or both."));
      return;
    }
    server.send(200, txtPlain_P, "ok");
    delay(1000);
    ESP.restart();
  }
  server.send(400, txtPlain_P, F("Unrecognized request"));
}

void handleGetFeedList(){ 
  trace(T_WEB,18);
  String response;
  {
    JsonDocument jsonDoc;
    JsonArray array = jsonDoc.to<JsonArray>();
    for(int i=0; i<maxInputs; i++){
      if(inputChannel[i]->isActive()){
        if(inputChannel[i]->_type == channelTypeVoltage){
          JsonObject voltage = array.add<JsonObject>();
          voltage["id"] = String("IV") + String(inputChannel[i]->_name);
          voltage["tag"] = F("Voltage");
          voltage["name"] = inputChannel[i]->_name;
        } 
        else
          if(inputChannel[i]->_type == channelTypePower){
          JsonObject power = array.add<JsonObject>();
          power["id"] = String("IP") + String(inputChannel[i]->_name);
          power["tag"] = F("Power");
          power["name"] = inputChannel[i]->_name;
          JsonObject energy = array.add<JsonObject>();
          energy["id"] = String("IE") + String(inputChannel[i]->_name);
          energy["tag"] = F("Energy");
          energy["name"] = inputChannel[i]->_name;
        }
      }
    }
    trace(T_WEB,18);
    if(outputs){
      Script* script = outputs->first();
      int outndx = 100;
      while(script){
        if(String(script->name()).indexOf(' ') == -1){
          String units = script->getUnits();
          if(units.equalsIgnoreCase("volts")){
            JsonObject voltage = array.add<JsonObject>();
            voltage["id"] = String("OV") + String(script->name());
            voltage["tag"] = F("Voltage");
            voltage["name"] = script->name();
          } 
          else if(units.equalsIgnoreCase("watts")) {
            JsonObject power = array.add<JsonObject>();
            power["id"] = String("OP") + String(script->name());
            power["tag"] = F("Power");
            power["name"] = script->name();
            JsonObject energy = array.add<JsonObject>();
            energy["id"] = String("OE") + String(script->name());
            energy["tag"] = F("Energy");
            energy["name"] = script->name();
          }
          else {
            JsonObject other = array.add<JsonObject>();
            other["id"] = String("OO") + String(script->name());
            other["tag"] = F("Outputs");
            other["name"] = script->name();
          }
        }
        script = script->next();
      }
    }
    serializeJson(array, response);
  }
  server.send(200, appJson_P,response);
}

void handleGetFeedData(){
  serverAvailable = false;
  getFeedData();
  return;
}

// Had to roll our own streamFile function so we can set the actual partial
// file length rather than the total file length.  Safari won't work otherwise.
// No big deal.  BTW/ This instance of Client.send is depricated in the newer
// ESP8266WiFiClient, so probably change at some point. (Remove buffer size parameter).

void sendMsgFile(File &dataFile, int32_t relPos){
    trace(T_WEB,20);
    int32_t absPos = relPos;
    if(relPos < 0) absPos = dataFile.size() + relPos;
    dataFile.seek(absPos);
    while(dataFile.available()){
      if(dataFile.read() == '\n') break;
    }
    absPos = dataFile.position();
    server.setContentLength(dataFile.size() - absPos);
    server.send(200, txtPlain_P, "");
    WiFiClient _client = server.client();
#ifdef CORE_3_0
    dataFile.sendAll(_client);                // core 3: WiFiClient::write(Stream&) is deprecated
#else
    _client.write(dataFile);
#endif
}


void handleQuery(){
  trace(T_WEB,50);
  CSVquery* query = new CSVquery();
  if( ! query->setup()){
    trace(T_WEB,51);
    String response("{\"error\":\"invalid query. ");
    response += query->failReason() + "\"}";
    server.send(400, txtPlain_P, response);
  } else {
    trace(T_WEB,52);
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    //server.sendHeader(String("Connection"), String("keep-alive"));
    if(server.hasArg(F("download"))){
      trace(T_WEB,53);
      server.send(200,"application/octet-stream","");
    }
    else if(query->isJson()){
      trace(T_WEB,54);
      server.send(200, appJson_P, "");
    }
    else {
      trace(T_WEB,55);
      server.send(200, txtPlain_P, "");
    }
      
    uint8_t* buf = new uint8_t[1440];
    int read = 0;
    size_t size = 0;
    trace(T_WEB,56);
    while((read = query->readResult(buf, 1440))){
      trace(T_WEB,57);
      server.sendContent((char*)buf, read);
      size += read;
      trace(T_WEB,58);
      yield();
    }
    trace(T_WEB,56);
    //server.sendContent((char*)buf, 0);
    if(query->timedOut()){
      log("query: time limit reached, transfer aborted (incomplete JSON array).");
      server.client().stop();                 // no final chunk: the client sees an error, not partial data
    }
    delete[] buf;
  }
  delete query;
  trace(T_WEB,59);
}

void handleUpdate(){
  if( ! server.hasArg(F("release"))){
    server.send(400, txtPlain_P, F("No release specified."));
    return;
  }
  String release = server.arg(F("release"));
  if(unpackUpdate(release)){
    if(installUpdate(release)){
      log ("Updater: Firmware updated, restarting.");
      server.send(200, txtPlain_P, F("Firmware updated, restarting."));
      delay(1000);
      ESP.restart();
    }
    else {
      server.send(400, txtPlain_P, F("Firmware update failed."));
    }
  }
  else {
    server.send(400, txtPlain_P, F("Release file not validated."));
    return;
  }
}
  
    

void handleDSTtest(){
    uint32_t begin = server.arg(F("begin")).toInt();
    uint32_t end = server.arg(F("end")).toInt();
    xbuf buf;
    while(begin <= end){
      buf.printf_P(PSTR("UTC %d, %s, Local %d, %s, UTC %d, %s"),
        begin, datef(begin).c_str(),
        localTime(begin), datef(localTime(begin)).c_str(),
        UTCtime(localTime(begin)), datef(UTCtime(localTime(begin))).c_str());
      if(begin != UTCtime(localTime(begin))){
        buf.print(" FAILED");
      }
      buf.println();
      begin += 60;
      if(buf.available() > 8000){
        buf.println("too big");
        break;
      }
    }
    server.send(200, txtPlain_P, buf.readString(buf.available()).c_str());
}
        // Seems to work better when sending chunk as a single write
        // including chunk header, body, and footer (\r\n).
        // This function accepts a char* buffer and length to send.
        // Buffer must have 6 bytes free at start for header and
        // must be long enough to add two byte footer.
        // bufPos is end of body (chunksize+6).

size_t sendChunk(char* buf, size_t bufPos){
  sprintf(buf,"%04x\r",bufPos-6);
  *(buf+5) = '\n';
  memcpy(buf+bufPos,"\r\n",2);
  return server.client().write(buf,bufPos+2);
}