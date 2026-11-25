#ifndef webui_h
#define webui_h

/*******************************************************************************************************
 * Embedded web UI (IOTAWATT_EMBED_WEBUI, set by scripts/embed_webui.py)
 *
 * The web UI files from SD/ are compiled into the firmware, gzipped, so the UI always matches the
 * firmware it runs with. By default the embedded copy is served, and old copies of those files in
 * the SD root are moved to /iotawatt/webui-old/ at boot so nothing stale is left in place.
 *
 * The "sdwebui" device setting (Device page: "Use web UI files from SD card") serves SD copies
 * instead when present, for custom or in-development UIs; the embedded copy remains the fallback for
 * missing files, and any request with ?ui=embedded gets the embedded copy (rescue from a broken page).
 ******************************************************************************************************/

#ifdef IOTAWATT_EMBED_WEBUI
#include "webui_embedded.h"

#define WEBUI_OLD_DIR "/iotawatt/webui-old"

const webuiFile* webuiFind(const char* path);   // "/index.htm" etc., case-insensitive; nullptr if not embedded
bool webuiUseEmbedded(const char* path, bool sdFileExists, bool forceEmbedded);
bool webuiServe(const char* path, const char* dataType);   // send the embedded copy; false if none
int  webuiCleanSD();                            // move SD root copies aside; returns how many were moved
#endif

#endif
