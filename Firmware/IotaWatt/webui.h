#ifndef webui_h
#define webui_h

/*******************************************************************************************************
 * Embedded web UI (IOTAWATT_EMBED_WEBUI, set by scripts/embed_webui.py)
 *
 * The web UI files from SD/ are compiled into the firmware, gzipped, so the UI always matches the
 * firmware it runs with. By default the embedded copy is served; copies of those files in the SD
 * root are ignored (and left alone, see below).
 *
 * The "sdwebui" device setting (Device page: "Use web UI files from SD card") serves SD copies
 * instead when present, for custom or in-development UIs; the embedded copy remains the fallback for
 * missing files, and any request with ?ui=embedded gets the embedded copy (rescue from a broken page).
 *
 * Retired pages (the original Graph page, replaced by Graph+) are no longer shipped: requests for
 * them redirect to the replacement.
 *
 * The SD card's copies of the web UI files are left in place: they aren't served unless "Use web UI
 * files from SD card" is on, and other firmware (an official IoTaWatt release flashed over USB) still
 * finds its web UI there (B74).
 ******************************************************************************************************/

#ifdef IOTAWATT_EMBED_WEBUI
#include "webui_embedded.h"

const webuiFile* webuiFind(const char* path);   // "/index.htm" etc., case-insensitive; nullptr if not embedded
bool webuiUseEmbedded(const char* path, bool sdFileExists, bool forceEmbedded);
bool webuiServe(const char* path, const char* dataType);   // send the embedded copy; false if none
const char* webuiRedirect(const char* path, bool sdFileExists);   // where a retired page went; nullptr if not retired
#endif

#endif
