#include "ws_save.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "share/game_save.h"
#include "share/emu_log_cpp.h"

extern "C" {
// Core WS 
extern int RAMSize;
extern int RAMBanks;
extern unsigned char** RAMMap;
extern unsigned char* MemDummy;
extern int CartKind;
extern int WsSramBackingActive(void);
extern int WsSramBackingDirty(void);
extern void WsSramBackingClearDirty(void);
extern void WsSramBackingFlush(void);
extern unsigned char WsSramBackingRead(int offset);
extern void WsSramBackingWrite(int offset, unsigned char value);
}

#ifndef CK_EEP
#define CK_EEP 0x01
#endif

#define WS_SAVE_DIR "/sd/ws_saves"

static uint8_t*     g_sram        = nullptr;   // pointer vers RAMMap
static size_t       g_sram_len    = 0;         // = RAMSize
static bool         g_sram_backed = false;     // SD/cache backed SRAM fallback
static char*        g_save_path   = nullptr;
static uint32_t     g_crc_last    = 0;
static TickType_t   g_next_check  = 0;
static TickType_t   g_next_allow  = 0;
static TaskHandle_t g_task        = nullptr;
static volatile bool g_flag_flush = false;
static volatile bool g_flag_check = false;
static TickType_t g_first_dirty = 0;
static TickType_t g_last_save   = 0;

// ====================== FS utils ======================
static void ensure_dir(){ mkdir(WS_SAVE_DIR, 0777); }

static void make_save_path(const char* romPathOrName){
  ensure_dir();

  const char* base = share::gameSaveBasename(romPathOrName);
  char name[160] = {0};

  if (base && *base){
    strncpy(name, base, sizeof(name)-1);
    size_t L = strlen(name);
    if (L >= 4) { name[L-3]='s'; name[L-2]='a'; name[L-1]='v'; }
    else strncat(name, ".sav", sizeof(name)-strlen(name)-1);
  } else {
    strcpy(name, "ws_autosave.sav");
  }

  int n = snprintf(g_save_path, PATH_MAX, WS_SAVE_DIR "/%s", name);
  if (n < 0 || (size_t)n >= PATH_MAX) {
    g_save_path[PATH_MAX-1] = '\0';
  }
}

// ====================== I/O ======================
static bool flush_now(){
  if ((!g_sram && !g_sram_backed) || !g_sram_len) return false;
  if (!share::gameSaveEnsureParentReady(WS_SAVE_DIR)) return false;
  if (!g_sram_backed && share::gameSaveIsTrivialSram(g_sram, g_sram_len)) return false;

  if (g_sram_backed) WsSramBackingFlush();

  FILE* f = fopen(g_save_path, "r+b");
  if (!f && g_sram_backed) {
    f = fopen(g_save_path, "wb");
  }
  if (!f) {
    return false;
  }

  const size_t CHUNK = 512;
  size_t remaining = g_sram_len;
  size_t offset = 0;
  uint8_t buf[CHUNK];

  while (remaining > 0) {
    size_t n = remaining < CHUNK ? remaining : CHUNK;
    const uint8_t* src = g_sram ? (g_sram + offset) : buf;
    if (g_sram_backed) {
      for (size_t i = 0; i < n; ++i) {
        buf[i] = WsSramBackingRead((int)(offset + i));
      }
    }
    size_t w = fwrite(src, 1, n, f);
    if (w != n) {
      // erreur write
      fclose(f);
      EMU_LOG("[WS][SAVE] write error saving %s\n", g_save_path);
      return false;
    }
    offset    += n;
    remaining -= n;
    taskYIELD();
  }

  fflush(f);
  fsync(fileno(f));
  fclose(f);

  EMU_LOG("[WS][SAVE] wrote %u bytes -> %s\n",
         (unsigned)g_sram_len, g_save_path);

  if (g_sram_backed) WsSramBackingClearDirty();
  return true;
}

// ====================== Task ======================
static void SaveTask(void*){
  for(;;){
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CHECK_MS));
    bool do_check = g_flag_check; g_flag_check=false;
    bool do_flush = g_flag_flush; g_flag_flush=false;
    TickType_t now = xTaskGetTickCount();

    if (do_check && g_sram_backed && g_sram_len) {
      if (WsSramBackingDirty() && now >= g_next_allow) {
        share::setGameIsSaving(true);
        do_flush = true;
      }
    } else if (do_check && g_sram && g_sram_len){
      uint32_t crc = share::gameSaveCrc32Update(0, g_sram, g_sram_len); // CRC zone utile
      if (crc != g_crc_last && now >= g_next_allow){
        g_crc_last = crc;
        if (!share::gameSaveIsTrivialSram(g_sram, g_sram_len)) {
          // SRAM modified
          share::setGameIsSaving(true);
          do_flush = true;
        } else {
          EMU_LOG("[WS][SAVE] trivial after change, skip\n");
        }
      }
    }

    if (do_flush && now >= g_next_allow){
      bool ok = flush_now();
      g_next_allow = xTaskGetTickCount() + pdMS_TO_TICKS(GAP_MS);
      share::setGameIsSaving(false);
      if (!ok) {
        // Fail, force dirty
        g_crc_last = 0xFFFFFFFFu;
        EMU_LOG("[WS][SAVE] save failed, will retry on next tick\n");
      }
    }
  }
}

// ====================== API ======================
void ws_save_init(const char* romPathOrName){
  bool is_eep  = (CartKind & CK_EEP) != 0;

  // EEP : 128 / 1024 / 2048 octets
  // SRAM : entre 1 et 32 Ko
  #ifdef WS_SAVE_ENABLED
  bool ok_size = is_eep
      ? (RAMSize == 0x80 || RAMSize == 0x400 || RAMSize == 0x800)
      : (RAMSize > 0 && RAMSize <= 0x8000); // max 32KB SRAM
  #else
  bool ok_size = false;
  #endif

  const bool has_sram_ptr = RAMMap && RAMMap[0] && RAMMap[0] != MemDummy;
  const bool has_sram_backing = !is_eep && ok_size && WsSramBackingActive();

  if (!ok_size || RAMBanks < 1 || (!has_sram_ptr && !has_sram_backing)) {
    EMU_LOG("[WS][SAVE] ignored (size=%d, banks=%d, kind=%s)\n",
           RAMSize, RAMBanks, is_eep ? "EEP" : "SRAM");
    g_sram      = nullptr;
    g_sram_backed = false;
    g_sram_len  = 0;
    g_crc_last  = 0;
    g_next_check = g_next_allow = 0;
    g_first_dirty = g_last_save = 0;
    return;
  }

  if (!g_save_path) {
    g_save_path = (char*)malloc(PATH_MAX);
    if (!g_save_path) {
      EMU_LOG("[WS][SAVE] ignored (OOM on path alloc)\n");
      g_sram      = nullptr;
      g_sram_backed = false;
      g_sram_len  = 0;
      g_crc_last  = 0;
      g_next_check = g_next_allow = 0;
      g_first_dirty = g_last_save = 0;
      return;
    }
  }

  g_save_path[0] = '\0';
  g_sram         = has_sram_ptr ? RAMMap[0] : nullptr;
  g_sram_backed  = !has_sram_ptr && has_sram_backing;
  g_sram_len     = (size_t)RAMSize;

  // Construit chemin
  make_save_path(romPathOrName);

  if (share::gameSaveEnsureParentReady(WS_SAVE_DIR)) {
    FILE* f = fopen(g_save_path, "rb");
    if (!f) {
      f = fopen(g_save_path, "wb");
      if (f) {
        const size_t CHUNK = 512;
        uint8_t buf[CHUNK];
        memset(buf, 0xFF, sizeof(buf));

        size_t remaining = g_sram_len;
        while (remaining > 0) {
          size_t n = (remaining < CHUNK) ? remaining : CHUNK;
          size_t w = fwrite(buf, 1, n, f);
          if (w != n) {
            EMU_LOG("[WS][SAVE] prealloc write error\n");
            break;
          }
          remaining -= n;
        }

        fflush(f);
        fsync(fileno(f));
        fclose(f);

        EMU_LOG("[WS][SAVE] preallocated save file %s (%u bytes)\n",
               g_save_path, (unsigned)g_sram_len);
      } else {
        EMU_LOG("[WS][SAVE] failed to create save file: %s\n", g_save_path);
      }
    } else {
      fclose(f);
    }
  } else {
    EMU_LOG("[WS][SAVE] storage path not ready, will try later\n");
  }

  // CRC initial
  g_crc_last    = g_sram_backed ? 0 : share::gameSaveCrc32Update(0, g_sram, g_sram_len);
  g_next_check  = 0;
  g_next_allow  = 0;
  g_first_dirty = 0;
  g_last_save   = 0;
  g_flag_check  = false;
  g_flag_flush  = false;
  if (g_sram_backed) WsSramBackingClearDirty();

  if (!g_task) {
    xTaskCreatePinnedToCore(
      SaveTask,
      "WS_SaveTask",
      3072,
      nullptr,
      6,
      &g_task,
      0
    );
  }

  EMU_LOG("[WS][SAVE] path=%s len=%u (%s)\n",
         g_save_path,
         (unsigned)g_sram_len,
         is_eep ? "EEP" : "SRAM");
}

void ws_save_load(void){
  if ((!g_sram && !g_sram_backed) || !g_sram_len) return;
  if (!share::gameSaveEnsureParentReady(WS_SAVE_DIR)) {
    EMU_LOG("[WS][SAVE] skip load (storage not ready)\n");
    return;
  }

  if (g_sram) memset(g_sram, 0xFF, g_sram_len);

  FILE* f = fopen(g_save_path, "rb");
  if (!f){
    // .sav manquant
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_save_path);

    // Tenter le rename
    if (rename(tmp_path, g_save_path) == 0) {
      EMU_LOG("[WS][SAVE] promoted temp -> sav: %s\n", g_save_path);
      f = fopen(g_save_path, "rb"); // rouvre le .sav
    } else {
      // lire .tmp
      f = fopen(tmp_path, "rb");
      if (f) {
        EMU_LOG("[WS][SAVE] loading from temp (rename failed)\n");
      } else {
        EMU_LOG("[WS][SAVE] no save and no temp: %s\n", g_save_path);
        return;
      }
    }
  }

  const size_t CHUNK = 512;
  size_t remaining = g_sram_len;
  size_t offset = 0;
  uint8_t* dst = g_sram;
  uint8_t buf[CHUNK];

  while (remaining > 0) {
    size_t want = remaining < CHUNK ? remaining : CHUNK;
    size_t got  = fread(g_sram_backed ? buf : dst, 1, want, f);
    if (got == 0) break;
    if (g_sram_backed) {
      for (size_t i = 0; i < got; ++i) {
        WsSramBackingWrite((int)(offset + i), buf[i]);
      }
    } else {
      dst += got;
    }
    offset    += got;
    remaining -= got;
    taskYIELD();
  }
  fclose(f);

  if (remaining && g_sram) memset(dst, 0xFF, remaining);

  g_crc_last = g_sram_backed ? 0 : share::gameSaveCrc32Update(0, g_sram, g_sram_len);
  if (g_sram_backed) WsSramBackingClearDirty();
  EMU_LOG("[WS][SAVE] loaded %u/%u from %s\n",
         (unsigned)(g_sram_len - remaining), (unsigned)g_sram_len, g_save_path);
}

void ws_save_tick(void){
  if ((!g_sram && !g_sram_backed) || !g_sram_len) return;
  TickType_t now = xTaskGetTickCount();
  if (now < g_next_check) return;
  g_next_check = now + pdMS_TO_TICKS(CHECK_MS);
  g_flag_check = true;
  if (g_task) xTaskNotifyGive(g_task);
}

void ws_save_request_flush(void){
  if (!g_sram && !g_sram_backed) return;
  g_flag_flush = true;
  if (g_task) xTaskNotifyGive(g_task);
}

void ws_save_force_flush(void){
  flush_now();
}
