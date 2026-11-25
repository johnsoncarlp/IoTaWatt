#ifdef IOTAWATT_HOST_TEST
#include "host_web_deps.h"              // host unit tests (Firmware/test/shim)
#else
#include "IotaWatt.h"
#endif

#define graphDir "graphs"
String hashName(const char* name);

void handleGraphCreate(){
  File graphFile = SD.open(graphDir);
  if(graphFile){
    if( ! graphFile.isDirectory()){
      graphFile.close();
      SD.remove(graphDir);
      SD.mkdir(graphDir);
    }
  }
  else {
    SD.mkdir(graphDir);
  }
  
  trace(T_uploadGraph,11);
  JsonDocument Json;
  DeserializationError jsonError = deserializeJson(Json, server.arg("data"));
  JsonObject graph = Json.as<JsonObject>();
  if(jsonError || ! graph["name"].is<const char*>()){
    server.send(400, txtPlain_P, F("Invalid graph data."));
    return;
  }
  String filePath = graphDir;
  String fileName = hashName(graph["name"].as<const char*>());
  filePath += "/" + fileName + ".txt";
  SD.remove(filePath);
  graphFile = SD.open(filePath, FILE_WRITE);
  if( ! graphFile){
    server.send(500, txtPlain_P, F("Couldn't create file."));
    return;
  } 
  graphFile.write(server.arg("data").c_str());
  graphFile.close();
  String response(F("{\"success\":true,\"message\":\"graph saved id:"));
  response += fileName + "\"}";
  server.send(200, appJson_P, response);  
  return;
}

void handleGraphDelete(){
  String filePath = graphDir;
  String fileName = server.arg("id");
  filePath += "/" + fileName;
  SD.remove(filePath);
  server.send(200, txtJson_P, F("{\"success\":true,\"message\":\"deleted\"}"));
}

void handleGraphGetall(){
  File directory = SD.open(graphDir);
  File graphFile;
  if( !(directory) || !(directory.isDirectory()) || !(graphFile = directory.openNextFile())){
    if(directory){
      directory.close(); 
    }
    server.send(200, appJson_P, "[]");
    return;
  }
  String response = "[";
  while(graphFile){
    uint32_t fileSize = graphFile.size();
    if(fileSize < 10){                          // too short to hold "start" (and fileSize-1/-9 would wrap)
      graphFile.close();
      graphFile = directory.openNextFile();
      continue;
    }
    char* bufr = new char[fileSize];
    graphFile.read((uint8_t*)bufr, fileSize);
    bufr[fileSize-1] = 0;
    char* ptr = bufr;
    for(int i=0; i<fileSize-9; i++){
      if(memcmp(ptr++, "\"start\"", 7) == 0){
        if( ! response.endsWith("[")) response += ',';
        response += bufr;
        response += ",\"id\":\"";
        response += graphFile.name();
        response += "\"}";
        break;
      }   
    }
    delete[] bufr;
    graphFile.close();
    graphFile = directory.openNextFile();
  }
  response += ']';
  server.send(200, appJson_P, response);
  directory.close();
  return;
}

void handleGraphGetallplus(){
  File directory = SD.open(graphDir);
  File graphFile;
  if( !(directory) || !(directory.isDirectory()) || !(graphFile = directory.openNextFile())){
    if(directory){
      directory.close(); 
    }
    server.send(200, appJson_P, "[]");
    return;
  }
  String response = "[";
  while(graphFile){
    uint32_t fileSize = graphFile.size();
    char* bufr = new char[fileSize+1];
    graphFile.read((uint8_t*)bufr, fileSize);
    bufr[fileSize] = 0;
    JsonDocument Json;
    DeserializationError jsonError = deserializeJson(Json, bufr);
    JsonObject graph = Json.as<JsonObject>();
    if( ! jsonError && ! graph.isNull()){
      if(graph[F("start")].isNull()){
        if( ! response.endsWith("[")) response += ',';
        response += "{\"name\":\"";
        response += graph[F("name")].as<const char*>();
        response += "\",\"id\":\"";
        response += graphFile.name();
        response += "\"}";
      }
    }
    delete[] bufr;
    graphFile.close();
    graphFile = directory.openNextFile();
  }
  response += ']';
  server.send(200, appJson_P, response);
  directory.close();
  return;
}

