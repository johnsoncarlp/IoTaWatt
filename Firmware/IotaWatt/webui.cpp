#ifdef IOTAWATT_HOST_TEST
#include "host_web_deps.h"              // host unit tests (Firmware/test/shim)
#else
#include "IotaWatt.h"
#endif
#include "webui.h"

#ifdef IOTAWATT_EMBED_WEBUI

const webuiFile* webuiFind(const char* path){
  if( ! path) return nullptr;
  if(*path == '/') path++;
  for(int i=0; i<WEBUI_COUNT; i++){
    if(strcasecmp_P(path, webuiFiles[i].name) == 0) return &webuiFiles[i];
  }
  return nullptr;
}

        // Embedded unless the SD override is on and the SD has the file (and the request didn't ask
        // for the embedded copy).

bool webuiUseEmbedded(const char* path, bool sdFileExists, bool forceEmbedded){
  if( ! webuiFind(path)) return false;
  return forceEmbedded || ! webuiFromSD || ! sdFileExists;
}

bool webuiServe(const char* path, const char* dataType){
  const webuiFile* file = webuiFind(path);
  if( ! file) return false;
  server.sendHeader(F("Content-Encoding"), F("gzip"));
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send_P(200, dataType, (PGM_P)file->gz, file->gzLen);
  return true;
}

        // Move SD root copies of the embedded files to WEBUI_OLD_DIR. An existing backup is kept: the
        // new one gets a numeric suffix (.1 to .9); if all are taken the file stays where it is (it's
        // not served anyway).

int webuiCleanSD(){
  int moved = 0;
  for(int i=0; i<WEBUI_COUNT; i++){
    char name[24];
    strncpy_P(name, webuiFiles[i].name, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    String path = String("/") + name;
    if( ! SD.exists(path.c_str())) continue;
    File f = SD.open(path.c_str());
    bool isDir = f && f.isDirectory();
    f.close();
    if(isDir) continue;
    if( ! SD.exists(WEBUI_OLD_DIR)) SD.mkdir(WEBUI_OLD_DIR);
    String dest = String(F(WEBUI_OLD_DIR)) + "/" + name;
    for(int n=1; SD.exists(dest.c_str()) && n<=10; n++){
      dest = String(F(WEBUI_OLD_DIR)) + "/" + name + "." + String(n);
    }
    if(SD.exists(dest.c_str()) || ! SDFS.rename(path.c_str(), dest.c_str())){
      log("webui: could not move %s aside (embedded copy is served)", path.c_str());
      continue;
    }
    log("webui: moved old %s to %s (embedded web UI %s is served)", path.c_str(), dest.c_str(), WEBUI_ID);
    moved++;
  }
  return moved;
}

#endif
