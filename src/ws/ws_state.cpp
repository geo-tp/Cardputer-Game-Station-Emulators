#include "ws_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "oswan/WS.h"
#include "oswan/WSApu.h"
}

#include "share/emu_log_cpp.h"
#include "share/game_save.h"
#include "ws_sound.h"

#ifdef WS_LOGS_ENABLED
#define WS_LOG(...) EMU_LOG(__VA_ARGS__)
#else
#define WS_LOG(...) ((void)0)
#endif

#define WS_STATE_DIR "/sd/ws_saves"
#define WS_STATE_MAGIC 0x54535357u
#define WS_STATE_VERSION 1u

typedef struct WsStateFileHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t headerSize;
  uint32_t payloadVersion;
  uint32_t romBanks;
  uint32_t ramBanks;
  uint32_t ramSize;
  uint32_t cartKind;
  uint32_t sramSize;
  uint32_t reserved[7];
} WsStateFileHeader;

static char* g_state_path = nullptr;
static volatile bool g_request_save = false;
static volatile bool g_request_load = false;

static void make_state_path(const char* romPathOrName)
{
  mkdir(WS_STATE_DIR, 0777);

  const char* base = share::gameSaveBasename(romPathOrName);
  char name[160] = {0};
  if (base && *base) {
    strncpy(name, base, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  } else {
    strcpy(name, "ws_autosave");
  }

  char* dot = strrchr(name, '.');
  if (dot) *dot = '\0';
  strncat(name, ".state", sizeof(name) - strlen(name) - 1);

  int n = snprintf(g_state_path, PATH_MAX, WS_STATE_DIR "/%s", name);
  if (n < 0 || (size_t)n >= PATH_MAX) {
    g_state_path[PATH_MAX - 1] = '\0';
  }
}

void ws_state_init(const char* romPathOrName)
{
  if (!g_state_path) {
    g_state_path = (char*)malloc(PATH_MAX);
    if (!g_state_path) {
      WS_LOG("[WS][STATE] disabled (OOM on path alloc)\n");
      return;
    }
  }

  g_state_path[0] = '\0';
  make_state_path(romPathOrName);
  g_request_save = false;
  g_request_load = false;
  WS_LOG("[WS][STATE] path=%s\n", g_state_path);
}

void ws_state_request_save(void)
{
  g_request_save = true;
}

void ws_state_request_load(void)
{
  g_request_load = true;
}

bool ws_state_save_now(void)
{
  if (!g_state_path || !g_state_path[0]) return false;
  if (!share::gameSaveEnsureParentReady(WS_STATE_DIR)) {
    WS_LOG("[WS][STATE] save skipped, storage not ready\n");
    return false;
  }

  WsStateFileHeader hdr = {};
  hdr.magic = WS_STATE_MAGIC;
  hdr.version = WS_STATE_VERSION;
  hdr.headerSize = sizeof(hdr);
  hdr.payloadVersion = WsStatePayloadVersion();
  hdr.romBanks = (uint32_t)ROMBanks;
  hdr.ramBanks = (uint32_t)RAMBanks;
  hdr.ramSize = (uint32_t)RAMSize;
  hdr.cartKind = (uint32_t)CartKind;
  hdr.sramSize = WsStateSramSize();

  share::setGameIsSaving(true);
  ws_sound_pause_task(true);
  apuClearRing();

  FILE* fp = fopen(g_state_path, "wb");
  bool ok = false;
  if (fp) {
    ok = fwrite(&hdr, 1, sizeof(hdr), fp) == sizeof(hdr) &&
         WsSaveStatePayload(fp);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
  }

  ws_sound_pause_task(false);
  share::setGameIsSaving(false);

  if (ok) {
    WS_LOG("[WS][STATE] saved %s\n", g_state_path);
  } else {
    WS_LOG("[WS][STATE] save failed %s\n", g_state_path);
  }
  return ok;
}

bool ws_state_load_now(void)
{
  if (!g_state_path || !g_state_path[0]) return false;
  if (!share::gameSaveEnsureParentReady(WS_STATE_DIR)) {
    WS_LOG("[WS][STATE] load skipped, storage not ready\n");
    return false;
  }

  FILE* fp = fopen(g_state_path, "rb");
  if (!fp) {
    WS_LOG("[WS][STATE] no state file: %s\n", g_state_path);
    return false;
  }

  WsStateFileHeader hdr = {};
  bool ok = fread(&hdr, 1, sizeof(hdr), fp) == sizeof(hdr) &&
            hdr.magic == WS_STATE_MAGIC &&
            hdr.version == WS_STATE_VERSION &&
            hdr.headerSize == sizeof(hdr) &&
            hdr.payloadVersion == WsStatePayloadVersion() &&
            hdr.romBanks == (uint32_t)ROMBanks &&
            hdr.ramBanks == (uint32_t)RAMBanks &&
            hdr.ramSize == (uint32_t)RAMSize &&
            hdr.cartKind == (uint32_t)CartKind;

  if (ok) {
    share::setGameIsSaving(true);
    ws_sound_pause_task(true);
    ok = WsLoadStatePayload(fp, hdr.sramSize) != 0;
    ws_sound_pause_task(false);
    share::setGameIsSaving(false);
  }
  fclose(fp);

  if (ok) {
    WS_LOG("[WS][STATE] loaded %s\n", g_state_path);
  } else {
    WS_LOG("[WS][STATE] load failed or incompatible: %s\n", g_state_path);
  }
  return ok;
}

void ws_state_tick(void)
{
  if (g_request_load) {
    g_request_load = false;
    ws_state_load_now();
  }
  if (g_request_save) {
    g_request_save = false;
    ws_state_save_now();
  }
}
