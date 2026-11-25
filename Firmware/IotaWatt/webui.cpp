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

        // Pages no longer shipped, and where their requests go (graph.js has no replacement: only
        // graph.htm used it).

static const char retiredGraph[] PROGMEM = "graph.htm";
static const char retiredGraphJs[] PROGMEM = "graph.js";
static const struct { const char* name; const char* to; } webuiRetired[] = {
  {retiredGraph,   "/graph2.htm"},
  {retiredGraphJs, nullptr},
};
#define WEBUI_RETIRED_COUNT (sizeof(webuiRetired) / sizeof(webuiRetired[0]))

const char* webuiRedirect(const char* path, bool sdFileExists){
  if( ! path) return nullptr;
  if(*path == '/') path++;
  if(webuiFromSD && sdFileExists) return nullptr;     // SD override: a copy kept on purpose is served
  for(size_t i=0; i<WEBUI_RETIRED_COUNT; i++){
    if(webuiRetired[i].to && strcasecmp_P(path, webuiRetired[i].name) == 0) return webuiRetired[i].to;
  }
  return nullptr;
}


#endif
