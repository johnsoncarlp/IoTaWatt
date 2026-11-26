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

        // Send a JSON array built from the files in the graph directory: list() is called with each
        // file's contents (null-terminated) and appends its entry, if any, to response.

static void sendGraphList(std::function<void(File& file, char* contents, uint32_t size, String& response)> list){
  File directory = SD.open(graphDir);
  String response = "[";
  if(directory && directory.isDirectory()){
    File graphFile;
    while((graphFile = directory.openNextFile())){
      uint32_t fileSize = graphFile.size();
      char* bufr = new char[fileSize + 1];
      graphFile.read((uint8_t*)bufr, fileSize);
      bufr[fileSize] = 0;
      list(graphFile, bufr, fileSize, response);
      delete[] bufr;
      graphFile.close();
    }
  }
  if(directory) directory.close();
  response += ']';
  server.send(200, appJson_P, response);
}

static void addGraphId(String& response, File& file){
  response += ",\"id\":\"";
  response += file.name();
  response += "\"}";
}

        // The original Graph page's graphs: those with "start", sent whole.

void handleGraphGetall(){
  sendGraphList([](File& file, char* contents, uint32_t size, String& response){
    if(size < 10) return;                       // too short to hold "start"
    contents[size - 1] = 0;                     // drop the closing brace, addGraphId puts it back
    if( ! strstr(contents, "\"start\"")) return;
    if( ! response.endsWith("[")) response += ',';
    response += contents;
    addGraphId(response, file);
  });
}

        // Graph+'s graphs: those without "start", sent as name and id.

void handleGraphGetallplus(){
  sendGraphList([](File& file, char* contents, uint32_t size, String& response){
    JsonDocument Json;
    DeserializationError jsonError = deserializeJson(Json, contents);
    JsonObject graph = Json.as<JsonObject>();
    if(jsonError || graph.isNull() || ! graph[F("start")].isNull()) return;
    if( ! response.endsWith("[")) response += ',';
    response += "{\"name\":\"";
    response += graph[F("name")].as<const char*>();
    response += '"';
    addGraphId(response, file);
  });
}

